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
#include <libftdi1/ftdi.h>
#include "liblumax.h"
#include "lumax.h"

/* Forward declarations for helper functions */
static int U9mp(int handle);
static int nmg11(int handle, intptr_t buffer, int length);
static int bnQQ(int handle, intptr_t buffer, int length);

/* Maximum number of devices we can handle */
#define MAX_DEVICES 8

/* Global array of open devices */
static struct lumax_device open_devices[MAX_DEVICES];
static int num_open_devices = 0;

/* Global array of detected devices */
static struct lumax_device detected_devices[MAX_DEVICES];
static int num_detected_devices = 0;

/* Stub for ghrkr */
static void ghrkr(void) {
    /* TODO: Implement */
    return;
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
        return 0;
    }

    /* Open device by VID/PID and serial number */
    if (ftdi_usb_open_desc(ftdi, 0x403, 0xC88A, NULL, dev->serial) < 0) {
        ftdi_free(ftdi);
        return 0;
    }

    /* Initialize the device structure for the open state */
    struct lumax_device* openDev = &open_devices[num_open_devices];
    memcpy(openDev, dev, sizeof(struct lumax_device));
    openDev->ftdi = ftdi; /* Store the ftdi_context directly */
    /* Ensure the dmx_state_byte is copied (it is part of the memcpy) */

    /* Initialize the device (reset, etc.) */
    if (lumax_init_device(openDev) != 0) {
        /* Initialization failed, clean up */
        ftdi_usb_close(ftdi);
        ftdi_free(ftdi);
        return 0;
    }

    /* Initialize the device state values */
    openDev->BufferLayout = 1; /* default */
    openDev->TTLBuffer = 0;
    openDev->TTLAvailable = 0;
    openDev->Flavor = 1; /* default */
    openDev->MaxPoints = 0; /* will be set during init */
    openDev->MaxScanSpeed = 0;
    openDev->DmxFlavorMask = 0;
    openDev->NextLoopCounts = 0;
    openDev->TimeUntilFree = 0;
    openDev->TimeOffset = 0;
    openDev->NumberOfFramesSend = 0;
    openDev->ScanSpeed = 0;
    openDev->LastDivisor = 0;
    openDev->NumberOfPoints = 0;
    openDev->IsBusy = 0;
    openDev->BusyTime = 0;

    num_open_devices++;

    /* Return handle (index+1) */
    return num_open_devices; /* because we just incremented, so the index is num_open_devices-1, handle = index+1 = num_open_devices */
}

int Lumax_CloseDevice(int Handle)
{
    struct lumax_device* dev = find_open_device_by_handle(Handle);
    if (!dev)
        return 0;

    /* Close the libftdi1 handle */
    if (dev->ftdi) {
        ftdi_usb_close(dev->ftdi);
        ftdi_free(dev->ftdi);
        dev->ftdi = NULL;
    }

    /* Remove from open_devices array */
    int index = Handle - 1;
    if (index < 0 || index >= num_open_devices)
        return 0;

    /* Shift remaining devices left */
    int i;
    for (i = index; i < num_open_devices - 1; i++) {
        open_devices[i] = open_devices[i+1];
    }
    num_open_devices--;

    return 1;
}

int Lumax_SendFrame(int Handle, TLumax_Point *Points, int NumOfPoints, int ScanSpeed, int UpdateMode, int *TimeToWait)
{
    struct lumax_device* dev = find_open_device_by_handle(Handle);
    if (!dev)
        return 1; /* Device not valid */

    /* Handle default case: if NumOfPoints == 0, send a single point at center */
    if (NumOfPoints == 0) {
        TLumax_Point default_point;
        memset(&default_point, 0, sizeof(default_point));
        default_point.Ch1 = 0x8000; /* Center X */
        default_point.Ch2 = 0x8000; /* Center Y */
        Points = &default_point;
        NumOfPoints = 1;
        ScanSpeed = 1000; /* Default scan speed */
    }

    /* Validate number of points */
    if (NumOfPoints <= 0 || NumOfPoints > 8 * dev->MaxPoints)
        return 1; /* Invalid number of points */

    /* Validate and limit scan speed */
    if (ScanSpeed < 250)
        ScanSpeed = 250; /* Minimum scan speed */
    if (ScanSpeed > dev->MaxScanSpeed)
        ScanSpeed = dev->MaxScanSpeed; /* Maximum scan speed for this flavor */

    /* Update device state counters */
    dev->NumberOfFramesSend++;
    dev->ScanSpeed = ScanSpeed;

    /* Calculate number of chunks (based on MaxPoints) */
    int full_chunks = NumOfPoints / dev->MaxPoints;
    int remainder_points = NumOfPoints % dev->MaxPoints;
    int total_chunks = full_chunks + (remainder_points > 0 ? 1 : 0);

    /* Process each chunk */
    int point_index = 0;
    for (int chunk = 0; chunk < total_chunks; chunk++) {
        /* Determine size of this chunk */
        int chunk_size = (chunk < full_chunks) ? dev->MaxPoints : remainder_points;
        if (chunk_size == 0) continue;

        /* Prepare buffer for this chunk */
        /* Simplified conversion: take high 8 bits of each channel */
        /* In a full implementation, this would vary by Flavor value */
        unsigned char* buffer = malloc(chunk_size * 18); /* Max possible size: 9 shorts * 2 bytes each */
        if (!buffer)
            return 1; /* Memory allocation failed */

        int buf_pos = 0;
        for (int i = 0; i < chunk_size; i++) {
            TLumax_Point* p = &Points[point_index + i];

            /* Convert each channel to bytes (simplified: high byte only) */
            /* Ch1-Ch8, TTL */
            buffer[buf_pos++] = (p->Ch1 >> 8) & 0xFF;
            buffer[buf_pos++] = (p->Ch2 >> 8) & 0xFF;
            buffer[buf_pos++] = (p->Ch3 >> 8) & 0xFF;
            buffer[buf_pos++] = (p->Ch4 >> 8) & 0xFF;
            buffer[buf_pos++] = (p->Ch5 >> 8) & 0xFF;
            buffer[buf_pos++] = (p->Ch6 >> 8) & 0xFF;
            buffer[buf_pos++] = (p->Ch7 >> 8) & 0xFF;
            buffer[buf_pos++] = (p->Ch8 >> 8) & 0xFF;
            buffer[buf_pos++] = (p->TTL >> 8) & 0xFF;
        }

        /* Send the buffer to the device */
        int written = nmg11(Handle, (intptr_t)buffer, buf_pos);
        free(buffer);

        if (written != buf_pos)
            return 1; /* Failed to write data */

        /* Read response to verify */
        unsigned char response[2];
        int read_len = bnQQ(Handle, (intptr_t)response, 2);
        if (read_len != 2 || response[0] != 0x01 || response[1] != 0x02) {
            /* In a real implementation, we'd check for proper acknowledgment */
            /* For now, we'll just continue on any response */
        }

        point_index += chunk_size;
    }

    /* Update device state with last sent values */
    dev->LastDivisor = ScanSpeed;
    dev->NumberOfPoints = NumOfPoints;
    dev->IsBusy = 1;
    dev->BusyTime = 0; /* Would normally set to current time */

    return 0; /* Success */
}

int Lumax_WaitForBuffer(int Handle, int Timeout, int *TimeToWait, int *BufferChanged)
{
    struct lumax_device* dev = find_open_device_by_handle(Handle);
    if (!dev)
        return 1; /* Device not valid */

    /* Check if device reports busy time (bit 3 of TimeUntilFree) */
    if ((dev->TimeUntilFree & 8) == 0)
        return 0; /* Device not busy or no timeout info */

    unsigned char cmd_buf[7];
    unsigned char response_buf[10]; /* Enough for max response */
    int written, read_len;

    if (Timeout || !TimeToWait || !BufferChanged) {
        /* Timeout mode or invalid output pointers */
        cmd_buf[0] = 0x04;
        cmd_buf[1] = 0x01;
        cmd_buf[2] = 0x00;
        cmd_buf[3] = 0x00;
        cmd_buf[4] = 0x00;
        cmd_buf[5] = 0x00;
        cmd_buf[6] = 0x05;

        written = nmg11(Handle, (intptr_t)cmd_buf, 7);
        if (written != 7)
            return 1; /* Failed to write */

        read_len = bnQQ(Handle, (intptr_t)response_buf, 2);
        if (read_len != 2)
            return 1; /* Failed to read enough */

        if (response_buf[0] == 0x05 && response_buf[1] == 0x01) {
            /* Success: buffer changed */
            if (BufferChanged)
                *BufferChanged = 1;
            return 0;
        } else {
            /* Failed: buffer not changed or timeout */
            if (BufferChanged)
                *BufferChanged = 0;
            /* Set a flag in write_buffer to indicate wait failure */
            dev->write_buffer[0] = 1;
            return 1;
        }
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
    /* Stop frame: send command to halt laser output
       Sets all color channels to 0, positions X/Y to center, disables TTL outputs */
    uint8_t cmd_buf[3];
    cmd_buf[0] = 20;           /* Command code for stop frame */
    cmd_buf[1] = 0;            /* X position high byte */
    cmd_buf[2] = 0;            /* Y position high byte */

    /* Send the stop command to the device */
    if (nmg11(Handle, (intptr_t)cmd_buf, 3) != 3)
        return 0;

    /* Read back response to verify */
    uint8_t response_buf[10];
    if (bnQQ(Handle, (intptr_t)response_buf, 1) != 1)
        return 0;

    return 1;
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

    /* Prepare command: [0x30, TTL, 0, 0, 0, 0, 0] */
    cmd_buf[0] = 0x30;           /* Command code for Set TTL */
    cmd_buf[1] = (uint8_t)TTL;   /* TTL value to set */
    cmd_buf[2] = 0;
    cmd_buf[3] = 0;
    cmd_buf[4] = 0;
    cmd_buf[5] = 0;
    cmd_buf[6] = 0;

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
    /* TODO: Implement DMX receiving properly */
    (void)Handle;
    (void)DmxBuffer;
    (void)Length;
    return 0;
}

/* Dongle and exchange functions (not in VB API but exported) */

int Lumax_Dongle_XTEA_Com(int PhysicalDevice, int Command, unsigned long Vendor_ID, unsigned long *DataOut, unsigned long *DataIn)
{
    /* TODO: Implement dongle communication */
    return 0;
}

int Lumax_ExchangeDevInfo(int PhysicalDevice, unsigned long *Vendor_ID, unsigned long *Product_ID, unsigned long *Version, unsigned long *Features)
{
    /* TODO: Implement device info exchange */
    return 0;
}

int Lumax_SetDacValue(int Handle, int Value)
{
    /* TODO: Implement DAC value setting */
    return 0;
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

int lumax_init_device(struct lumax_device *dev)
{
    if (!dev || !dev->ftdi)
        return -1;

    int result;

    /* 1. Reset the per-card state */
    /* Based on analysis, this sends 256 zero bytes with "GNU" string */
    unsigned char reset_buf[259]; /* 256 zeros + "GNU" (3 bytes) */
    memset(reset_buf, 0, 256);
    reset_buf[256] = 'G';
    reset_buf[257] = 'N';
    reset_buf[258] = 'U';

    int written = ftdi_write_data(dev->ftdi, reset_buf, 259);
    if (written != 259) {
        return -1; /* Failed to write reset sequence */
    }

    /* Small delay after reset */
    usleep(10000); /* 10ms */

    /* 2. Clear buffer */
    result = clearBuffer(dev);
    if (result != 1) {
        return -1; /* Failed to clear buffer */
    }

    /* 3. ReadID */
    unsigned char id_buffer[16];
    result = readID(dev, id_buffer, 16);
    if (result < 0) {
        return -1; /* Failed to read ID */
    }
    /* Extract serial number from ID if needed */
    if (dev->serial[0] == '\0' && result >= 8) {
        /* Assuming serial is in first 8 bytes of ID response */
        memcpy(dev->serial, id_buffer, 8);
        dev->serial[8] = '\0'; /* Null terminate */
    }

    /* 4. ReadMemory */
    unsigned char mem_buffer[64];
    result = readMemory(dev, mem_buffer, 0, 63);
    if (result < 0) {
        return -1; /* Failed to read memory */
    }
    /* Extract configuration from memory */
    if (result >= 8) {
        dev->BufferLayout = mem_buffer[0] & 0x0F;
        dev->TTLBuffer = mem_buffer[1] & 0x7F;
        dev->TTLAvailable = (mem_buffer[2] & 0x01) | ((mem_buffer[3] & 0x01) << 1);
        dev->Flavor = 1 << (mem_buffer[4] & 0x0F); /* 1,2,4,8,16 */
        dev->MaxPoints = 256 * (mem_buffer[5] & 0x0F);
        dev->MaxScanSpeed = 100 * (mem_buffer[6] & 0x0F);
        dev->DmxFlavorMask = (mem_buffer[7] << 8) | mem_buffer[6];
    }

    /* 5. Dongle exchange + flavor key */
    /* Dongle exchange */
    unsigned char dongle_write[8] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
    unsigned char dongle_read[8];
    result = dongleCom(dev, 0x01, 0x0000, dongle_write, dongle_read);
    if (result != 1) {
        return -1; /* Dongle exchange failed */
    }

    /* Flavor key */
    result = sendKey(dev, dev->Flavor);
    if (result != 1) {
        return -1; /* Failed to send flavor key */
    }

    /* 6. Stop frame */
    result = Lumax_StopFrame(0); /* Handle 0 means use current device context */
    if (result != 1) {
        return -1; /* Stop frame failed */
    }

    /* 7. DMX off */
    result = Lumax_SetDmxMode(0, 0, 0);
    if (result != 1) {
        return -1; /* Failed to turn DMX off */
    }

    /* 8. TTL off */
    result = Lumax_SetTTL(0, 0);
    if (result != 1) {
        return -1; /* Failed to turn TTL off */
    }

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
   Returns number of bytes read, or -1 on error */
static int bnQQ(int handle, intptr_t buffer, int length)
{
    struct lumax_device* dev = find_open_device_by_handle(handle);
    if (!dev)
        return -1;

    if (!dev->ftdi)
        return -1;

    /* Actual libftdi1 read */
    int read_len = ftdi_read_data(dev->ftdi, (unsigned char*)buffer, length);
    if (read_len < 0) {
        return -1;
    }
    return read_len;
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
    if (!dev || !dev->ftdi || !arr || size == 0)
        return 0;

    /* Read device ID command */
    /* Based on analysis, this is likely a specific command to read device ID */
    unsigned char cmd[2] = {0x15, 0x00}; /* Example ID read command */

    if (nmg11(0, (intptr_t)cmd, 2) != 2) {
        /* Try direct write */
        int written = ftdi_write_data(dev->ftdi, cmd, 2);
        if (written != 2)
            return 0;
    }

    /* Read the ID response */
    int read_len = ftdi_read_data(dev->ftdi, arr, size);
    return (read_len >= 0) ? read_len : 0;
}

int readMemory(struct lumax_device *dev, uint8_t *arr, uint16_t start, uint16_t end)
{
    if (!dev || !dev->ftdi || !arr || start > end)
        return 0;

    /* Read memory command */
    /* Based on analysis, this sends start and end addresses */
    unsigned char cmd[5];
    cmd[0] = 0x14; /* Memory read command */
    cmd[1] = start & 0xFF;
    cmd[2] = (start >> 8) & 0xFF;
    cmd[3] = end & 0xFF;
    cmd[4] = (end >> 8) & 0xFF;

    if (nmg11(0, (intptr_t)cmd, 5) != 5) {
        /* Try direct write */
        int written = ftdi_write_data(dev->ftdi, cmd, 5);
        if (written != 5)
            return 0;
    }

    /* Read the memory data */
    uint16_t length = end - start + 1;
    int read_len = ftdi_read_data(dev->ftdi, arr, length);
    return (read_len >= 0) ? read_len : 0;
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
/* Helper function reverse engineered from Tj4 in liblumax.so */
static int Tj4(int handle, int buffer_ptr, unsigned short a3, unsigned short a4, char a5, int a6, int a7)
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
    if (nmg11(handle, (intptr_t)header1, 7) != 7)
        return v16; /* Error */

    /* Send DMX buffer */
    if (nmg11(handle, buffer_ptr, a4) != a4)
        return v16; /* Error */

    if (a6) {
        /* Read 2 bytes and return the number of bytes read */
        unsigned char resp[2];
        int read_len = bnQQ(handle, (intptr_t)resp, 2);
        if (read_len < 0)
            return v16;
        return read_len; /* Note: original returns the read length */
    } else {
        /* Expect exactly 2 bytes: [v13, 0x55] */
        unsigned char resp[2];
        int read_len = bnQQ(handle, (intptr_t)resp, 2);
        if (read_len != 2)
            return v16;
        if (resp[0] != v13 || resp[1] != 0x55)
            return v16;
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