#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Polls an MFRC522 (RC522) NFC/RFID reader over SPI for a card UID.
//
// Wiring: SDA/CS->SPI CE0, SCK->SPI SCLK, MOSI->SPI MOSI, MISO->SPI MISO,
// IRQ unused, RST->3.3V (the chip is soft-reset over SPI on Open(), so a
// GPIO reset line isn't required), 3.3V->3.3V, GND->GND. On Raspberry Pi
// OS, enable SPI first (`raspi-config` -> Interface Options -> SPI, or
// `dtparam=spi=on` in /boot/firmware/config.txt), which creates
// /dev/spidev0.0.
//
// Only reads single-size (4-byte) UIDs -- i.e. cascade level 1 anticollision
// -- which covers the Mifare Classic 1K/4K cards and keyfobs bundled with
// most RC522 starter kits. 7-byte UID tags (Mifare Ultralight/NTAG) aren't
// supported.
//
// Linux-only: on other platforms (e.g. macOS dev builds) Open() always
// fails and PollForUid() always returns false, so callers don't need to
// #ifdef around this class.
class NfcReader
{
public:
    ~NfcReader();

    // Opens the SPI device and soft-resets/initializes the MFRC522. Returns
    // false (and leaves the reader unusable) if the device can't be opened
    // or the chip doesn't respond -- e.g. SPI not enabled, wrong wiring, or
    // (always, on non-Linux platforms) unsupported.
    bool Open(const std::string& spiDevice = "/dev/spidev0.0");
    void Close();

    bool IsOpen() const { return fd_ >= 0; }

    // Non-blocking: polls the reader once for a card in the field. Returns
    // true and fills outUid (4 bytes) if one answered. Safe to call at any
    // rate (e.g. once per rendered frame) -- each call is a handful of fast
    // SPI transactions, not a network round trip.
    bool PollForUid(std::vector<uint8_t>& outUid);

private:
    int fd_ = -1;

    uint8_t ReadRegister(uint8_t reg);
    void WriteRegister(uint8_t reg, uint8_t value);
    void SetBitMask(uint8_t reg, uint8_t mask);
    void ClearBitMask(uint8_t reg, uint8_t mask);
    void AntennaOn();

    // Runs a PCD command (e.g. transceive) and collects the card's reply.
    // Returns true on success and fills outBackData/outBackBits.
    bool CommunicateWithCard(uint8_t command, const std::vector<uint8_t>& sendData,
        std::vector<uint8_t>& outBackData, unsigned& outBackBits);

    bool RequestA();
    bool AnticollisionRead(std::vector<uint8_t>& outUid);
};
