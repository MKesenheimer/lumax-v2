/*
 * liblumax.c - Reverse-engineered Lumax laser controller library
 * Implements the Lumax API using FTDI USB devices.
 *
 * Based on analysis of lumax.dll and header files.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <libftdi1/ftdi.h>
#include "liblumax.h"
#include "lumax.h"

/* Millisecond clock (port of the Windows MMSYSTEM timeGetTime()). Monotonic,
 * used for the WaitForBuffer poll deadline and busy-time bookkeeping. */
#ifndef WINDOWS
uint32_t timeGetTime(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}
#endif

/* Forward declarations for helper functions */
static int U9mp(int handle);
static int nmg11(int handle, intptr_t buffer, int length);
static int bnQQ(int handle, intptr_t buffer, int length);
/* device-pointer-based I/O used by lumax_init_device (before handle is valid) */
static int dev_write(struct lumax_device *dev, const uint8_t *buf, int len);
static int dev_read(struct lumax_device *dev, uint8_t *buf, int len);
static int dev_read_available(struct lumax_device *dev);
static int fvr(int handle, intptr_t dmx_buffer, unsigned short cmd, unsigned short length, char flag);
static int vN67(int handle, char command, int address, intptr_t write_buf, intptr_t read_buf);
static int Tj4(int handle, intptr_t buffer_ptr, unsigned short a3, unsigned short a4, char a5, int a6, int a7);

/*
 * Debug tracing, enabled by setting LUMAX_DEBUG in the environment
 * (e.g. LUMAX_DEBUG=1 ./test). Silent by default.
 */
static int lumax_debug_enabled(void)
{
    static int enabled = -1;
    if (enabled < 0)
        enabled = (getenv("LUMAX_DEBUG") != NULL) ? 1 : 0;
    return enabled;
}
#define DBG(fmt, ...) do { if (lumax_debug_enabled()) fprintf(stderr, "[lumax] " fmt "\n", ##__VA_ARGS__); } while (0)

/* Maximum number of devices we can handle */
#define MAX_DEVICES 8

/* Global array of open devices */
static struct lumax_device open_devices[MAX_DEVICES];
static int num_open_devices = 0;

/* Global array of detected devices */
static struct lumax_device detected_devices[MAX_DEVICES];
static int num_detected_devices = 0;

/*
 * Fallback per-card configuration, applied by ghrkr().
 *
 * In liblumax.so these are the defaults ghrkr() writes into each of the 8
 * device slots of the m_38[] backing array:
 *   field 1191 = 31500 / 7 = 4500  (points per frame-buffer chunk -> MaxPoints)
 *   field 1206 = 45000             (max scan speed PPS            -> MaxScanSpeed)
 *   field 1204 = 1                 (frame-header layout byte      -> BufferLayout)
 *
 * They are pure fallbacks: lumax_init_device() re-derives the real values from
 * the card's memory (256 * mem[5], 100 * mem[6]) and those override the
 * defaults when non-zero. The fallbacks exist so SendFrame()'s arithmetic
 * (it divides by MaxPoints and clamps to MaxScanSpeed) stays valid even for a
 * slot whose memory read yielded 0. The scan-speed preset tables that ghrkr
 * also fills in the .so are not modelled here because this port's
 * writeFrameBuffer() is a raw ftdi_write_data that never consults them.
 */
#define GHKR_FALLBACK_MAX_POINTS   4500
#define GHKR_FALLBACK_MAX_SCANSPEED 45000
#define GHKR_FALLBACK_BUFFER_LAYOUT 1
#define GHKR_FALLBACK_FLAVOR       1

/* Fill any unset (0) config field from the ghrkr() fallback values.
 *
 * Mirrors the per-slot defaults ghrkr() writes into m_38[] in liblumax.so:
 *   slot[34] = 7         -> BytesPerPoint
 *   slot[35] = 31500     -> FrameCapacity
 *   slot[36] = 31500/7   -> MaxPoints
 *   slot[49] = 1         -> Flavor
 *   slot[51] = 45000     -> MaxScanSpeed (HJdd later raises this to 70000 for flavor 1)
 *   slot[52] = 255       -> WaitGate
 *   slot[324144] = 16000000 -> FrameDivisor
 * Fields the reference does not pre-fill (BufferLayout) keep the port's
 * BufferLayout fallback. Only 0-valued fields are touched, so real card
 * values (applied later by HJdd/lumax_init_device) are never clobbered. */
static void apply_fallback_config(struct lumax_device *dev)
{
    if (!dev)
        return;
    if (dev->FrameCapacity == 0)
        dev->FrameCapacity = GHKR_FALLBACK_MAX_POINTS * 7; /* 31500 */
    if (dev->BytesPerPoint == 0)
        dev->BytesPerPoint = 7;
    if (dev->MaxPoints == 0)
        dev->MaxPoints = GHKR_FALLBACK_MAX_POINTS;
    if (dev->MaxScanSpeed == 0)
        dev->MaxScanSpeed = GHKR_FALLBACK_MAX_SCANSPEED;
    if (dev->BufferLayout == 0)
        dev->BufferLayout = GHKR_FALLBACK_BUFFER_LAYOUT;
    if (dev->Flavor == 0)
        dev->Flavor = GHKR_FALLBACK_FLAVOR;
    if (dev->WaitGate == 0)
        dev->WaitGate = 255;
    if (dev->FrameDivisor == 0)
        dev->FrameDivisor = 16000000;
}

/*
 * One-time lazy global constructor (analogous to the .so's Constructor_State
 * guard). Pre-sets every device slot's fallback configuration exactly once.
 *
 * The .so version mallocs the 10.6 MB m_38[] array and initialises all 8 slots
 * (chunk size, max scan speed, buffer layout, and the per-flavor scan-speed
 * preset tables). This port keeps its device records in the static
 * open_devices[]/detected_devices[] arrays (already zero-initialised, no
 * malloc) and does not model the preset tables, so the faithful equivalent is:
 * set the config fallbacks on the open-device slots. The guard makes it safe to
 * call from any entry point; it is a no-op after the first run.
 */
static void ghrkr(void)
{
    static int initialized = 0;
    int i;

    if (initialized)
        return;

    for (i = 0; i < MAX_DEVICES; i++)
        apply_fallback_config(&open_devices[i]);

    initialized = 1;
}

/* Helper function to find device by serial number */
static struct lumax_device* find_device_by_serial(const char *serial)
{
    int i;
    for (i = 0; i < num_detected_devices; i++) {
        if (strcmp(detected_devices[i].serial, serial) == 0) {
            return &detected_devices[i];
        }
    }
    return NULL;
}

/* Helper function to find open device by handle */
static struct lumax_device* find_open_device_by_handle(int handle)
{
    if (handle < 1 || handle > num_open_devices)
        return NULL;
    return &open_devices[handle-1];
}

/* Helper function to find device by index (1-based) */
static struct lumax_device* find_device_by_index(int index)
{
    if (index < 1 || index > num_open_devices) {
        return NULL;
    }
    return &open_devices[index-1];
}

/* Exported API Functions */

int Lumax_GetApiVersion(void)
{
    /* From the VB example, the version as of Feb 2008 is 207 */
    return 207;
}

int Lumax_GetPhysicalDevices(void)
{
    struct ftdi_context *ftdi;
    struct ftdi_device_list *devlist, *curdev;
    int ret, i;
    char serial[64];

    /* Initialize */
    ghrkr();

    /* Initialize ftdi context */
    ftdi = ftdi_new();
    if (!ftdi) {
        return 0;
    }

    /* Find all Lumax devices (VID=0x403, PID=0xC88A) */
    ret = ftdi_usb_find_all(ftdi, &devlist, 0x403, 0xC88A);
    if (ret < 0) {
        ftdi_free(ftdi);
        return 0;
    }

    /* Reset detected devices count */
    num_detected_devices = 0;

    /* Iterate through the device list */
    i = 0;
    for (curdev = devlist; curdev != NULL && num_detected_devices < MAX_DEVICES; curdev = curdev->next) {
        /* Get the serial number of the device */
        if (ftdi_usb_get_strings(ftdi, curdev->dev, NULL, 0, NULL, 0, serial, sizeof(serial)) < 0) {
            continue;
        }

        /* We assume all devices with the set VID/PID are Lumax devices */
        /* Copy serial number */
        strncpy(detected_devices[num_detected_devices].serial,
                serial,
                sizeof(detected_devices[num_detected_devices].serial) - 1);
        detected_devices[num_detected_devices].serial[sizeof(detected_devices[num_detected_devices].serial) - 1] = '\0';

        /* Initialize the device structure */
        detected_devices[num_detected_devices].ftdi = NULL;
        detected_devices[num_detected_devices].numDev = num_detected_devices + 1; /* 1-based index */
        detected_devices[num_detected_devices].recovering = 0;
        /* Other fields will be initialized during open/init */
        detected_devices[num_detected_devices].BufferLayout = 0;
        detected_devices[num_detected_devices].TTLBuffer = 0;
        detected_devices[num_detected_devices].TTLAvailable = 0;
        detected_devices[num_detected_devices].Flavor = 0;
        detected_devices[num_detected_devices].MaxPoints = 0;
        detected_devices[num_detected_devices].MaxScanSpeed = 0;
        detected_devices[num_detected_devices].DmxFlavorMask = 0;
        detected_devices[num_detected_devices].dmx_state_byte = 0;
        detected_devices[num_detected_devices].NextLoopCounts = 0;
        detected_devices[num_detected_devices].TimeUntilFree = 0;
        detected_devices[num_detected_devices].TimeOffset = 0;
        detected_devices[num_detected_devices].NumberOfFramesSend = 0;
        detected_devices[num_detected_devices].ScanSpeed = 0;
        detected_devices[num_detected_devices].LastDivisor = 0;
        detected_devices[num_detected_devices].NumberOfPoints = 0;
        detected_devices[num_detected_devices].IsBusy = 0;
        detected_devices[num_detected_devices].BusyTime = 0;

        num_detected_devices++;
    }

    /* Free the device list */
    ftdi_list_free(&devlist);
    /* Free the ftdi context */
    ftdi_free(ftdi);

    return num_detected_devices;
}

int Lumax_GetDeviceInfo(int PhysicalDevice)
{
    /* Reserved for future developments */
    (void)PhysicalDevice; /* Suppress unused parameter warning */
    return 0;
}

int Lumax_OpenDevice(int PhysicalDevice, int Channel)
{
    /* One-time global init (idempotent; matches the .so, which calls ghrkr()
     * from both GetPhysicalDevices and OpenDevice). */
    ghrkr();

    /* Check if PhysicalDevice is valid */
    if (PhysicalDevice < 1 || PhysicalDevice > num_detected_devices)
        return 0;

    /* Check if device is already open (we could allow reopening, but we'll prevent double open) */
    /* We can check if the device is already in open_devices by serial number */
    struct lumax_device* dev = &detected_devices[PhysicalDevice-1];
    int i;
    for (i = 0; i < num_open_devices; i++) {
        if (strcmp(open_devices[i].serial, dev->serial) == 0) {
            /* Already open */
            return 0;
        }
    }

    /* If we have too many open devices, refuse */
    if (num_open_devices >= MAX_DEVICES)
        return 0;

    /* Open the device using libftdi1 */
    struct ftdi_context *ftdi = ftdi_new();
    if (!ftdi) {
        DBG("ftdi_new failed");
        return 0;
    }

    /* Open device by VID/PID and serial number */
    int open_rc = ftdi_usb_open_desc(ftdi, 0x403, 0xC88A, NULL, dev->serial);
    DBG("ftdi_usb_open_desc(serial='%s') = %d", dev->serial, open_rc);
    if (open_rc < 0) {
        ftdi_free(ftdi);
        return 0;
    }

    /* Reference setup right after FT_OpenEx: FT_SetTimeouts(h,1000,1000) and
     * FT_SetLatencyTimer(h,2). libftdi1 exposes the same knobs as context
     * fields plus a setter. */
    ftdi->usb_read_timeout = 1000;
    ftdi->usb_write_timeout = 1000;
    ftdi_set_latency_timer(ftdi, 2);

    /* Register the device slot NOW (before the init handshake) so the handle
     * is valid for the whole init. The reference stores the FTDI handle in the
     * slot before P5555/g4jz/... so every nmg11/bnQQ resolves it; the final
     * gate (StopFrame/SetDmxMode/SetTTL) are public handle-keyed functions and
     * need the slot present too. On any init failure we roll this back. */
    struct lumax_device *openDev = &open_devices[num_open_devices];
    memset(openDev, 0, sizeof(*openDev));
    memcpy(openDev->serial, dev->serial, sizeof(openDev->serial));
    openDev->ftdi = ftdi;
    openDev->numDev = num_open_devices + 1;   /* 1-based handle */
    openDev->recovering = 0;

    /* Per-card config, mirroring the reference's slot writes. The reset
     * (P5555) and the HJdd/StopFrame/SetDmxMode/SetTTL gate all run against
     * these; HJdd (in lumax_init_device) then finalizes BytesPerPoint /
     * MaxPoints / MaxScanSpeed for the detected flavor. */
    openDev->BufferLayout     = GHKR_FALLBACK_BUFFER_LAYOUT; /* slot[1204]=1 */
    openDev->TTLBuffer        = 0;                           /* v10[32]    */
    openDev->TTLAvailable     = 0;                           /* v10[31]    */
    openDev->Flavor           = 1;                           /* v10[49]    */
    openDev->FrameCapacity    = GHKR_FALLBACK_MAX_POINTS * 7;/* v10[35]=31500 */
    openDev->MaxPoints        = GHKR_FALLBACK_MAX_POINTS;    /* v10[36] (HJdd overrides) */
    openDev->MaxScanSpeed     = GHKR_FALLBACK_MAX_SCANSPEED; /* v10[51] (HJdd -> 70000)  */
    openDev->BytesPerPoint    = 7;                           /* v10[34] (HJdd overrides) */
    openDev->WaitGate         = 255;                         /* v10[52]    */
    openDev->DmxFlavorMask    = 0;                           /* v10[50]    */
    openDev->FrameDivisor     = 16000000;                    /* v10[324144]*/
    openDev->dmx_state_byte   = 1;                           /* v10[324143]*/
    openDev->NextLoopCounts   = 0;                           /* v10[42]    */
    openDev->ScanSpeed        = 1000;                        /* v10[46]    */
    openDev->LastDivisor      = 1000;                        /* v10[45]    */
    openDev->NumberOfPoints   = 0;                           /* v10[47]    */
    openDev->TimeUntilFree    = 0;
    openDev->TimeOffset       = 0;
    openDev->NumberOfFramesSend = 0;
    openDev->IsBusy           = 0;
    openDev->BusyTime         = 0;
    num_open_devices++;
    int handle = openDev->numDev;

    /* Full open handshake: reset (P5555), readID (g4jz), [advanced-card
     * config], HJdd flavor setup, then the reference's final gate — StopFrame,
     * DMX off, TTL off — all driven through the now-valid handle. */
    if (lumax_init_device(openDev) != 0) {
        DBG("lumax_init_device failed");
        ftdi_usb_close(ftdi);
        ftdi_free(ftdi);
        openDev->ftdi = NULL;
        num_open_devices--;   /* roll back the slot registration */
        return 0;
    }

    DBG("Lumax_OpenDevice: handle %d (serial %s, flavor %d, MaxPoints %d, BytesPerPoint %d, MaxScanSpeed %d)",
        handle, openDev->serial, openDev->Flavor, openDev->MaxPoints,
        openDev->BytesPerPoint, openDev->MaxScanSpeed);
    return handle;
}

int Lumax_CloseDevice(int Handle)
{
    struct lumax_device *dev = find_open_device_by_handle(Handle);
    if (!dev)
        return 1; /* invalid handle (reference: U9mp -> return 1) */

    int result;
    if (dev->ftdi) {
        /* reference: FT_Close(handle, 1) purges the RX buffer on close */
        ftdi_usb_purge_buffers(dev->ftdi);
        int rc = ftdi_usb_close(dev->ftdi);
        ftdi_free(dev->ftdi);
        dev->ftdi = NULL;
        result = (rc == 0) ? 0 : 1; /* FT_Close returns 0 on success */
    } else {
        return 1; /* no open link (reference: v4[1]==0 -> return 1) */
    }

    /* Remove from open_devices array */
    int index = Handle - 1;
    if (index >= 0 && index < num_open_devices) {
        int i;
        for (i = index; i < num_open_devices - 1; i++)
            open_devices[i] = open_devices[i + 1];
        num_open_devices--;
    }
    return result;
}

int Lumax_SendFrame(int Handle, TLumax_Point *Points, int NumOfPoints, int ScanSpeed, int UpdateMode, int *TimeToWait)
{
    (void)UpdateMode; /* the reference ignores this argument */

    /* NumOfPoints == 0 -> a single "laser off at center" point at the
       default 1000 PPS (this is what Lumax_StopFrame relies on). */
    if (NumOfPoints == 0) {
        static TLumax_Point def_point;
        memset(&def_point, 0, sizeof(def_point));
        def_point.Ch1 = 0x8000; /* center X */
        def_point.Ch2 = 0x8000; /* center Y */
        Points = &def_point;
        NumOfPoints = 1;
        ScanSpeed = 1000;
    }

    struct lumax_device *dev = find_open_device_by_handle(Handle);
    if (!dev || !dev->ftdi)
        return 1;

    int max_points = dev->MaxPoints;
    if (NumOfPoints <= 0 || 8 * max_points < NumOfPoints)
        return 1;

    /* Clamp scan speed to [250, MaxScanSpeed] */
    int pps = ScanSpeed;
    if (pps <= 249)
        pps = 250;
    if (pps > dev->MaxScanSpeed)
        pps = dev->MaxScanSpeed;

    /* Per-frame state (reference: v36[37..43]) */
    dev->NumberOfFramesSend++;
    dev->NumberOfPoints = NumOfPoints;
    dev->ScanSpeed = pps;
    dev->TimeOffset = 0;

    /* Chunk layout: the FIRST chunk carries the remainder, the rest carry
       MaxPoints. total_chunks = ceil(NumOfPoints / MaxPoints). */
    int total_chunks = NumOfPoints / max_points;
    int remainder = NumOfPoints % max_points;
    if (remainder)
        total_chunks++;

    int flavor = dev->Flavor;
    int point_index = 0;

    /* One packed chunk is at most MaxPoints * BytesPerPoint == FrameCapacity
       bytes; a little slack covers any rounding. */
    unsigned char *chunk = malloc(dev->MaxPoints * dev->BytesPerPoint + 16);
    if (!chunk)
        return 1;

    int tj_err = 0;
    for (int c = 0; c < total_chunks; c++) {
        int n_points = (c == 0) ? remainder : max_points;
        int bp = 0;

        for (int i = 0; i < n_points; i++) {
            TLumax_Point *p = &Points[point_index + i];
            unsigned int x = (p->Ch1 >> 4) & 0xFFF; /* 12-bit X */
            unsigned int y = (p->Ch2 >> 4) & 0xFFF; /* 12-bit Y */

            /* common 6-byte prefix */
            chunk[bp++] = (unsigned char)((x >> 8) + 16 * (y >> 8));
            chunk[bp++] = (unsigned char)(x & 0xFF);
            chunk[bp++] = (unsigned char)(y & 0xFF);
            chunk[bp++] = (unsigned char)(p->Ch3 >> 8); /* R hi */
            chunk[bp++] = (unsigned char)(p->Ch4 >> 8); /* G hi */
            chunk[bp++] = (unsigned char)(p->Ch5 >> 8); /* B hi */

            /* flavor-specific suffix */
            switch (flavor) {
            case 1: /* 7 bytes: +TTL */
                chunk[bp++] = (dev->TTLAvailable & 2) ? dev->TTLBuffer
                                                      : (unsigned char)(p->TTL & 0xFF);
                break;
            case 2: /* 6 bytes: no suffix */
                break;
            case 4: /* 7 bytes: +Ch8 */
                chunk[bp++] = (unsigned char)(p->Ch8 >> 8);
                break;
            case 8: /* 8 bytes: +Ch8 +Ch6 */
                chunk[bp++] = (unsigned char)(p->Ch8 >> 8);
                chunk[bp++] = (unsigned char)(p->Ch6 >> 8);
                break;
            case 16: /* 9 bytes: +Ch8 +Ch6 +Ch7 */
                chunk[bp++] = (unsigned char)(p->Ch8 >> 8);
                chunk[bp++] = (unsigned char)(p->Ch6 >> 8);
                chunk[bp++] = (unsigned char)(p->Ch7 >> 8);
                break;
            default: /* unknown flavor: fall back to the 7-byte flavor-1 tail */
                chunk[bp++] = (unsigned char)(p->TTL & 0xFF);
                break;
            }
        }
        point_index += n_points;

        DBG("SendFrame chunk %d: %d points, %d bytes, counter %d",
            c, n_points, bp, dev->NextLoopCounts + c);

        /* a3=0x8000 marks a frame-buffer write; a4=packed size; a5=chunk
           counter; a6=0 -> Tj4 reads back 2 bytes [checksum, 0x55]. */
        int r = Tj4(Handle, (intptr_t)chunk, 0x8000, (unsigned short)bp,
                    (char)(dev->NextLoopCounts + c), 0, 0);
        if (r) {
            tj_err = r;
            break;
        }
    }
    free(chunk);
    if (tj_err)
        return 1;

    /* Frame-end command (7 bytes): [3, rem_lo, rem_hi, div_lo, div_hi,
       final_counter, checksum]; divisor = FrameDivisor / scan_speed. */
    int divisor = dev->FrameDivisor / pps;
    int final_counter = dev->NextLoopCounts + 16 * (total_chunks - 1);
    unsigned char cmd[7];
    cmd[0] = 3;
    cmd[1] = (unsigned char)(remainder & 0xFF);
    cmd[2] = (unsigned char)((remainder >> 8) & 0xFF);
    cmd[3] = (unsigned char)(divisor & 0xFF);
    cmd[4] = (unsigned char)((divisor >> 8) & 0xFF);
    cmd[5] = (unsigned char)final_counter;
    cmd[6] = (unsigned char)(cmd[0] ^ cmd[1] ^ cmd[2] ^ cmd[3] ^ cmd[4] ^ cmd[5]);

    DBG("SendFrame frame-end: [3, rem=%d, div=%d, counter=%d, cks=0x%02x]",
        remainder, divisor, final_counter, cmd[6]);

    int ret = 1;
    if (nmg11(Handle, (intptr_t)cmd, 7) == 7) {
        unsigned char reply[4];
        int rlen = bnQQ(Handle, (intptr_t)reply, 4);
        DBG("SendFrame frame-end reply (%d bytes): %02x %02x %02x %02x",
            rlen,
            rlen > 0 ? reply[0] : 0, rlen > 1 ? reply[1] : 0,
            rlen > 2 ? reply[2] : 0, rlen > 3 ? reply[3] : 0);
        if (rlen == 4 && reply[0] == cmd[6]) {
            if (reply[3] & 0x80) {
                unsigned int tuf = reply[1]
                    + max_points * ((reply[3] & 0x7F) - 1)
                    + ((unsigned int)reply[2] << 8);
                dev->TimeUntilFree = 1000 * tuf / pps;
            } else {
                dev->TimeUntilFree = 0;
            }
            dev->TimeOffset = 1000 * NumOfPoints / pps;
            dev->LastDivisor = pps; /* v36[45] = v36[46] */
            if (TimeToWait)
                *TimeToWait = dev->TimeUntilFree;
            ret = 0;
        } else {
            DBG("SendFrame frame-end reply mismatch: %02x %02x %02x %02x (want cks 0x%02x)",
                reply[0], reply[1], reply[2], reply[3], cmd[6]);
        }
    } else {
        DBG("SendFrame frame-end write failed");
    }

    /* Toggle the chunk counter 0 <-> 8 between frames. */
    dev->NextLoopCounts = (dev->NextLoopCounts != 0) ? 0 : 8;
    return ret;
}

int Lumax_WaitForBuffer(int Handle, int Timeout, int *TimeToWait, int *BufferChanged)
{
    struct lumax_device* dev = find_open_device_by_handle(Handle);
    if (!dev)
        return 1; /* Device not valid */

    /* Gate (reference v25[52] & 8): WaitGate is 255 for a live card, so bit 3
       is set and we query the card. A closed/zeroed slot would return 0 here. */
    if ((dev->WaitGate & 8) == 0)
        return 0;

    unsigned char cmd_buf[7];
    unsigned char response_buf[10]; /* Enough for max response */
    int written, read_len;

    if (Timeout || !TimeToWait || !BufferChanged) {
        /* Timeout mode or invalid output pointers.
         *
         * The reference (liblumax.so) issues the query once and fails if the
         * card is not already free. That is fragile for the rapid
         * open/stop/reopen case: a card left playing a frame is busy for one
         * frame pass (~points/pps) and only then reports [05 01]. The
         * `Timeout` argument is named a timeout, so we honor it: poll the
         * single-shot query until the card reports free or `Timeout` ms have
         * elapsed. An idle card still answers on the first poll, so this is a
         * no-op in the common case. */
        cmd_buf[0] = 0x04;
        cmd_buf[1] = 0x01;
        cmd_buf[2] = 0x00;
        cmd_buf[3] = 0x00;
        cmd_buf[4] = 0x00;
        cmd_buf[5] = 0x00;
        cmd_buf[6] = 0x05;

        int poll_deadline = Timeout > 0 ? Timeout : 2000;
        int poll_start = timeGetTime();
        for (;;) {
            /* Clean slate before each query: drop any stale/queued bytes so
               this read is only the reply to the query we are about to send
               (a short read otherwise leaves a byte behind and the next poll
               reads a misaligned pair such as 01 05). */
            ftdi_usb_purge_buffers(dev->ftdi);
            written = nmg11(Handle, (intptr_t)cmd_buf, 7);
            if (written != 7)
                return 1; /* Failed to write */
            usleep(30000); /* ~30 ms for the card to process and reply */

            read_len = bnQQ(Handle, (intptr_t)response_buf, 2);
            DBG("WaitForBuffer(timeout) read %d bytes: %02x %02x (want 05 01)",
                read_len,
                read_len > 0 ? response_buf[0] : 0,
                read_len > 1 ? response_buf[1] : 0);

            if (read_len == 2 && response_buf[0] == 0x05 && response_buf[1] == 0x01) {
                /* Success: buffer free */
                if (BufferChanged)
                    *BufferChanged = 1;
                return 0;
            }

            /* Not free yet (or short read): retry until the timeout elapses. */
            if (timeGetTime() - poll_start >= (unsigned int)poll_deadline)
                break;
            usleep(20000); /* 20 ms between polls */
        }

        /* Timed out without the card reporting free. */
        if (BufferChanged)
            *BufferChanged = 0;
        /* Set a flag in write_buffer to indicate wait failure */
        dev->write_buffer[0] = 1;
        return 1;
    } else {
        /* Normal mode: wait for buffer and return timing info */
        cmd_buf[0] = 0x04;
        cmd_buf[1] = 0x00;
        cmd_buf[2] = 0x00;
        cmd_buf[3] = 0x00;
        cmd_buf[4] = 0x00;
        cmd_buf[5] = 0x00;
        cmd_buf[6] = 0x04;

        written = nmg11(Handle, (intptr_t)cmd_buf, 7);
        if (written != 7)
            return 1; /* Failed to write */

        read_len = bnQQ(Handle, (intptr_t)response_buf, 4);
        if (read_len != 4)
            return 1; /* Failed to read enough */

        if (response_buf[0] != 0x04)
            return 1; /* Unexpected response */

        /* Extract BufferChanged flag (bit 7 of byte 3) */
        if (BufferChanged)
            *BufferChanged = (response_buf[3] & 0x80) == 0;

        /* Extract timing values */
        uint8_t byte1 = response_buf[1];
        uint8_t byte2 = response_buf[2];
        uint8_t val = response_buf[3] & 0x7F; /* Lower 7 bits of byte 3 */

        /* Calculate time value */
        uint32_t temp = byte1 + (dev->MaxPoints * (val - 1)) + ((uint32_t)byte2 << 8);

        /* Select divisor based on BufferChanged flag */
        uint32_t divisor;
        if (*BufferChanged) {
            /* Use ScanSpeed when buffer changed */
            divisor = dev->ScanSpeed;
        } else {
            /* Use LastDivisor when buffer not changed */
            divisor = dev->LastDivisor;
        }

        /* Avoid division by zero */
        if (divisor == 0)
            divisor = 1; /* Default to 1 to prevent crash */

        /* Calculate TimeToWait in milliseconds */
        if (TimeToWait)
            *TimeToWait = (1000 * temp) / divisor;

        return 0; /* Success */
    }
}

int Lumax_StopFrame(int Handle)
{
    /* Reference: a "stop" is just a single blank point at center plus a
       2-second buffer wait, so the laser goes dark and the card settles. */
    int r = Lumax_SendFrame(Handle, NULL, 0, 0, 0, NULL);
    DBG("StopFrame: SendFrame(0) = %d", r);
    if (r)
        return r;
    r = Lumax_WaitForBuffer(Handle, 2000, NULL, NULL);
    DBG("StopFrame: WaitForBuffer(2000) = %d", r);
    return r;
}

int Lumax_SetTTL(int Handle, int TTL)
{
    /* Set TTL outputs */
    uint8_t cmd_buf[7];
    uint8_t response_buf[1];
    uint8_t expected_response;

    /* Check if device handle is valid */
    if (U9mp(Handle))
        return 1; /* Device not valid */

    /* Prepare command: [0x1E, TTL, 0, 0, 0, 0, TTL^0x1E] */
    cmd_buf[0] = 0x1E;                    /* command code for Set TTL */
    cmd_buf[1] = (uint8_t)TTL;            /* TTL value to set */
    cmd_buf[2] = 0;
    cmd_buf[3] = 0;
    cmd_buf[4] = 0;
    cmd_buf[5] = 0;
    cmd_buf[6] = (uint8_t)(TTL ^ 0x1E);   /* trailing checksum */

    /* Send the command to the device */
    if (nmg11(Handle, (intptr_t)cmd_buf, 7) != 7)
        return 1; /* Failed to write */

    /* Read back response to verify */
    if (bnQQ(Handle, (intptr_t)response_buf, 1) != 1)
        return 1; /* Failed to read */

    /* Calculate expected response (TTL XOR 0x1E) */
    expected_response = (uint8_t)TTL ^ 0x1E;

    /* Verify response matches expected value */
    if (response_buf[0] != expected_response)
        return 1; /* Response mismatch */

    return 0; /* Success */
}

int Lumax_SetDmxMode(int Handle, int NumOfTxChannels, int NumOfRxChannels)
{
    struct lumax_device* dev;
    unsigned char cmd_buf[7];
    unsigned char resp_buf[1];
    int written, read_len;
    int tx_flag_zero = 0;
    int rx_flag_zero = 0;
    int tmp;

    /* Check if device handle is valid */
    if (U9mp(Handle))
        return 1; /* Device not valid */

    dev = find_open_device_by_handle(Handle);
    if (!dev)
        return 1; /* Device not found */

    /* Process TxChannels */
    if (NumOfTxChannels < 0 || NumOfTxChannels > 1024)
        return 1; /* Invalid TxChannels */

    if (NumOfTxChannels == 0) {
        NumOfTxChannels = 1;
        tx_flag_zero = 1;
    }

    /* Build command for TxChannels: [0x0A, TxCh+1, 0x0C, 0xFB, checksum_low, checksum_high?] */
    /* Actually, from analysis: 7 bytes: */
    /*   0: 0x0A */
    /*   1: (TxCh+1) & 0xFF */
    /*   2: ((TxCh+1) >> 8) & 0xFF */
    /*   3: 0x0C */
    /*   4: 0xFB */
    /*   5: 16 * tx_flag_zero */
    /*   6: checksum = byte2 ^ (TxCh+1) ^ 0x0A ^ 0xF7 ^ byte5 */
    cmd_buf[0] = 0x0A;
    cmd_buf[1] = (NumOfTxChannels + 1) & 0xFF;
    cmd_buf[2] = ((NumOfTxChannels + 1) >> 8) & 0xFF;
    cmd_buf[3] = 0x0C;
    cmd_buf[4] = 0xFB;
    cmd_buf[5] = tx_flag_zero * 16; /* 0 or 16 */
    cmd_buf[6] = cmd_buf[2] ^ (NumOfTxChannels + 1) ^ 0x0A ^ 0xF7 ^ cmd_buf[5];

    /* Send command */
    written = nmg11(Handle, (intptr_t)cmd_buf, 7);
    if (written != 7)
        return 1; /* Failed to write */

    /* Read response */
    read_len = bnQQ(Handle, (intptr_t)resp_buf, 1);
    if (read_len != 1)
        return 1; /* Failed to read */

    /* Verify response */
    if (resp_buf[0] != cmd_buf[6])
        return 1; /* Response mismatch */

    /* Process RxChannels */
    if (NumOfRxChannels < 0 || NumOfRxChannels > 1024)
        return 1; /* Invalid RxChannels */

    if (NumOfRxChannels == 0) {
        NumOfRxChannels = 1;
        rx_flag_zero = 1;
    }

    /* Build command for RxChannels: [0x0B, RxCh+1, 0x0C, 0xFB, checksum_low, checksum_high?] */
    cmd_buf[0] = 0x0B;
    cmd_buf[1] = (NumOfRxChannels + 1) & 0xFF;
    cmd_buf[2] = ((NumOfRxChannels + 1) >> 8) & 0xFF;
    cmd_buf[3] = 0x0C;
    cmd_buf[4] = 0xFB;
    cmd_buf[5] = rx_flag_zero * 16 + 1; /* 1 or 17 */
    cmd_buf[6] = cmd_buf[2] ^ (NumOfRxChannels + 1) ^ 0x0B ^ 0xF7 ^ cmd_buf[5];

    /* Send command */
    written = nmg11(Handle, (intptr_t)cmd_buf, 7);
    if (written != 7)
        return 1; /* Failed to write */

    /* Read response */
    read_len = bnQQ(Handle, (intptr_t)resp_buf, 1);
    if (read_len != 1)
        return 1; /* Failed to read */

    /* Verify response */
    if (resp_buf[0] != cmd_buf[6])
        return 1; /* Response mismatch */

    return 0; /* Success */
}


int Lumax_ReceiveDmx(int Handle, unsigned char *DmxBuffer, int Length)
{
    /* Receive a DMX frame from the card. Port of fvr/Lumax_ReceiveDmx
       (liblumax.so) — a 7-byte header [2, cmd(0xFB0C), len, flag, checksum]
       is sent, the card echoes the header checksum, streams the Length data
       bytes, and finally a data checksum (XOR of all data bytes) is checked. */
    if (Length <= 0 || Length > 1025 || U9mp(Handle))
        return 1;
    return fvr(Handle, (intptr_t)DmxBuffer, 0xFB0C, (unsigned short)Length, 1);
}

/* Dongle and exchange functions (not in VB API but exported) */

/* ---- Dongle (XTEA copy-protection) layer ----
 *
 * The dongle is addressed over the 24-byte XTEA packet (vN67). There are two
 * levels in the reference binaries:
 *
 *   .so : vN67 (24-byte transaction)  <- Lumax_DongleCom  (raw command set)
 *   .dll: sub_406068 (24-byte txn)    <- sub_4061D4 (raw) <- Lumax_Dongle_XTEA_Com (1-9)
 *
 * We port both levels. dongleRawCom() mirrors the raw command layer
 * (Lumax_DongleCom / sub_4061D4): it runs one vN67 transaction for a raw
 * dongle command and interprets the response's status byte (byte 12 of the
 * 13-byte payload). The public entry points wrap it.
 *
 * Data buffers are arrays of 32-bit words. The 32-bit reference typed them as
 * int*; the public header types them as unsigned long*. On this 64-bit
 * platform the words are therefore accessed at a fixed 4-byte stride (matching
 * the original memory layout) rather than sizeof(unsigned long).
 */
#define DONGLE_DEFAULT_ADDR 1238675770u /* fixed dongle base used by cmds 1/43/85/90 */

static uint32_t dw_get(const unsigned char *p, int idx)
{
    uint32_t v;
    memcpy(&v, p + 4 * idx, 4);
    return v;
}
static void dw_set(unsigned char *p, int idx, uint32_t v)
{
    memcpy(p + 4 * idx, &v, 4);
}

/* Run one raw dongle command. in = 16-byte write payload (may be NULL);
   data_out = response-word buffer, 4-byte stride (may be NULL). Returns 0 on
   success or a 2xx dongle error code, 1 on transport/unknown failure.
   Mirrors Lumax_DongleCom (liblumax.so) / sub_4061D4 (lumax.dll). */
static int dongleRawCom(int handle, int cmd, int address,
                        const unsigned char *in, unsigned char *data_out)
{
    unsigned char write16[16] = {0};
    unsigned char resp[13];
    uint32_t status, resp0;
    int addr;

    /* Original precondition: cmd 90 requires a caller 16-byte write payload. */
    if (cmd == 90 && !in)
        return 1;

    /* Per-command 16-byte write payload (mirrors the original's per-command
       stack buffer construction; unused bytes stay zero). */
    switch (cmd) {
    case 90:
    case 132:
        if (in) memcpy(write16, in, 16);
        break;
    case 133:
    case 134:
        if (in) memcpy(write16, in, 8); /* two caller words */
        break;
    case 130:
        if (in) write16[4] = (unsigned char)((4 * in[0]) & 0xC);
        break;
    case 131:
        if (in) write16[4] = (unsigned char)((4 * in[0]) & 0xC);
        if (data_out) memcpy(write16, data_out, 4); /* v6 = *a5 (first word) */
        break;
    case 43:
        if (in) write16[0] = in[0];
        break;
    case 1:
    case 85:
    default:
        break;
    }

    /* Commands 1, 43, 85 and 90 always target the default dongle address. */
    addr = (cmd == 1 || cmd == 43 || cmd == 85 || cmd == 90)
              ? (int)DONGLE_DEFAULT_ADDR
              : address;

    if (vN67(handle, (char)cmd, addr, (intptr_t)write16, (intptr_t)resp) != 0)
        return 1;

    status = resp[12];
    resp0  = resp[0];

    if (status == 0) {
        switch (cmd) {
        case 1:
            if (data_out) dw_set(data_out, 0, (uint32_t)resp[0]);
            return 0;
        case 85:
            /* original: resp byte 1 == 0xFF is a distinct 201 condition */
            if (resp[1] == 0xFF) return 201;
            return (resp0 == 0) ? 0 : 206;
        case 43:
        case 90:
        case 131:
        case 132:
            return (resp0 == 0) ? 0 : 206;
        case 129:
            if (data_out) {
                dw_set(data_out, 0, dw_get(resp, 0));
                dw_set(data_out, 1, dw_get(resp, 4));
                dw_set(data_out, 2, dw_get(resp, 8));
            }
            return 0;
        case 133:
        case 134:
            if (data_out) {
                dw_set(data_out, 0, dw_get(resp, 0));
                dw_set(data_out, 1, dw_get(resp, 4));
            }
            return 0;
        case 130:
            if (data_out) dw_set(data_out, 0, dw_get(resp, 0));
            return 0;
        }
        return 1;
    }

    if (status == 1)
        return 205;

    if (status == 2) {
        if (cmd == 90) {
            if (resp0 == 0xFF) return 201;
            if (resp0 == 2)    return 203;
            if (resp0 == 0xFE) return 204;
        } else if (cmd != 1 && cmd != 43 && cmd != 85) {
            /* 129, 130, 131, 132, 133, 134 */
            if (resp0 == 0xFF) return 201;
            if (resp0 == 1)    return 202;
            if (resp0 == 0xFE) return 204;
        }
        /* cmds 1, 43, 85 do not special-case status 2; fall through. */
    }
    return 1;
}

/* Public raw-command dongle entry (Lumax_DongleCom in liblumax.so; declared in
   lumax.h). Raw command set: 1, 43, 85, 90, 129, 130, 131, 132, 133, 134. */
int Lumax_DongleCom(int PhysicalDevice, int Command, unsigned long Vendor_ID, unsigned long *DataOut, unsigned long *DataIn)
{
    unsigned char *in  = (unsigned char *)DataOut;
    unsigned char *out = (unsigned char *)DataIn;
    int r;

    if (U9mp(PhysicalDevice))
        return 1; /* device not open / link down */

    /* Original preconditions: 132 and 85 require a magic sentinel word the
       caller pre-loads into DataIn[0] before the call. */
    if (Command == 132) {
        if (!DataOut || !DataIn || dw_get((const unsigned char *)DataIn, 0) != 1442775125u)
            return 1;
    } else if (Command == 85) {
        if (!DataIn || (int32_t)dw_get((const unsigned char *)DataIn, 0) != -11184896)
            return 1;
    }

    r = dongleRawCom(PhysicalDevice, Command, (int)Vendor_ID, in, out);
    return r;
}

/* Public 1-9 command entry (Lumax_Dongle_XTEA_Com in lumax.dll). Maps the
   public command onto the raw set and runs it via dongleRawCom; command 1
   ("dongle present") additionally remaps the card's one-byte answer. */
int Lumax_Dongle_XTEA_Com(int PhysicalDevice, int Command, unsigned long Vendor_ID, unsigned long *DataOut, unsigned long *DataIn)
{
    int raw;

    if (U9mp(PhysicalDevice))
        return 1; /* device not open / link down */

    switch (Command) {
    case 1: {
        /* Query dongle present: raw 1 returns one byte, remap it. */
        uint32_t val = 0;
        if (!DataIn)
            return 1;
        dw_set((unsigned char *)DataIn, 0, 0);
        if (dongleRawCom(PhysicalDevice, 1, 0, NULL, (unsigned char *)DataIn) == 0) {
            val = dw_get((const unsigned char *)DataIn, 0);
            if (val == 1)
                dw_set((unsigned char *)DataIn, 0, 2);
            else if (val == 2)
                dw_set((unsigned char *)DataIn, 0, 1);
            else if (val == 254)
                dw_set((unsigned char *)DataIn, 0, 3);
            return 0;
        }
        return 1;
    }
    case 2: raw = 90;  break;
    case 3: raw = 129; break;
    case 4: raw = 131; break;
    case 5: raw = 130; break;
    case 6: raw = 133; break;
    case 7: raw = 134; break;
    case 8: raw = 132; break;
    case 9: raw = 85;  break;
    default:
        return 1;
    }

    return dongleRawCom(PhysicalDevice, raw, (int)Vendor_ID,
                        (const unsigned char *)DataOut, (unsigned char *)DataIn);
}

int Lumax_ExchangeDevInfo(int PhysicalDevice, unsigned long *Vendor_ID, unsigned long *Product_ID, unsigned long *Version, unsigned long *Features)
{
    /* Exchange device info. In lumax.dll this is tightly bound to the
       obfuscated per-device Block struct (a marker write plus a
       version/features read-back). That struct is not ported 1:1 here, so
       this remains a documented stub: it validates its inputs and reports
       "not supported" (1) rather than touching unmodelled state. */
    if (PhysicalDevice <= 0 || PhysicalDevice > MAX_DEVICES)
        return 1;
    if (!Vendor_ID || !Product_ID || !Version || !Features)
        return 1;
    (void)Vendor_ID;
    (void)Product_ID;
    (void)Version;
    (void)Features;
    return 1;
}

int Lumax_SetDacValue(int Handle, int Value)
{
    /* No-op in the reference implementation (lumax.dll Lumax_SetDacValue
       unconditionally returns 1). Kept for API/export compatibility. */
    (void)Handle;
    (void)Value;
    return 1;
}

/* Internal helper functions (from liblumax.h) */

int openDev(int numDev, struct lumax_device *dev)
{
    /* Check if numDev is valid */
    if (numDev < 1 || numDev > num_detected_devices)
        return 0;

    /* Copy the device information */
    memcpy(dev, &detected_devices[numDev-1], sizeof(struct lumax_device));
    /* Note: ftdi context is not copied, it will be set during open */
    dev->ftdi = NULL;

    return 1;
}

int openDevBySerial(const char *serial, struct lumax_device *dev)
{
    int i;
    /* Find device with matching serial */
    for (i = 0; i < num_detected_devices; i++) {
        if (strcmp(detected_devices[i].serial, serial) == 0) {
            /* Found matching device */
            memcpy(dev, &detected_devices[i], sizeof(struct lumax_device));
            /* Note: ftdi context is not copied, it will be set during open */
            dev->ftdi = NULL;
            return 1;
        }
    }
    /* Device not found */
    return 0;
}

/* Port of HJdd (liblumax.so): finalize the point geometry for the active
 * flavor. v10 = DmxFlavorMask (0 for a basic card -> treated as 1).
 * flavor = Flavor & mask. Per flavor, BytesPerPoint and MaxScanSpeed are fixed
 * and MaxPoints = FrameCapacity / BytesPerPoint. For a basic card (mask 0)
 * this is a pure computation with no USB I/O; for a Bare-Lumax card (mask
 * non-zero) the reference also sends a 7-byte flavor command (gKTU + a framed
 * header + 2-byte verify), which this port does not model. 0 on success. */
static int hjdd(struct lumax_device *dev)
{
    if (!dev)
        return 1;
    int mask = dev->DmxFlavorMask ? dev->DmxFlavorMask : 1;
    int flavor = dev->Flavor & mask;
    int bytes_per_point = 0;
    int max_scanspeed = 0;
    if (flavor == 1)      { bytes_per_point = 7; max_scanspeed = 70000; }
    else if (flavor == 2) { bytes_per_point = 6; max_scanspeed = 70000; }
    else if (flavor == 4) { bytes_per_point = 7; max_scanspeed = 65000; }
    else if (flavor == 8) { bytes_per_point = 8; max_scanspeed = 55000; }
    else if (flavor == 16){ bytes_per_point = 9; max_scanspeed = 45000; }
    else
        return 1;
    int max_points = dev->FrameCapacity / bytes_per_point;
    if (max_points <= 0)
        return 1;
    if (dev->DmxFlavorMask) {
        /* Bare-Lumax flavor command not modelled in this port. */
        return 1;
    }
    dev->Flavor = flavor;
    dev->BytesPerPoint = bytes_per_point;
    dev->MaxPoints = max_points;
    dev->MaxScanSpeed = max_scanspeed;
    return 0;
}

/* Port of the reference open handshake. liblumax.so runs all of this inside
 * Lumax_OpenDevice (there is no separate init function): after FT_OpenEx and
 * the timeout/latency setup it does P5555 (reset), g4jz (readID), an
 * advanced-card block (wp8o readMemory + dongle + fvr), and then the final
 * gate HJdd/StopFrame/SetDmxMode/SetTTL. The slot is registered before any of
 * it, so the handle-keyed calls in the gate resolve. This port factors the
 * same sequence out so it is also reusable by busy-recovery. Returns 0 on
 * full success, -1 otherwise. */
int lumax_init_device(struct lumax_device *dev)
{
    if (!dev || !dev->ftdi)
        return -1;

    int handle = dev->numDev;
    int r;

    /* 1. P5555 reset: 256 zero-bytes, 256 times = 65536 zeros, then 200 ms
     * settle, then purge RX+TX. (In the decompilation the write amount 0x100
     * was mislabelled as the "GNU" string pointer; the payload is all zeros.) */
    unsigned char zbuf[256];
    memset(zbuf, 0, sizeof(zbuf));
    int wr_total = 0;
    int rep;
    for (rep = 0; rep < 256; rep++) {
        int w = ftdi_write_data(dev->ftdi, zbuf, 256);
        if (w < 0) {
            DBG("init P5555 reset write rep %d failed: %d", rep, w);
            return -1;
        }
        wr_total += w;
    }
    DBG("init P5555 reset wrote %d bytes (expect 65536)", wr_total);
    usleep(200000);                 /* 200 ms settle, matches reference */
    ftdi_usb_purge_buffers(dev->ftdi);

    /* 2. g4jz readID. A basic card answers with only the 0xC9 echo (1 byte in
     * the queue) -> success with a zeroed id. An advanced card answers with
     * 18 bytes (echo + 16 data + checksum). On transport failure the reference
     * simply skips the id block and still reaches the final gate; we do the
     * same (treat as basic) rather than failing the open. */
    uint8_t id[16];
    memset(id, 0, sizeof(id));
    r = readID(dev, id, 16);
    DBG("init g4jz readID = %d (0=basic/ok, 1=fail)", r);
    if (r != 0) {
        DBG("  readID failed; treating as a basic card");
    } else {
        int i;
        for (i = 0; i < 16; i++)
            DBG("  id[%d]=0x%02x", i, id[i]);
    }

    /* 3. Advanced-card config (only when id[4] > 6). The connected card is
     * basic (id[4]==0) so this branch is not taken; it is kept for fidelity.
     * wp8o reads 15 bytes of card memory; a read failure invalidates the open
     * in the reference (v10[1]=0). */
    if (r == 0 && id[4] > 6) {
        dev->dmx_state_byte = 5;    /* reference: v10[324143] = 5 for advanced */
        uint8_t mem[16];
        if (readMemory(dev, mem, 0, 15) != 0) {
            DBG("init wp8o readMemory (advanced) failed");
            return -1;
        }
        /* The reference then performs the dongle exchange (Lumax_DongleCom cmd
         * 1) and, for id[4] 9..11, applies the card's flavor/mask/waitgate and
         * (for id[4] > 10) re-derives the frame divisor. That memory layout is
         * not modelled in this port; HJdd below still finalizes the point
         * geometry for whatever Flavor is set. */
        DBG("init: advanced card (id[4]=%d); flavor config left to HJdd", id[4]);
    }

    /* 4. HJdd: finalize BytesPerPoint / MaxPoints / MaxScanSpeed / Flavor for
     * the (basic) flavor. For a basic card (DmxFlavorMask==0) this is a pure
     * computation with no USB I/O. */
    if (hjdd(dev) != 0) {
        DBG("init HJdd(flavor=%d) failed", dev->Flavor);
        return -1;
    }
    DBG("init HJdd done: flavor=%d BytesPerPoint=%d MaxPoints=%d MaxScanSpeed=%d",
        dev->Flavor, dev->BytesPerPoint, dev->MaxPoints, dev->MaxScanSpeed);

    /* 5. Reference final gate: StopFrame, DMX off, TTL off. All must succeed
     * (each returns 0 on success). StopFrame == SendFrame(0 points) +
     * WaitForBuffer(2000 ms). */
    r = Lumax_StopFrame(handle);
    DBG("init StopFrame = %d (expect 0)", r);
    if (r != 0)
        return -1;

    r = Lumax_SetDmxMode(handle, 0, 0);
    DBG("init SetDmxMode(0,0) = %d (expect 0)", r);
    if (r != 0)
        return -1;

    r = Lumax_SetTTL(handle, 0);
    DBG("init SetTTL(0) = %d (expect 0)", r);
    if (r != 0)
        return -1;

    return 0; /* Success */
}

/* Helper functions reverse engineered from liblumax.so */

/* U9mp: Check if device handle is valid/ready
   Returns 0 if device is valid, 1 if device is invalid/closed */
static int U9mp(int handle)
{
    struct lumax_device* dev = find_open_device_by_handle(handle);
    if (!dev)
        return 1; /* Device not open */

    if (!dev->ftdi)
        return 1; /* Device ftdi context is NULL (link down) */

    return 0; /* Device is valid */
}

/* nmg11: Write buffer to device (similar to FT_Write but with device-specific formatting)
   Returns number of bytes written, or -1 on error */
static int nmg11(int handle, intptr_t buffer, int length)
{
    struct lumax_device* dev = find_open_device_by_handle(handle);
    if (!dev)
        return -1;

    if (!dev->ftdi)
        return -1;

    /* Actual libftdi1 write */
    int written = ftdi_write_data(dev->ftdi, (const unsigned char*)buffer, length);
    if (written < 0) {
        return -1;
    }
    return written;
}

/* bnQQ: Read buffer from device (similar to FT_Read but with device-specific formatting)
   Returns number of bytes read, or -1 on error.

   The reference .so's bnQQ wraps D2XX FT_Read, which blocks and accumulates
   bytes across multiple USB transactions until it has exactly `length` bytes
   or the read timeout expires. libftdi1's ftdi_read_data is a single USB
   read that can return after the first packet, so a short response split
   across two packets would otherwise be miscounted. To match FT_Read we loop,
   accumulating until we have `length` bytes or a read times out / errors. */
static int bnQQ(int handle, intptr_t buffer, int length)
{
    struct lumax_device* dev = find_open_device_by_handle(handle);
    if (!dev)
        return -1;

    if (!dev->ftdi)
        return -1;

    unsigned char *buf = (unsigned char *)buffer;
    int total = 0;
    /* Honor usb_read_timeout as a monotonic deadline. ftdi_read_data on this
       stack returns ~immediately with whatever is buffered (0 if none), so we
       must re-poll until we have `length` bytes or the budget elapses; a single
       empty read does NOT mean the reply has failed to arrive. */
    int budget = dev->ftdi->usb_read_timeout;
    int start = timeGetTime();
    while (total < length) {
        int r = ftdi_read_data(dev->ftdi, buf + total, length - total);
        if (r > 0) { total += r; continue; }
        if (budget <= 0)
            break; /* non-blocking: nothing ready right now */
        if (timeGetTime() - start >= (unsigned int)budget)
            break; /* deadline reached */
        usleep(2000); /* ~2 ms between polls */
    }
    return total;
}

/* ---- Device-pointer-based low-level I/O ----
 *
 * The handle-keyed nmg11/bnQQ resolve the device through
 * find_open_device_by_handle(), which is only valid once a slot has been
 * added to open_devices[]. But lumax_init_device() runs *before* the slot is
 * registered (Lumax_OpenDevice increments num_open_devices only after init
 * succeeds), so the init path drives these dev->ftdi-based primitives
 * directly. They mirror the reference .so's nmg11/bnQQ, which also return
 * 0 on success / 1 on failure.
 */
static int dev_write(struct lumax_device *dev, const uint8_t *buf, int len)
{
    if (!dev || !dev->ftdi)
        return 1;
    return (ftdi_write_data(dev->ftdi, buf, len) == len) ? 0 : 1;
}

static int dev_read(struct lumax_device *dev, uint8_t *buf, int len)
{
    if (!dev || !dev->ftdi)
        return 1;
    /* FT_Read semantics: accumulate across packets until `len` bytes arrive or
       the read-timeout budget elapses. Re-poll on empty reads (ftdi_read_data
       returns ~immediately here) -- see bnQQ above. */
    int total = 0;
    int budget = dev->ftdi->usb_read_timeout;
    int start = timeGetTime();
    while (total < len) {
        int r = ftdi_read_data(dev->ftdi, buf + total, len - total);
        if (r > 0) { total += r; continue; }
        if (budget <= 0)
            break;
        if (timeGetTime() - start >= (unsigned int)budget)
            break;
        usleep(2000);
    }
    return (total == len) ? 0 : 1;
}

/* Emulates FT_GetQueueStatus (the .so's LUO2): returns the number of bytes
 * currently sitting in the RX buffer without waiting for more. libftdi1 has
 * no direct equivalent, so we poll with a 0ms (non-blocking) read. The caller
 * must restore ftdi->usb_read_timeout afterwards (readID does this). */
static int dev_read_available(struct lumax_device *dev)
{
    unsigned char b;
    if (!dev || !dev->ftdi)
        return 0;
    int save = dev->ftdi->usb_read_timeout;
    dev->ftdi->usb_read_timeout = 0;
    int n = ftdi_read_data(dev->ftdi, &b, 1);
    dev->ftdi->usb_read_timeout = save;
    return (n > 0) ? 1 : 0;
}

/* fvr: receive a framed block from the card (DMX). Port of fvr (liblumax.so).
   Sends a 7-byte header [2, cmd_lo, cmd_hi, len_lo, len_hi, flag, checksum]
   where checksum = XOR(header[0..5]); the card echoes that checksum (1 byte),
   streams `length` data bytes into dmx_buffer, then sends a data checksum
   (1 byte) which must equal the XOR of all data bytes. 0 on success, 1 on
   any I/O or checksum mismatch. (Our nmg11/bnQQ return byte counts, so
   success is "read exactly N", unlike the .so where they return 0=OK.) */
static int fvr(int handle, intptr_t dmx_buffer, unsigned short cmd, unsigned short length, char flag)
{
    unsigned char header[7];
    unsigned char *buf = (unsigned char *)dmx_buffer;
    unsigned char echo, data_checksum, x;
    int i;

    if (!length || (unsigned int)(length + cmd) > 0x10000)
        return 1;

    header[0] = 2;
    header[1] = cmd & 0xFF;
    header[2] = (cmd >> 8) & 0xFF;
    header[3] = length & 0xFF;
    header[4] = (length >> 8) & 0xFF;
    header[5] = (unsigned char)flag;
    header[6] = header[0] ^ header[1] ^ header[2] ^ header[3] ^ header[4] ^ header[5];

    if (nmg11(handle, (intptr_t)header, 7) != 7)
        return 1;

    /* Echo of the header checksum must match. */
    if (bnQQ(handle, (intptr_t)&echo, 1) != 1 || echo != header[6])
        return 1;

    /* The framed data. */
    if (bnQQ(handle, (intptr_t)buf, length) != length)
        return 1;

    /* Data checksum = XOR of every data byte. */
    if (bnQQ(handle, (intptr_t)&data_checksum, 1) != 1)
        return 1;

    x = 0;
    for (i = 0; i < length; i++)
        x ^= buf[i];
    if (data_checksum != x)
        return 1;

    return 0;
}

/* vN67: single 24-byte XTEA dongle transaction. Port of vN67 (liblumax.so).
   Packet layout:
     [0]   0xCD            (header byte)
     [1..4] address        (4 bytes, little-endian)
     [5]   command
     [6]   hdr_checksum = address(4) ^ 0xCD ^ command
     [7..22] write_buf     (16 bytes)
     [23]  data_checksum = XOR(write_buf[0..15])
   The card echoes the header checksum (1 byte, must match), then returns a
   14-byte response; the 14th byte must equal XOR(response[0..12]) and the
   13 payload bytes are copied into read_buf. 0 on success, 1 otherwise. */
static int vN67(int handle, char command, int address, intptr_t write_buf, intptr_t read_buf)
{
    unsigned char packet[24];
    unsigned char *wb = (unsigned char *)write_buf;
    unsigned char *rb = (unsigned char *)read_buf;
    unsigned char echo, data_checksum;
    unsigned char response[14];
    unsigned char x;
    int i;

    packet[0] = 0xCD;
    packet[1] = address & 0xFF;
    packet[2] = (address >> 8) & 0xFF;
    packet[3] = (address >> 16) & 0xFF;
    packet[4] = (address >> 24) & 0xFF;
    packet[5] = (unsigned char)command;
    packet[6] = packet[1] ^ packet[2] ^ packet[3] ^ packet[4] ^ packet[0] ^ packet[5];

    data_checksum = 0;
    for (i = 0; i < 16; i++) {
        packet[7 + i] = wb ? wb[i] : 0;
        data_checksum ^= packet[7 + i];
    }
    packet[23] = data_checksum;

    if (nmg11(handle, (intptr_t)packet, 24) != 24)
        return 1;

    /* Echo of the header checksum must match. */
    if (bnQQ(handle, (intptr_t)&echo, 1) != 1 || echo != packet[6])
        return 1;

    /* 14-byte response. */
    if (bnQQ(handle, (intptr_t)response, 14) != 14)
        return 1;

    /* Response checksum = XOR of the first 13 bytes. */
    x = 0;
    for (i = 0; i < 13; i++)
        x ^= response[i];
    if (response[13] != x)
        return 1;

    if (rb)
        memcpy(rb, response, 13);

    return 0;
}

int clearBuffer(struct lumax_device *dev)
{
    if (!dev || !dev->ftdi)
        return 0;

    /* Send clear buffer command */
    /* Based on analysis, this is likely a specific command */
    unsigned char cmd[2] = {0x04, 0x02}; /* Example clear buffer command */

    if (nmg11(0, (intptr_t)cmd, 2) != 2) {
        /* Try direct write since we might not have a valid handle yet */
        int written = ftdi_write_data(dev->ftdi, cmd, 2);
        return (written == 2) ? 1 : 0;
    }

    return 1;
}

void checkIfBusy(struct lumax_device *dev, uint32_t ftStatus)
{
    if (!dev)
        return;

    /* Check if device reports busy status */
    /* Bit 3 of TimeUntilFree indicates if device is busy */
    if (dev->TimeUntilFree & 8) {
        dev->IsBusy = 1;
        dev->BusyTime = ftStatus; /* Assume ftStatus is current time */
    } else {
        dev->IsBusy = 0;
        dev->BusyTime = 0;
    }
}

int isOpen(struct lumax_device *dev)
{
    if (!dev)
        return 0;
    return (dev->ftdi != NULL);
}

int sendInitKey(struct lumax_device *dev)
{
    if (!dev || !dev->ftdi)
        return 0;

    /* Send initialization key */
    /* Based on analysis of the DLL, this sends a specific key sequence */
    unsigned char key[8] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};

    if (nmg11(0, (intptr_t)key, 8) != 8) {
        /* Try direct write */
        int written = ftdi_write_data(dev->ftdi, key, 8);
        return (written == 8) ? 1 : 0;
    }

    return 1;
}

int sendKey(struct lumax_device *dev, int flavor)
{
    if (!dev || !dev->ftdi)
        return 0;

    /* Send flavor key */
    /* Based on analysis, this sends a key based on the flavor value */
    unsigned char key[8];
    key[0] = 0x09; /* Command for sending key */
    key[1] = (flavor & 0xFF00) >> 8;
    key[2] = flavor & 0x00FF;
    /* Fill remaining bytes with fixed values or compute based on flavor */
    key[3] = 0x10;
    key[4] = 0x20;
    key[5] = 0x30;
    key[6] = 0x40;
    key[7] = 0x50;

    if (nmg11(0, (intptr_t)key, 8) != 8) {
        /* Try direct write */
        int written = ftdi_write_data(dev->ftdi, key, 8);
        return (written == 8) ? 1 : 0;
    }

    return 1;
}

int writeToDev(struct lumax_device *dev, uint8_t *buffer, uint32_t bytesToWrite)
{
    if (!dev || !dev->ftdi || !buffer || bytesToWrite == 0)
        return 0;

    int written = ftdi_write_data(dev->ftdi, buffer, bytesToWrite);
    return (written == (int)bytesToWrite) ? 1 : 0;
}

int readFromDev(struct lumax_device *dev, uint8_t *buffer, uint32_t bytesToRead)
{
    if (!dev || !dev->ftdi || !buffer || bytesToRead == 0)
        return 0;

    int read_len = ftdi_read_data(dev->ftdi, buffer, bytesToRead);
    return (read_len == (int)bytesToRead) ? 1 : 0;
}

int writeFrameBuffer(struct lumax_device *dev, uint8_t *frameBuffer, uint16_t frameBufferSize, uint16_t numberOfBytes, uint8_t counter, int flag)
{
    if (!dev || !dev->ftdi || !frameBuffer || numberOfBytes == 0)
        return 0;

    /* Apply any counter or flag modifications if needed */
    /* For now, just write the buffer as-is */

    int written = ftdi_write_data(dev->ftdi, frameBuffer, numberOfBytes);
    return (written == (int)numberOfBytes) ? 1 : 0;
}

int readID(struct lumax_device *dev, uint8_t *arr, uint16_t size)
{
    if (!dev || !dev->ftdi || !arr || size < 16)
        return 1;

    /* g4jz: write [0xC9, 0,0,0,0,0, 0xC9], wait 50 ms, then classify the
       response. A basic card answers with only the 0xC9 echo (1 byte); an
       advanced card sends 18 bytes (echo + 16 data + 1 data checksum). */
    unsigned char cmd[7] = {0xC9, 0, 0, 0, 0, 0, 0xC9};
    if (dev_write(dev, cmd, 7))
        return 1;

    usleep(50000); /* 50 ms initial settle, matches reference */

    /* Accumulate everything the card sends (the 0xC9 echo, and for an advanced
       card 16 data bytes + 1 checksum right after it).
       NOTE: on the macOS libftdi1+libusb stack ftdi_read_data returns in ~1 ms
       rather than blocking for usb_read_timeout, so a single drain attempt
       races the echo's USB delivery and intermittently sees 0 bytes even though
       the byte is still in the buffer a few ms later (it is never lost). Poll
       with a monotonic deadline instead: wait for the first byte, then keep a
       short window open for follow-on bytes before classifying basic/advanced. */
    unsigned char acc[18];
    int total = 0;
    int save_to = dev->ftdi->usb_read_timeout;
    dev->ftdi->usb_read_timeout = 0; /* non-blocking poll */

    /* Phase 1: wait (up to ~300 ms) for the first byte, the 0xC9 echo. */
    int start1 = timeGetTime();
    while (total < 1) {
        int r = ftdi_read_data(dev->ftdi, acc + total, 18 - total);
        if (r > 0) { total += r; continue; }
        if (timeGetTime() - start1 >= 300) break;
        usleep(5000); /* ~5 ms between polls */
    }

    /* Phase 2: brief grace window to collect follow-on bytes (advanced card).
       A basic card sends only the echo, so this just confirms nothing more. */
    if (total > 0 && total < 18) {
        int start2 = timeGetTime();
        while (total < 18 && timeGetTime() - start2 < 40) {
            int r = ftdi_read_data(dev->ftdi, acc + total, 18 - total);
            if (r > 0) { total += r; continue; }
            usleep(2000);
        }
    }
    dev->ftdi->usb_read_timeout = save_to;

    if (total <= 0)
        return 1;
    if (acc[0] != 0xC9) { /* echo mismatch */
        DBG("readID: echo 0x%02x != 0xC9", acc[0]);
        return 1;
    }

    if (total == 1) {
        /* Basic card: no ID block. Zero the 16-byte buffer, report success. */
        memset(arr, 0, 16);
        DBG("readID: basic card (echo only)");
        return 0;
    }

    if (total < 18) {
        DBG("readID: expected 18 bytes, got %d", total);
        return 1;
    }

    /* Advanced card: 16 data bytes (acc[1..16]) + 1 checksum (acc[17]). */
    int x = 0;
    for (int i = 0; i < 16; i++) {
        arr[i] = acc[1 + i];
        x ^= arr[i];
    }
    if (x != acc[17]) {
        DBG("readID: data xor 0x%02x != checksum 0x%02x", x, acc[17]);
        return 1;
    }
    DBG("readID: advanced card, id[4]=0x%02x", arr[4]);
    return 0;
}

int readMemory(struct lumax_device *dev, uint8_t *arr, uint16_t start, uint16_t len)
{
    if (!dev || !dev->ftdi || !arr)
        return 1;
    if (start < 0 || len <= 0 || start + len > 400)
        return 1;

    /* wp8o: write [0xCA, start_lo, start_hi, len_lo, len_hi, 0, checksum] and
       read len+2 bytes (echo checksum + data + data checksum). */
    unsigned char cmd[7];
    cmd[0] = 0xCA;
    cmd[1] = start & 0xFF;
    cmd[2] = (start >> 8) & 0xFF;
    cmd[3] = len & 0xFF;
    cmd[4] = (len >> 8) & 0xFF;
    cmd[5] = 0;
    cmd[6] = (unsigned char)(cmd[0] ^ cmd[1] ^ cmd[2] ^ cmd[3] ^ cmd[4] ^ cmd[5]);

    if (dev_write(dev, cmd, 7))
        return 1;

    unsigned char rx[400 + 2];
    if (dev_read(dev, rx, len + 2))
        return 1;

    int x = 0;
    for (int i = 0; i < len; i++) {
        arr[i] = rx[1 + i];
        x ^= arr[i];
    }
    if (rx[0] != cmd[6] || rx[len + 1] != (unsigned char)x) {
        DBG("readMemory: echo 0x%02x (want 0x%02x), data xor 0x%02x (want 0x%02x)",
            rx[0], cmd[6], rx[len + 1], x);
        return 1;
    }
    DBG("readMemory: start=%d len=%d ok", start, len);
    return 0;
}

void Lumax_SetBlankingDelay(int delay_ms)
{
    /* Store the blanking delay globally or per device */
    /* For now, we'll just store it globally */
    /* In a full implementation, this would be per-device state */
    static int global_blanking_delay = 0;
    global_blanking_delay = delay_ms;
}

int dongleCom(struct lumax_device *dev, uint8_t flag, uint32_t address, uint8_t *writeBuffer, uint8_t *readBuffer)
{
    if (!dev || !dev->ftdi)
        return 0;

    /* Dongle communication function */
    /* Based on analysis, this handles communication with the security dongle */

    /* Build command packet */
    unsigned char cmd[10];
    int cmd_len = 0;

    cmd[cmd_len++] = 0x12; /* Dongle command */
    cmd[cmd_len++] = flag;
    cmd[cmd_len++] = address & 0xFF;
    cmd[cmd_len++] = (address >> 8) & 0xFF;
    cmd[cmd_len++] = (address >> 16) & 0xFF;
    cmd[cmd_len++] = (address >> 24) & 0xFF;

    /* Add write buffer if provided */
    if (writeBuffer) {
        /* Assuming we know the length somehow - in reality this would be passed as a parameter */
        /* For now, we'll assume a fixed length or determine it from context */
        int write_len = 8; /* Example length */
        if (cmd_len + write_len < sizeof(cmd)) {
            memcpy(&cmd[cmd_len], writeBuffer, write_len);
            cmd_len += write_len;
        }
    }

    /* Send command */
    if (nmg11(0, (intptr_t)cmd, cmd_len) != cmd_len) {
        /* Try direct write */
        int written = ftdi_write_data(dev->ftdi, cmd, cmd_len);
        if (written != cmd_len)
            return 0;
    }

    /* Read response if readBuffer is provided */
    if (readBuffer) {
        /* Assuming we know the response length */
        int read_len = 8; /* Example length */
        int read_result = ftdi_read_data(dev->ftdi, readBuffer, read_len);
        return (read_result == read_len) ? 1 : 0;
    }

    return 1;
}

/* Helper function reverse engineered from Tj4 in liblumax.so */
static int Tj4(int handle, intptr_t buffer_ptr, unsigned short a3, unsigned short a4, char a5, int a6, int a7)
{
    struct lumax_device* dev = find_open_device_by_handle(handle);
    if (!dev)
        return 1; /* Device not valid */

    int v16 = 1; /* Default return value (error) */
    unsigned char v8 = a7 ? 1 : dev->dmx_state_byte;
    unsigned char v9 = v8;
    unsigned short v10 = a3;
    unsigned short v11 = a4;
    unsigned char v12 = a5;
    unsigned char v13 = ((a4 >> 8) ^ a4 ^ ((a3 >> 8) ^ a3) ^ v8 ^ a5);

    /* First header: 7 bytes [v9, v10 low, v10 high, v11 low, v11 high, v12, v13] */
    unsigned char header1[7];
    header1[0] = v9;
    header1[1] = v10 & 0xFF; /* low byte */
    header1[2] = (v10 >> 8) & 0xFF; /* high byte */
    header1[3] = v11 & 0xFF; /* low byte */
    header1[4] = (v11 >> 8) & 0xFF; /* high byte */
    header1[5] = v12;
    header1[6] = v13;

    /* Send header */
    if (nmg11(handle, (intptr_t)header1, 7) != 7) {
        DBG("Tj4: header write failed (v9=%u cks=0x%02x)", v9, v13);
        return v16; /* Error */
    }

    /* Send DMX buffer */
    if (nmg11(handle, buffer_ptr, a4) != a4) {
        DBG("Tj4: data write failed (a4=%u)", a4);
        return v16; /* Error */
    }

    if (a6) {
        /* Read 2 bytes and return the number of bytes read */
        unsigned char resp[2];
        int read_len = bnQQ(handle, (intptr_t)resp, 2);
        if (read_len < 0) {
            DBG("Tj4: read (a6) failed");
            return v16;
        }
        return read_len; /* Note: original returns the read length */
    } else {
        /* Expect exactly 2 bytes: [v13, 0x55] */
        unsigned char resp[2];
        int read_len = bnQQ(handle, (intptr_t)resp, 2);
        if (read_len != 2) {
            DBG("Tj4: read got %d bytes (want 2)", read_len);
            return v16;
        }
        if (resp[0] != v13 || resp[1] != 0x55) {
            DBG("Tj4: resp [0x%02x 0x%02x] != [0x%02x 0x55]", resp[0], resp[1], v13);
            return v16;
        }
        return 0; /* Success */
    }
}

/* Exported API Functions */
int Lumax_SendDmx(int Handle, unsigned char *DmxBuffer, int Length)
{
    if (Length <= 0 || Length > 1025 || U9mp(Handle))
        return 1;
    else
        return Tj4(Handle, (intptr_t)DmxBuffer, 0xFB0C, Length, 0, 0, 0);
}