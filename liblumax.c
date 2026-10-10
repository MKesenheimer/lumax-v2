/*
 * liblumax.c - Reverse-engineered Lumax laser controller library
 * Implements the Lumax API using FTDI USB devices.
 *
 * Based on analysis of lumax.dll and header files.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "liblumax.h"
#include "lumax.h"
#include <libftdi1/ftdi.h>

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

    /* Find all Lumax devices (VID=0x403, PID=0xC82A) */
    ret = ftdi_usb_find_all(ftdi, &devlist, 0x403, 0xC82A);
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

    /* Open device by serial number */
    if (ftdi_usb_open_string(ftdi, dev->serial) < 0) {
        ftdi_free(ftdi);
        return 0;
    }

    /* Initialize the device structure for the open state */
    struct lumax_device* openDev = &open_devices[num_open_devices];
    memcpy(openDev, dev, sizeof(struct lumax_device));
    openDev->ftdi = ftdi; /* Store the ftdi_context directly */

    /* Initialize the device (reset, etc.) */
    /* We will call lumax_init_device later, but for now we just mark as open */
    /* We'll set some default values */
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
    /* TODO: Implement frame sending */
    return 0;
}

int Lumax_WaitForBuffer(int Handle, int Timeout, int *TimeToWait, int *BufferChanged)
{
    /* TODO: Implement buffer wait */
    return 0;
}

int Lumax_StopFrame(int Handle)
{
    /* TODO: Implement stop frame */
    return 0;
}

int Lumax_SetTTL(int Handle, int TTL)
{
    /* TODO: Implement setting TTL */
    return 0;
}

int Lumax_SetDmxMode(int Handle, int NumOfTxChannels, int NumOfRxChannels)
{
    /* TODO: Implement DMX mode setting */
    return 0;
}

int Lumax_SendDmx(int Handle, unsigned char *DmxBuffer, int Length)
{
    /* TODO: Implement DMX sending */
    return 0;
}

int Lumax_ReceiveDmx(int Handle, unsigned char *DmxBuffer, int Length)
{
    /* TODO: Implement DMX receiving */
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
    /* TODO: Implement */
    return 0;
}

int openDevBySerial(const char *serial, struct lumax_device *dev)
{
    /* TODO: Implement */
    return 0;
}

int lumax_init_device(struct lumax_device *dev)
{
    /* TODO: Implement */
    return 0;
}

int clearBuffer(struct lumax_device *dev)
{
    /* TODO: Implement */
    return 0;
}

void checkIfBusy(struct lumax_device *dev, uint32_t ftStatus)
{
    /* TODO: Implement */
}

int isOpen(struct lumax_device *dev)
{
    /* TODO: Implement */
    return 0;
}

int sendInitKey(struct lumax_device *dev)
{
    /* TODO: Implement */
    return 0;
}

int sendKey(struct lumax_device *dev, int flavor)
{
    /* TODO: Implement */
    return 0;
}

int writeToDev(struct lumax_device *dev, uint8_t *buffer, uint32_t bytesToWrite)
{
    /* TODO: Implement */
    return 0;
}

int readFromDev(struct lumax_device *dev, uint8_t *buffer, uint32_t bytesToRead)
{
    /* TODO: Implement */
    return 0;
}

int writeFrameBuffer(struct lumax_device *dev, uint8_t *frameBuffer, uint16_t frameBufferSize, uint16_t numberOfBytes, uint8_t counter, int flag)
{
    /* TODO: Implement */
    return 0;
}

int readID(struct lumax_device *dev, uint8_t *arr, uint16_t size)
{
    /* TODO: Implement */
    return 0;
}

int readMemory(struct lumax_device *dev, uint8_t *arr, uint16_t start, uint16_t end)
{
    /* TODO: Implement */
    return 0;
}

void Lumax_SetBlankingDelay(int delay_ms)
{
    /* TODO: Implement */
}

int dongleCom(struct lumax_device *dev, uint8_t flag, uint32_t address, uint8_t *writeBuffer, uint8_t *readBuffer)
{
    /* TODO: Implement */
    return 0;
}