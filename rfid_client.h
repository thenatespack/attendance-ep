#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

// Client for the Python RFID server (rfid-py/rfid_server.py) running
// alongside this app on the Pi: it owns the RC522 hardware and streams
// newline-delimited JSON events -- {"event":"card","uid":"<hex>",...} on a
// tap, {"event":"removed",...} when the card leaves the field -- over a
// Unix domain socket. This app connects to that socket as a client and only
// acts on "card" events; the Python side already dedupes repeat events for
// a card left sitting on the reader, so there's no presence-tracking to
// replicate here.
//
// Reconnects automatically (the socket may not exist yet if the Python
// server hasn't started, or may drop if it restarts), so callers don't need
// to handle that themselves.
class RfidSocketClient
{
public:
    explicit RfidSocketClient(std::string socketPath = "/tmp/rfid.sock");
    ~RfidSocketClient();

    // Blocks until a "card" event arrives, filling outUid with its raw
    // bytes (decoded from the event's hex uid string) and returning true.
    // Returns false only once Stop() has been called -- a dropped
    // connection or missing socket is retried internally, not surfaced as
    // failure, so this only returns once there's a real event or shutdown.
    bool WaitForCard(std::vector<uint8_t>& outUid);

    // Unblocks a concurrent WaitForCard() call so a background thread
    // running it can be joined promptly.
    void Stop();

private:
    bool EnsureConnected();
    void Disconnect();

    std::string socketPath_;
    int fd_ = -1;
    std::string recvBuffer_;
    std::atomic<bool> stop_{false};
};
