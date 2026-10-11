# Lumax Library Reverse Engineering Project

## Overview
This project involves reversing the Lumax DLL (`lumax.dll`) which controls Lumax laser show controllers via USB using FTDI chips. The DLL provides functions for device enumeration, opening/closing devices, sending laser frames, configuring TTL outputs, DMX512 communication, and more.

## Directory Contents

- `Example_VB/`: A Visual Basic 6 example project demonstrating how to use the Lumax DLL.
  - Contains `lumax.dll` (the library to reverse), `Lumax_dll.bas` (API declarations), `Form1.frm`, `Global.bas`, and a compiled example executable `Beispiel_Lumax_VB.exe`.
  - The example shows how to enumerate devices, open handles, send laser frames (drawing a circle), and handle device closing.

- `minilumax_test/`: A test directory for the Lumax driver.
  - Contains `minilumax.exe` (test program), `lumax.dll` (same library), `readme.txt` (version and usage notes), `ML_Driver.mld` (possibly a Microchip Lab project file), and `liblumax.so` (Linux shared library for the Lumax driver).

- `minilumax_usb_64/`: FTDI USB driver files (both 32-bit and 64-bit) required for the Lumax DLL to function.
  - Includes `ftd2xx.h`, DLLs (`ftd2xx.dll`, `ftlang.dll`, etc.), driver `.sys` and `.inf` files, and release documentation.

## Additional Files Provided

- `lumax.h`: C header with API definitions and TLumax_Point struct.
- `liblumax.h`: Internal header with helper functions and device structure.
- `main.c`: Simple test program demonstrating usage.

## API Documentation (from Lumax_dll.bas)

The Lumax DLL exports the following functions (as declared in Visual Basic):

- `Lumax_GetApiVersion()` → Returns DLL version number (Integer)
- `Lumax_GetPhysicalDevices()` → Returns number of connected Lumax cards (Long)
- `Lumax_GetDeviceInfo(ByVal PhysicalDevice As Long)` → Reserved for future developments (Long)
- `Lumax_OpenDevice(ByVal PhysicalDevice As Long, ByVal Channel As Long)` → Opens a device and returns a handle (Long)
- `Lumax_CloseDevice(ByVal Handle As Long)` → Closes a device (Long)
- `Lumax_SendFrame(ByVal Handle As Long, ByRef Points As tLumax_Punkt, ByVal NumOfPoints As Long, ByVal ScanSpeed As Long, ByVal UpdateMode As Long, ByVal TimeToWait As Long)` → Sends a laser frame to the card (Long)
- `Lumax_WaitForBuffer(ByVal Handle As Long, ByVal Timeout As Long, ByRef TimeToWait As Long, ByRef BufferChanged As Boolean)` → Waits for buffer availability or returns its status (Long)
- `Lumax_StopFrame(ByVal Handle As Long)` → Halts laser output (sets all color channels to 0, positions X/Y to center, disables TTL outputs) (Long)
- `Lumax_SetTTL(ByVal Handle As Long, ByVal TTL As Long)` → Sets TTL outputs (Long)
- `Lumax_SetDmxMode(ByVal Handle As Long, ByVal NumOfTxChannels As Long, ByVal NumOfRxChannels As Long)` → Activates/deactivates DMX512 sender/receiver on the MiniLumax card (Long)
- `Lumax_SendDmx(ByVal Handle As Long, ByRef DmxBuffer As Byte, ByVal Length As Long)` → Transmits DMX512 data to the MiniLumax card's output buffer (Long)
- `Lumax_ReceiveDmx(ByVal Handle As Long, ByRef DmxBuffer As Byte, ByVal Length As Long)` → Receives DMX512 data from the MiniLumax card's input buffer (Long)

### Data Structures
- `tLumax_Punkt`: Defines a laser point with 16-bit coordinates (X0,X1,Y0,Y1), color channels (R0,R1,G0,G1,B0,B1), two additional channels (ch6l/ch6h, ch7l/ch7h, ch8l/ch8h), and TTL control (TTLl,TTLh). Each component is a byte.
- `LumaxDatenfeld(0 To 20000) As tLumax_Punkt`: Array to hold up to 20001 points for a frame.

## Function Inventory

Based on analysis of `lumax.dll` exports and header files, the following functions are part of the Lumax API:

### Exported API Functions
1. `Lumax_GetApiVersion` - Returns DLL version number (constant 207)
2. `Lumax_GetPhysicalDevices` - Returns number of connected Lumax cards (via libftdi1 enumeration)
3. `Lumax_GetDeviceInfo` - Reserved for future developments
4. `Lumax_OpenDevice` - Opens a device and returns a handle (via libftdi1)
5. `Lumax_CloseDevice` - Closes a device (via libftdi1)
6. `Lumax_SendFrame` - Sends a laser frame to the card
7. `Lumax_WaitForBuffer` - Waits for buffer availability or returns its status
8. `Lumax_StopFrame` - Halts laser output (sets all color channels to 0, positions X/Y to center, disables TTL outputs)
9. `Lumax_SetTTL` - Sets TTL outputs
10. `Lumax_SetDmxMode` - Activates/deactivates DMX512 sender/receiver on the MiniLumax card
11. `Lumax_SendDmx` - Transmits DMX512 data to the MiniLumax card's output buffer
12. `Lumax_ReceiveDmx` - Receives DMX512 data from the MiniLumax card's input buffer
13. `Lumax_Dongle_XTEA_Com` - Performs dongle communication (XTEA encryption)
14. `Lumax_ExchangeDevInfo` - Exchanges device information
15. `Lumax_SetDacValue` - Sets DAC value (unknown purpose)

### Internal Functions (from liblumax.h)
- `openDev` - Opens the numDev-th connected card
- `openDevBySerial` - Low-level open of the card with given USB serial
- `lumax_init_device` - Full open handshake: reset, clear buffer, readID, readMemory, dongle exchange + flavor key, stop frame, DMX off, TTL off
- `clearBuffer` - Clears the buffer
- `checkIfBusy` - Checks if the device is busy
- `isOpen` - Checks if device is open
- `sendInitKey` - Sends initialization key
- `sendKey` - Sends flavor key
- `writeToDev` - Writes data to device
- `readFromDev` - Reads data from device
- `writeFrameBuffer` - Writes frame buffer to device
- `readID` - Reads device ID
- `readMemory` - Reads device memory
- `Lumax_SetBlankingDelay` - Sets blanking delay in milliseconds
- `dongleCom` - Low-level dongle communication

### Data Structures
- `struct lumax_device` - Represents an open Lumax card (opaque handle)
- `TLumax_Point` (from lumax.h) - Defines a laser point with 16-bit color channels (Ch1-Ch8, TTL)

## Version and Notes
- From `readme.txt` (v2.16): Bugfix for sporadic error #6 during HE-Laserscan when using multiple cards.
- Important note: Starting from version 2.13, the INI file structure changed. Sections are no longer named by USB-ID (e.g., `[FT5NG6UL]`) but by `[Projector_01]`, etc. The test program allows mapping projector numbers to output cards.
- The example VB project (`Form1.frm`) shows that the current API version (as of February 2008) is 207. If the returned version is less than 207, a warning is displayed.

## Dependencies
- The Lumax DLL depends on FTDI USB drivers (provided in `minilumax_usb_64`). Specifically, it uses the `ftd2xx.dll` library for USB communication with the FTDI chips on the Lumax cards.
- To build the liblumax library on Linux or macOS, the FTDI library (`libftdi1`) must be installed and available in the library search path.

## Usage Example (from VB6)
1. Call `Lumax_GetApiVersion` to verify DLL version.
2. Call `Lumax_GetPhysicalDevices` to get the number of connected cards.
3. For each device, call `Lumax_OpenDevice(DeviceIndex, 0)` to obtain a handle.
4. Prepare a frame by filling the `LumaxDatenfeld` array with `tLumax_Punkt` structures.
5. Use `Lumax_WaitForBuffer` to ensure the device is ready to accept new data.
6. Send the frame with `Lumax_SendFrame(handle, LumaxDatenfeld(0), numPoints, scanSpeed, updateMode, timeToWait)`.
7. To stop output, call `Lumax_StopFrame(handle)`.
8. When finished, call `Lumax_CloseDevice(handle)` for each opened device.

## Next Steps for Reverse Engineering
- We have created a stub library (`liblumax.c`) that now successfully compiles and links with libftdi1.
- We have analyzed and implemented `Lumax_GetApiVersion` (returns 207), `Lumax_GetPhysicalDevices` (enumerates Lumax devices via libftdi1), `Lumax_OpenDevice` (opens a device and returns a handle), and `Lumax_CloseDevice` (closes a device).
- Additional functions have been partially implemented: `Lumax_SetTTL`, `Lumax_StopFrame`, `Lumax_WaitForBuffer`, `Lumax_SetDmxMode`, `Lumax_SendDmx`, `Lumax_ReceiveDmx`, `Lumax_Dongle_XTEA_Com`.
- Now we will reverse engineer each remaining function from the provided `lumax.dll` and `minilumax_test/liblumax.so` using IDA Pro (MCP servers available on ports 13337 and 13338).
- For each function, we will:
  1. Analyze the disassembly and pseudo-code.
  2. Update the implementation in `liblumax.c`.
  3. Update the CLAUDE.md with our findings.
- Focus on how the DLL interacts with the FTDI driver (`ftd2xx.dll`) to perform USB I/O.
- Examine the frame sending process, buffer handling, and device initialization sequence.
- Consider the safety features (e.g., emergency stop via `Lumax_StopFrame`) and how they are implemented.
- Utilize the IDA Pro MCP servers running on ports 13337 and 13338 for advanced reverse engineering analysis.
- Check availability of MCP server with `curl -s http://127.0.0.1:13337/config.html 2>/dev/null || echo "IDA Pro MCP server not available"` or `curl -s http://127.0.0.1:13338/config.html 2>/dev/null || echo "IDA Pro MCP server not available"`. Don't use endpoint `/health`, as it does not exist.