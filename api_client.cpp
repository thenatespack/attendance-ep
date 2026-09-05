#include "api_client.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>

#include "json/json.hpp"

#include "env.h"
#include "http_client.h"

using json = nlohmann::json;

namespace
{
    // Several DTO fields in api-docs.json are typed as ["integer","string"]
    // (e.g. id, classId, roomId) — the server accepts/emits either. Parse both.
    int JsonToInt(const json& value, int fallback = 0)
    {
        if (value.is_number_integer())
            return value.get<int>();
        if (value.is_string())
        {
            const std::string& s = value.get_ref<const std::string&>();
            if (!s.empty())
                return std::atoi(s.c_str());
        }
        return fallback;
    }

    std::string JsonToString(const json& value, const std::string& fallback = "")
    {
        if (value.is_string())
            return value.get<std::string>();
        return fallback;
    }

    // "08:00:00" -> "08:00"
    std::string FormatTime(const std::string& isoTime)
    {
        if (isoTime.size() >= 5)
            return isoTime.substr(0, 5);
        return isoTime;
    }

    bool EqualsCaseInsensitive(const std::string& a, const std::string& b)
    {
        if (a.size() != b.size())
            return false;
        for (size_t i = 0; i < a.size(); i++)
        {
            if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i]))
                return false;
        }
        return true;
    }

    // AutoCheckInDto.nfcUid is `type: string, format: byte` -- OpenAPI's way
    // of saying "base64-encoded bytes". nlohmann::json only knows how to
    // serialize the string we hand it, so the encoding has to happen here.
    std::string Base64Encode(const std::vector<uint8_t>& data)
    {
        static const char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

        std::string out;
        out.reserve(((data.size() + 2) / 3) * 4);

        size_t i = 0;
        for (; i + 3 <= data.size(); i += 3)
        {
            uint32_t n = (data[i] << 16) | (data[i + 1] << 8) | data[i + 2];
            out += table[(n >> 18) & 0x3F];
            out += table[(n >> 12) & 0x3F];
            out += table[(n >> 6) & 0x3F];
            out += table[n & 0x3F];
        }

        size_t remaining = data.size() - i;
        if (remaining == 1)
        {
            uint32_t n = data[i] << 16;
            out += table[(n >> 18) & 0x3F];
            out += table[(n >> 12) & 0x3F];
            out += "==";
        }
        else if (remaining == 2)
        {
            uint32_t n = (data[i] << 16) | (data[i + 1] << 8);
            out += table[(n >> 18) & 0x3F];
            out += table[(n >> 12) & 0x3F];
            out += table[(n >> 6) & 0x3F];
            out += "=";
        }

        return out;
    }

    // Parses an array of ClassDto into display-ready schedule rows. Shared
    // by FetchSchedule and FetchScheduleForEndpoint (same DTO shape).
    bool ParseClassesToSchedule(const json& parsed, std::vector<ScheduleEntry>& outSchedule)
    {
        if (!parsed.is_array())
            return false;

        outSchedule.clear();
        for (const json& item : parsed)
        {
            ScheduleEntry entry;
            entry.classId = JsonToInt(item.value("id", json(-1)), -1);

            std::string start = FormatTime(JsonToString(item.value("startTime", json(nullptr)), ""));
            std::string end = FormatTime(JsonToString(item.value("endTime", json(nullptr)), ""));

            if (!start.empty() && !end.empty())
                entry.time = start + " - " + end;
            else
                entry.time = "TBD";

            entry.course = JsonToString(item.value("name", json("")), "(unnamed class)");

            std::string room = JsonToString(item.value("roomName", json(nullptr)), "");
            entry.room = room.empty() ? "No room" : room;

            outSchedule.push_back(std::move(entry));
        }
        return true;
    }
}

ApiClient::ApiClient(std::string baseUrl) : baseUrl_(std::move(baseUrl))
{
    if (!baseUrl_.empty() && baseUrl_.back() != '/')
        baseUrl_ += '/';
}

std::string ApiClient::Url(const std::string& path) const
{
    return baseUrl_ + path;
}

std::vector<std::string> ApiClient::AuthHeaders() const
{
    if (!apiKey_.empty())
        return { "X-Api-Key: " + apiKey_ };
    if (token_.empty())
        return {};
    return { "Authorization: Bearer " + token_ };
}

bool ApiClient::Authenticate()
{
    std::string apiKey = dotenv::Get("ATTENDANCE_API_KEY");
    if (!apiKey.empty())
    {
        apiKey_ = apiKey;
        // Not marked reachable_ yet: unlike a JWT, this credential is never
        // exchanged with the server up front, so reachability is only known
        // once the first real request (schedule/totp) succeeds or fails.
        return true;
    }

    std::string presetToken = dotenv::Get("ATTENDANCE_API_TOKEN");
    if (!presetToken.empty())
    {
        token_ = presetToken;
        reachable_ = true;
        return true;
    }

    std::string email = dotenv::Get("ATTENDANCE_API_EMAIL");
    std::string password = dotenv::Get("ATTENDANCE_API_PASSWORD");

    if (!email.empty() && !password.empty())
    {
        json body;
        body["email"] = email;
        body["password"] = password;

        HttpResponse response = HttpPostJson(Url("api/Auth/login"), body.dump());
        if (response.ok)
        {
            reachable_ = true;
            try
            {
                json parsed = json::parse(response.body);
                token_ = JsonToString(parsed["token"]);
                return !token_.empty();
            }
            catch (const json::exception& e)
            {
                printf("ApiClient: failed to parse login response: %s\n", e.what());
            }
        }
        else
        {
            printf("ApiClient: login failed (%ld): %s\n", response.statusCode,
                response.error.empty() ? response.body.c_str() : response.error.c_str());
        }
        return false;
    }

    // Local development fallback.
    std::string url = Url("api/Auth/dev-token");
    std::string role = dotenv::Get("ATTENDANCE_API_ROLE");
    if (!role.empty())
        url += "?role=" + role;

    HttpResponse response = HttpGet(url);
    if (!response.ok)
    {
        printf("ApiClient: dev-token request failed (%ld): %s\n", response.statusCode,
            response.error.empty() ? response.body.c_str() : response.error.c_str());
        return false;
    }

    reachable_ = true;
    try
    {
        json parsed = json::parse(response.body);
        token_ = JsonToString(parsed["token"]);
        return !token_.empty();
    }
    catch (const json::exception& e)
    {
        printf("ApiClient: failed to parse dev-token response: %s\n", e.what());
        return false;
    }
}

bool ApiClient::ResolveRoomId(const std::string& roomName, int& outRoomId)
{
    HttpResponse response = HttpGet(Url("api/Rooms"), AuthHeaders());
    if (!response.ok)
    {
        printf("ApiClient: GET /api/Rooms failed (%ld): %s\n", response.statusCode,
            response.error.empty() ? response.body.c_str() : response.error.c_str());
        return false;
    }

    reachable_ = true;

    try
    {
        json rooms = json::parse(response.body);
        if (!rooms.is_array())
            return false;

        for (const json& room : rooms)
        {
            if (EqualsCaseInsensitive(JsonToString(room.value("name", json(""))), roomName))
            {
                outRoomId = JsonToInt(room["id"]);
                return true;
            }
        }

        printf("ApiClient: no room named \"%s\" found in /api/Rooms\n", roomName.c_str());
        return false;
    }
    catch (const json::exception& e)
    {
        printf("ApiClient: failed to parse /api/Rooms response: %s\n", e.what());
        return false;
    }
}

bool ApiClient::FetchSchedule(std::vector<ScheduleEntry>& outSchedule, int roomId)
{
    std::string url = Url("api/Classes");
    if (roomId >= 0)
        url += "?roomId=" + std::to_string(roomId);

    HttpResponse response = HttpGet(url, AuthHeaders());
    if (!response.ok)
    {
        printf("ApiClient: GET /api/Classes failed (%ld): %s\n", response.statusCode,
            response.error.empty() ? response.body.c_str() : response.error.c_str());
        return false;
    }

    reachable_ = true;

    try
    {
        return ParseClassesToSchedule(json::parse(response.body), outSchedule);
    }
    catch (const json::exception& e)
    {
        printf("ApiClient: failed to parse /api/Classes response: %s\n", e.what());
        return false;
    }
}

bool ApiClient::FetchScheduleForEndpoint(std::vector<ScheduleEntry>& outSchedule)
{
    HttpResponse response = HttpGet(Url("api/Classes/schedule"), AuthHeaders());
    if (!response.ok)
    {
        printf("ApiClient: GET /api/Classes/schedule failed (%ld): %s\n", response.statusCode,
            response.error.empty() ? response.body.c_str() : response.error.c_str());
        return false;
    }

    reachable_ = true;

    try
    {
        return ParseClassesToSchedule(json::parse(response.body), outSchedule);
    }
    catch (const json::exception& e)
    {
        printf("ApiClient: failed to parse /api/Classes/schedule response: %s\n", e.what());
        return false;
    }
}

bool ApiClient::ResolveClassSessionId(int& outSessionId, int roomId)
{
    std::string configured = dotenv::Get("ATTENDANCE_CLASS_SESSION_ID");
    if (!configured.empty())
    {
        outSessionId = std::atoi(configured.c_str());
        return true;
    }

    std::string classesUrl = Url("api/Classes");
    if (roomId >= 0)
        classesUrl += "?roomId=" + std::to_string(roomId);

    HttpResponse classesResponse = HttpGet(classesUrl, AuthHeaders());
    if (!classesResponse.ok)
        return false;

    reachable_ = true;

    try
    {
        json classes = json::parse(classesResponse.body);
        if (!classes.is_array() || classes.empty())
            return false;

        // Not every class has a session yet (e.g. none created for today) —
        // try each until one resolves instead of giving up after the first.
        for (const json& cls : classes)
        {
            if (ResolveSessionIdForClass(JsonToInt(cls["id"]), outSessionId))
                return true;
        }
        return false;
    }
    catch (const json::exception&)
    {
        return false;
    }
}

bool ApiClient::ResolveSessionIdForClass(int classId, int& outSessionId)
{
    HttpResponse sessionsResponse = HttpGet(Url("api/ClassSessions?classId=" + std::to_string(classId)), AuthHeaders());
    if (!sessionsResponse.ok)
        return false;

    reachable_ = true;

    try
    {
        json sessions = json::parse(sessionsResponse.body);
        if (!sessions.is_array() || sessions.empty())
            return false;

        // Prefer the highest id (most recently created session).
        int bestId = JsonToInt(sessions[0]["id"]);
        for (const json& session : sessions)
            bestId = std::max(bestId, JsonToInt(session["id"]));

        outSessionId = bestId;
        return true;
    }
    catch (const json::exception&)
    {
        return false;
    }
}

bool ApiClient::FetchTotp(int sessionId, TotpInfo& outTotp)
{
    HttpResponse response = HttpGet(Url("api/ClassSessions/" + std::to_string(sessionId) + "/totp"), AuthHeaders());
    if (!response.ok)
        return false;

    reachable_ = true;

    try
    {
        json parsed = json::parse(response.body);
        outTotp.code = JsonToString(parsed["code"]);
        outTotp.secondsRemaining = JsonToInt(parsed["secondsRemaining"]);
        return !outTotp.code.empty();
    }
    catch (const json::exception& e)
    {
        printf("ApiClient: failed to parse totp response: %s\n", e.what());
        return false;
    }
}

bool ApiClient::AutoCheckIn(const std::vector<uint8_t>& nfcUid, AutoCheckInResult& outResult,
    std::string& outErrorCode, std::string& outErrorMessage)
{
    json body;
    body["nfcUid"] = Base64Encode(nfcUid);

    HttpResponse response = HttpPostJson(Url("api/ClassSessions/auto-checkin"), body.dump(), AuthHeaders());
    if (!response.ok)
    {
        outErrorCode.clear();
        outErrorMessage.clear();

        // A non-2xx status still means the transport succeeded, so the body
        // is (expected to be) the `{ "error", "message" }` shape below --
        // try that first and only fall back once it doesn't parse.
        try
        {
            json parsed = json::parse(response.body);
            outErrorCode = JsonToString(parsed.value("error", json("")));
            outErrorMessage = JsonToString(parsed.value("message", json("")));
        }
        catch (const json::exception&)
        {
        }

        if (outErrorMessage.empty())
        {
            outErrorMessage = !response.error.empty() ? response.error
                : !response.body.empty() ? response.body
                : "HTTP " + std::to_string(response.statusCode);
        }
        return false;
    }

    reachable_ = true;

    try
    {
        json parsed = json::parse(response.body);
        outResult.classSessionId = JsonToInt(parsed["classSessionId"]);
        outResult.classId = JsonToInt(parsed["classId"]);
        outResult.className = JsonToString(parsed["className"]);
        outResult.studentId = JsonToInt(parsed["studentId"]);
        outResult.studentFirstName = JsonToString(parsed["studentFirstName"]);
        outResult.studentLastName = JsonToString(parsed["studentLastName"]);
        outResult.alreadyCheckedIn = parsed.value("alreadyCheckedIn", false);
        return true;
    }
    catch (const json::exception& e)
    {
        outErrorMessage = e.what();
        return false;
    }
}
