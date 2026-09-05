#include "rfid_client.h"

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>

#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#include "json/json.hpp"

using json = nlohmann::json;

namespace
{
    int HexNibble(char c)
    {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    }

    // "04A3B2C1" -> { 0x04, 0xA3, 0xB2, 0xC1 }. Returns false on anything
    // that isn't valid hex of even length (including empty input).
    bool HexDecode(const std::string& hex, std::vector<uint8_t>& outBytes)
    {
        if (hex.empty() || hex.size() % 2 != 0)
            return false;

        outBytes.clear();
        outBytes.reserve(hex.size() / 2);
        for (size_t i = 0; i < hex.size(); i += 2)
        {
            int hi = HexNibble(hex[i]);
            int lo = HexNibble(hex[i + 1]);
            if (hi < 0 || lo < 0)
                return false;
            outBytes.push_back(static_cast<uint8_t>((hi << 4) | lo));
        }
        return true;
    }
}

RfidSocketClient::RfidSocketClient(std::string socketPath) : socketPath_(std::move(socketPath))
{
}

RfidSocketClient::~RfidSocketClient()
{
    Disconnect();
}

void RfidSocketClient::Stop()
{
    stop_.store(true);
}

void RfidSocketClient::Disconnect()
{
    if (fd_ >= 0)
    {
        close(fd_);
        fd_ = -1;
    }
    recvBuffer_.clear();
}

bool RfidSocketClient::EnsureConnected()
{
    if (fd_ >= 0)
        return true;

    int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0)
        return false;

    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    strncpy(address.sun_path, socketPath_.c_str(), sizeof(address.sun_path) - 1);

    if (connect(sock, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0)
    {
        // Expected whenever the Python server hasn't started yet (or the
        // Pi has no reader wired up at all, e.g. a dev machine) -- retried
        // silently by WaitForCard's loop, not logged as an error.
        close(sock);
        return false;
    }

    // Bounds how long a blocking read() in WaitForCard can run, so it
    // still notices Stop() (or a server that's gone silent) promptly
    // instead of hanging until the next event arrives.
    timeval recvTimeout{};
    recvTimeout.tv_sec = 0;
    recvTimeout.tv_usec = 500000; // 500ms
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &recvTimeout, sizeof(recvTimeout));

    fd_ = sock;
    printf("RfidSocketClient: connected to %s\n", socketPath_.c_str());
    return true;
}

bool RfidSocketClient::WaitForCard(std::vector<uint8_t>& outUid)
{
    while (!stop_.load())
    {
        if (!EnsureConnected())
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            continue;
        }

        // Drain any complete lines already buffered before blocking for more.
        size_t newlinePos;
        while ((newlinePos = recvBuffer_.find('\n')) != std::string::npos)
        {
            std::string line = recvBuffer_.substr(0, newlinePos);
            recvBuffer_.erase(0, newlinePos + 1);
            if (line.empty())
                continue;

            try
            {
                json parsed = json::parse(line);
                std::string eventType = parsed.value("event", "");
                std::string uidHex = parsed.value("uid", "");

                // "removed" (and any future event type) is ignored -- the
                // Python side only sends "card" once per tap already.
                if (eventType == "card" && HexDecode(uidHex, outUid))
                    return true;
            }
            catch (const json::exception& e)
            {
                printf("RfidSocketClient: malformed message %s: %s\n", line.c_str(), e.what());
            }
        }

        char temp[512];
        ssize_t bytesRead = read(fd_, temp, sizeof(temp));

        if (bytesRead > 0)
        {
            recvBuffer_.append(temp, bytesRead);
            continue;
        }

        if (bytesRead == 0)
        {
            printf("RfidSocketClient: %s closed the connection; reconnecting.\n", socketPath_.c_str());
            Disconnect();
            continue;
        }

        if (errno == EAGAIN || errno == EWOULDBLOCK)
            continue; // just the SO_RCVTIMEO timeout -- loop back to recheck Stop()

        printf("RfidSocketClient: read() failed: %s; reconnecting.\n", strerror(errno));
        Disconnect();
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    return false;
}
