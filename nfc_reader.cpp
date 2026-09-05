#include "nfc_reader.h"

#ifdef __linux__

#include <cstdio>
#include <cstring>

#include <fcntl.h>
#include <linux/spi/spidev.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace
{
    // MFRC522 register addresses (see the datasheet's register map).
    constexpr uint8_t CommandReg    = 0x01;
    constexpr uint8_t CommIEnReg    = 0x02;
    constexpr uint8_t CommIrqReg    = 0x04;
    constexpr uint8_t ErrorReg      = 0x06;
    constexpr uint8_t FIFODataReg   = 0x09;
    constexpr uint8_t FIFOLevelReg  = 0x0A;
    constexpr uint8_t ControlReg    = 0x0C;
    constexpr uint8_t BitFramingReg = 0x0D;
    constexpr uint8_t ModeReg       = 0x11;
    constexpr uint8_t TxControlReg  = 0x14;
    constexpr uint8_t TxASKReg      = 0x15;
    constexpr uint8_t TModeReg      = 0x2A;
    constexpr uint8_t TPrescalerReg = 0x2B;
    constexpr uint8_t TReloadRegH   = 0x2C;
    constexpr uint8_t TReloadRegL   = 0x2D;

    // PCD (reader) commands.
    constexpr uint8_t PCD_Idle       = 0x00;
    constexpr uint8_t PCD_Transceive = 0x0C;
    constexpr uint8_t PCD_SoftReset  = 0x0F;

    // PICC (card) commands.
    constexpr uint8_t PICC_REQA      = 0x26;
    constexpr uint8_t PICC_ANTICOLL1 = 0x93;

    constexpr uint32_t kSpiSpeedHz = 1000000;
}

NfcReader::~NfcReader()
{
    Close();
}

bool NfcReader::Open(const std::string& spiDevice)
{
    Close();

    fd_ = open(spiDevice.c_str(), O_RDWR);
    if (fd_ < 0)
    {
        printf("NfcReader: could not open %s (is SPI enabled? see nfc_reader.h)\n", spiDevice.c_str());
        return false;
    }

    uint8_t mode = SPI_MODE_0;
    uint8_t bits = 8;
    uint32_t speed = kSpiSpeedHz;

    if (ioctl(fd_, SPI_IOC_WR_MODE, &mode) < 0 ||
        ioctl(fd_, SPI_IOC_WR_BITS_PER_WORD, &bits) < 0 ||
        ioctl(fd_, SPI_IOC_WR_MAX_SPEED_HZ, &speed) < 0)
    {
        printf("NfcReader: SPI ioctl setup failed on %s\n", spiDevice.c_str());
        Close();
        return false;
    }

    // Soft reset, then the standard MFRC522 init sequence (timer for its
    // internal timeout, 100% ASK modulation, CRC preset), then power the
    // antenna on.
    WriteRegister(CommandReg, PCD_SoftReset);

    WriteRegister(TModeReg, 0x8D);
    WriteRegister(TPrescalerReg, 0x3E);
    WriteRegister(TReloadRegL, 30);
    WriteRegister(TReloadRegH, 0);
    WriteRegister(TxASKReg, 0x40);
    WriteRegister(ModeReg, 0x3D);
    AntennaOn();

    // VersionReg (0x37) reads back a known value (0x91/0x92 for common
    // clones) on a chip that's actually there and talking SPI correctly;
    // 0x00 or 0xFF means nothing answered.
    uint8_t version = ReadRegister(0x37);
    if (version == 0x00 || version == 0xFF)
    {
        printf("NfcReader: no response from MFRC522 on %s (check wiring)\n", spiDevice.c_str());
        Close();
        return false;
    }

    return true;
}

void NfcReader::Close()
{
    if (fd_ >= 0)
    {
        close(fd_);
        fd_ = -1;
    }
}

uint8_t NfcReader::ReadRegister(uint8_t reg)
{
    uint8_t tx[2] = { static_cast<uint8_t>(((reg << 1) & 0x7E) | 0x80), 0x00 };
    uint8_t rx[2] = { 0, 0 };

    struct spi_ioc_transfer transfer;
    std::memset(&transfer, 0, sizeof(transfer));
    transfer.tx_buf = reinterpret_cast<unsigned long>(tx);
    transfer.rx_buf = reinterpret_cast<unsigned long>(rx);
    transfer.len = 2;
    transfer.speed_hz = kSpiSpeedHz;
    transfer.bits_per_word = 8;

    ioctl(fd_, SPI_IOC_MESSAGE(1), &transfer);
    return rx[1];
}

void NfcReader::WriteRegister(uint8_t reg, uint8_t value)
{
    uint8_t tx[2] = { static_cast<uint8_t>((reg << 1) & 0x7E), value };
    uint8_t rx[2] = { 0, 0 };

    struct spi_ioc_transfer transfer;
    std::memset(&transfer, 0, sizeof(transfer));
    transfer.tx_buf = reinterpret_cast<unsigned long>(tx);
    transfer.rx_buf = reinterpret_cast<unsigned long>(rx);
    transfer.len = 2;
    transfer.speed_hz = kSpiSpeedHz;
    transfer.bits_per_word = 8;

    ioctl(fd_, SPI_IOC_MESSAGE(1), &transfer);
}

void NfcReader::SetBitMask(uint8_t reg, uint8_t mask)
{
    WriteRegister(reg, ReadRegister(reg) | mask);
}

void NfcReader::ClearBitMask(uint8_t reg, uint8_t mask)
{
    WriteRegister(reg, ReadRegister(reg) & (~mask));
}

void NfcReader::AntennaOn()
{
    uint8_t value = ReadRegister(TxControlReg);
    if (!(value & 0x03))
        SetBitMask(TxControlReg, 0x03);
}

bool NfcReader::CommunicateWithCard(uint8_t command, const std::vector<uint8_t>& sendData,
    std::vector<uint8_t>& outBackData, unsigned& outBackBits)
{
    // irqEn/waitIRq bits for PCD_Transceive, per the datasheet's ComIEnReg
    // and ComIrqReg tables (0x77 enables the interrupts we care about,
    // 0x30 is RxIRq|IdleIRq -- "a full reply arrived" / "command finished").
    const uint8_t irqEn = 0x77;
    const uint8_t waitIRq = 0x30;

    WriteRegister(CommIEnReg, irqEn | 0x80);
    ClearBitMask(CommIrqReg, 0x80);
    SetBitMask(FIFOLevelReg, 0x80); // flush FIFO
    WriteRegister(CommandReg, PCD_Idle);

    for (uint8_t b : sendData)
        WriteRegister(FIFODataReg, b);

    WriteRegister(CommandReg, command);
    if (command == PCD_Transceive)
        SetBitMask(BitFramingReg, 0x80); // StartSend

    // Poll ComIrqReg for completion instead of blocking on the chip's IRQ
    // pin (unused/unwired here). A card that isn't present never raises
    // waitIRq, so this loop is what bounds a single PollForUid() call.
    uint8_t irqStatus = 0;
    int tries = 2000;
    while (tries-- > 0)
    {
        irqStatus = ReadRegister(CommIrqReg);
        if (irqStatus & (waitIRq | 0x01)) // 0x01 = TimerIRq (chip gave up)
            break;
    }

    ClearBitMask(BitFramingReg, 0x80);

    if (tries <= 0 || (irqStatus & 0x01))
        return false; // timed out, or the chip's own timer expired: no card

    // BufferOvfl | CollErr | ParityErr | ProtocolErr
    if (ReadRegister(ErrorReg) & 0x1B)
        return false;

    if (command != PCD_Transceive)
        return true;

    uint8_t fifoLevel = ReadRegister(FIFOLevelReg);
    uint8_t lastBits = ReadRegister(ControlReg) & 0x07;
    outBackBits = lastBits ? (fifoLevel - 1) * 8 + lastBits : fifoLevel * 8;

    if (fifoLevel == 0)
        fifoLevel = 1;
    if (fifoLevel > 16)
        fifoLevel = 16;

    outBackData.resize(fifoLevel);
    for (uint8_t i = 0; i < fifoLevel; i++)
        outBackData[i] = ReadRegister(FIFODataReg);

    return true;
}

bool NfcReader::RequestA()
{
    // Short frame: 7 bits, no CRC -- required for REQA by the ISO14443-3 spec.
    WriteRegister(BitFramingReg, 0x07);

    std::vector<uint8_t> backData;
    unsigned backBits = 0;
    bool ok = CommunicateWithCard(PCD_Transceive, { PICC_REQA }, backData, backBits);
    return ok && backBits == 16; // ATQA is always exactly 2 bytes
}

bool NfcReader::AnticollisionRead(std::vector<uint8_t>& outUid)
{
    WriteRegister(BitFramingReg, 0x00); // full bytes, no CRC

    std::vector<uint8_t> backData;
    unsigned backBits = 0;
    bool ok = CommunicateWithCard(PCD_Transceive, { PICC_ANTICOLL1, 0x20 }, backData, backBits);
    if (!ok || backData.size() != 5)
        return false;

    // Last byte is a checksum (XOR of the 4 UID bytes); reject a bad read
    // instead of returning a UID that might not match what's on the card.
    uint8_t checksum = backData[0] ^ backData[1] ^ backData[2] ^ backData[3];
    if (checksum != backData[4])
        return false;

    outUid.assign(backData.begin(), backData.begin() + 4);
    return true;
}

bool NfcReader::PollForUid(std::vector<uint8_t>& outUid)
{
    if (fd_ < 0)
        return false;

    if (!RequestA())
        return false;

    return AnticollisionRead(outUid);
}

#else // !__linux__

NfcReader::~NfcReader() = default;

bool NfcReader::Open(const std::string&)
{
    return false;
}

void NfcReader::Close()
{
}

bool NfcReader::PollForUid(std::vector<uint8_t>&)
{
    return false;
}

uint8_t NfcReader::ReadRegister(uint8_t) { return 0; }
void NfcReader::WriteRegister(uint8_t, uint8_t) {}
void NfcReader::SetBitMask(uint8_t, uint8_t) {}
void NfcReader::ClearBitMask(uint8_t, uint8_t) {}
void NfcReader::AntennaOn() {}
bool NfcReader::CommunicateWithCard(uint8_t, const std::vector<uint8_t>&, std::vector<uint8_t>&, unsigned&) { return false; }
bool NfcReader::RequestA() { return false; }
bool NfcReader::AnticollisionRead(std::vector<uint8_t>&) { return false; }

#endif
