#pragma once

#include <stdint.h>
#include <libftdi1/ftdi.h>

// internal helpers (see liblumax.c)

// full type comes from <ftdi.h>; the device struct only holds a pointer
struct ftdi_context;

// One open Lumax card. The public API passes this around as an opaque
// void* handle (Lumax_OpenDevice returns it, every other routine takes
// it). Heap-allocated by Lumax_OpenDevice, freed by Lumax_CloseDevice.
// dev->ftdi == NULL means "link down" (a busy-recovery failed or the
// device was just closed); all I/O is refused until it is re-opened.
struct lumax_device {
    // identity / transport
    struct ftdi_context *ftdi;   // raw FTDI context (was the handle itself)
    char     serial[16];         // USB serial of this card; re-opens use it
    int      numDev;             // registry slot in open_devices[] (1..8)
    int      recovering;         // re-entrancy guard for the busy-recovery

    // configuration re-derived from the card at every init
    uint8_t  BufferLayout;       // frame-header layout byte (1 or 5)
    uint8_t  TTLBuffer;          // global TTL value sent with every point
    uint8_t  TTLAvailable;       // bit 0: card has TTL (from readID), bit 1: set via Lumax_SetTTL
    int      Flavor;             // color-channel layout (1/2/4/8/16)
    int      MaxPoints;          // max points per frame-buffer chunk (v10[36])
    int      FrameCapacity;      // bytes per chunk buffer (v10[35] = 31500); MaxPoints = FrameCapacity/BytesPerPoint
    int      MaxScanSpeed;       // max scan speed (PPS) for this flavor (v10[51])
    uint32_t FrameDivisor;       // v10[324144] = 16000000; SendFrame frame-end divisor
    int      BytesPerPoint;      // v10[34]; bytes per point for the current flavor (7 for flavor 1)
    uint32_t WaitGate;           // v10[52] = 255; the bit-3 gate WaitForBuffer checks
    uint32_t DmxFlavorMask;      // non-zero only on Bare-Lumax cards

    // per-card frame / timing state
    uint32_t NextLoopCounts;     // chunk counter, toggles 0 <-> 8 per frame
    uint32_t TimeUntilFree;      // device-reported busy time after a frame (ms)
    uint32_t TimeOffset;         // expected ms until the next frame can be sent
    uint32_t NumberOfFramesSend; // frame counter
    uint32_t ScanSpeed;          // last requested PPS
    uint32_t LastDivisor;        // PPS of the frame currently being played
    uint32_t NumberOfPoints;     // size of the last frame

    // per-card busy tracking
    uint32_t IsBusy;             // set on a short FTDI write
    uint32_t BusyTime;           // timeGetTime() when the busy state began

    // temporary buffers for helper functions
    uint8_t  write_buffer[256];  // last written buffer (for echo simulation)
    uint32_t write_len;          // length of last written buffer

    // Additional state for DMX functions (reverse engineered)
    uint8_t  dmx_state_byte;     // corresponds to m_38[324146 * numDev - 322991 + 324143]
};

#ifndef WINDOWS
uint32_t timeGetTime(void);
#endif

// open the numDev-th connected card (1..8; cards are sorted ascending
// by their alphanumerical serial number) and store its serial in dev
int openDev(int numDev, struct lumax_device *dev);

// low-level open of the card with the given USB serial (empty = first
// vid/pid match); sets dev->ftdi
int openDevBySerial(const char *serial, struct lumax_device *dev);

// full open handshake on an already-open dev->ftdi: reset the per-card
// state, clear buffer, readID, readMemory, dongle exchange + flavor key,
// stop frame (retry once after cycling the card), DMX off, TTL off.
// Fresh opens and busy-recovery both run this, so the card always ends
// up in the same known state. 0 on full success.
int lumax_init_device(struct lumax_device *dev);

int clearBuffer(struct lumax_device *dev);

void checkIfBusy(struct lumax_device *dev, uint32_t ftStatus);

int isOpen(struct lumax_device *dev);

int sendInitKey(struct lumax_device *dev);

int sendKey(struct lumax_device *dev, int flavor);

int writeToDev(struct lumax_device *dev, uint8_t *buffer, uint32_t bytesToWrite);

int readFromDev(struct lumax_device *dev, uint8_t *buffer, uint32_t bytesToRead);

int writeFrameBuffer(struct lumax_device *dev, uint8_t *frameBuffer, uint16_t frameBufferSize, uint16_t numberOfBytes, uint8_t counter, int flag);

// Port of g4jz (liblumax.so): write [0xC9,0,0,0,0,0,0xC9], wait 50 ms, then
// read the echo byte and classify: 1 byte present = basic card (no ID, the
// 16-byte buffer is zeroed and it is a success); 18 bytes present = advanced
// card (16 data bytes + 1 data checksum, verified by XOR). 0 on success.
int readID(struct lumax_device *dev, uint8_t *arr, uint16_t size);

// Port of wp8o (liblumax.so): write [0xCA, start_lo, start_hi, len_lo,
// len_hi, 0, checksum] and read len+2 bytes (echo + data + data checksum).
// 0 on success.
int readMemory(struct lumax_device *dev, uint8_t *arr, uint16_t start, uint16_t len);

// Set the blanking delay in milliseconds. At each off->on light transition
// (laser turned on), this many ms worth of blank (laser-off) points are
// inserted at the same position. At each on->off transition, extra "on"
// points are added before the laser turns off.
// delay_ms: blanking delay in milliseconds (0 = disabled, default).
void Lumax_SetBlankingDelay(int delay_ms);

int dongleCom(struct lumax_device *dev, uint8_t flag, uint32_t address, uint8_t *writeBuffer, uint8_t *readBuffer);
