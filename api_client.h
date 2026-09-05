#pragma once

#include <string>
#include <vector>

struct ScheduleEntry
{
    std::string time;   // e.g. "08:00 - 09:15"
    std::string course; // class name
    std::string room;   // room name, or "TBD"
    int classId = -1;   // ClassDto.id, for resolving a check-in session
};

struct TotpInfo
{
    std::string code;
    int secondsRemaining = 0;
};

// Talks to the AttendanceApi described in api-docs.json.
class ApiClient
{
public:
    explicit ApiClient(std::string baseUrl);

    // Picks how the client authenticates, in priority order:
    //   1. ATTENDANCE_API_KEY  -> device auth, header "X-Api-Key: {id}.{secret}"
    //      (a CheckInEndpoint scoped to one room; no user JWT involved).
    //   2. ATTENDANCE_API_TOKEN -> a pre-issued bearer token, used as-is.
    //   3. ATTENDANCE_API_EMAIL/ATTENDANCE_API_PASSWORD -> POST /api/Auth/login.
    //   4. otherwise -> GET /api/Auth/dev-token, for local development.
    // Returns true if a credential was obtained; the client still works
    // unauthenticated (returns false) for endpoints that don't require one.
    bool Authenticate();

    // True once ATTENDANCE_API_KEY authentication is active. An endpoint's
    // room is implicit in its key (server-side room_id claim), so callers
    // should skip ResolveRoomId/roomId filtering and use FetchScheduleForEndpoint.
    bool IsEndpointAuth() const { return !apiKey_.empty(); }

    // True if the last request to the API succeeded (i.e. the server is reachable).
    bool IsReachable() const { return reachable_; }

    // GET /api/Rooms -> finds the room whose name matches (case-insensitive),
    // e.g. ATTENDANCE_ROOM=207 matching RoomDto.name "207". Only meaningful
    // for user (JWT) auth; an endpoint's room comes from its API key instead.
    bool ResolveRoomId(const std::string& roomName, int& outRoomId);

    // GET /api/Classes (optionally filtered to one room) -> schedule rows,
    // formatted for display. roomId < 0 means no filter (all rooms).
    bool FetchSchedule(std::vector<ScheduleEntry>& outSchedule, int roomId = -1);

    // GET /api/Classes/schedule (endpoint-auth only) -> schedule rows for
    // whatever room this endpoint's API key is scoped to.
    bool FetchScheduleForEndpoint(std::vector<ScheduleEntry>& outSchedule);

    // Picks a class session to show a check-in QR/code for: prefers
    // ATTENDANCE_CLASS_SESSION_ID from the environment, otherwise the most
    // recent session (by id) of the first class in the given room (or the
    // first class overall if roomId < 0).
    bool ResolveClassSessionId(int& outSessionId, int roomId = -1);

    // GET /api/ClassSessions?classId={classId} -> the most recent session id
    // for that class. Used by the endpoint-auth path once
    // FetchScheduleForEndpoint has given us a classId. NOTE: as of the
    // CheckInEndpoint changes, only /api/Classes/schedule, .../totp and
    // .../checkin are confirmed to accept endpoint (X-Api-Key) auth — this
    // call may 401 for endpoint credentials until GET /api/ClassSessions is
    // updated too. Treat a false return here as "not supported yet", not a
    // hard error.
    bool ResolveSessionIdForClass(int classId, int& outSessionId);

    // GET /api/ClassSessions/{id}/totp
    bool FetchTotp(int sessionId, TotpInfo& outTotp);

private:
    std::string baseUrl_;
    std::string token_;
    std::string apiKey_;
    bool reachable_ = false;

    std::string Url(const std::string& path) const;
    std::vector<std::string> AuthHeaders() const;
};
