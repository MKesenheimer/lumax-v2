#pragma once

#include <stdint.h>

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
    int      MaxPoints;          // max points per frame-buffer chunk
    int      MaxScanSpeed;       // max scan speed (PPS) for this flavor
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

int readID(struct lumax_device *dev, uint8_t *arr, uint16_t size);

int readMemory(struct lumax_device *dev, uint8_t *arr, uint16_t start, uint16_t end);

// Set the blanking delay in milliseconds. At each off->on light transition
// (laser turned on), this many ms worth of blank (laser-off) points are
// inserted at the same position. At each on->off transition, extra "on"
// points are added before the laser turns off.
// delay_ms: blanking delay in milliseconds (0 = disabled, default).
void Lumax_SetBlankingDelay(int delay_ms);

int dongleCom(struct lumax_device *dev, uint8_t flag, uint32_t address, uint8_t *writeBuffer, uint8_t *readBuffer);
