# Lumax Library Reverse Engineering Project

## Overview
This project involves reversing the Lumax DLL (`lumax.dll`) which controls Lumax laser show controllers via USB using FTDI chips. The DLL provides functions for device enumeration, opening/closing devices, sending laser frames, configuring TTL outputs, DMX512 communication, and more.

## Directory Contents
- `Example_VB/`: A Visual Basic 6 example project demonstrating how to use the Lumax DLL.
  - Contains `lumax.dll` (the library to reverse), `Lumax_dll.bas` (API declarations), `Form1.frm`, `Global.bas`, and a compiled example executable `Beispiel_Lumax_VB.exe`.
  - The example shows how to enumerate devices, open handles, send laser frames (drawing a circle), and handle device closing.

- `resources/`: Reverse-engineering resources and reference files.
  - `resources/minilumax_test/`: Original test directory with `minilumax.exe`, `lumax.dll`, `readme.txt`, `ML_Driver.mld`, and `liblumax.so` (Linux shared library).
  - `resources/minilumax_usb_64/`: FTDI USB driver files (both 32-bit and 64-bit) required for the Lumax DLL to function. Includes `ftd2xx.h`, DLLs (`ftd2xx.dll`, `ftlang.dll`, etc.), driver `.sys` and `.inf` files, and release documentation.

- `libs/`: Compiled shared libraries (output directory).
  - `liblumax.dylib` / `liblumax.so` - macOS / Linux shared library
  - `liblumax.o` - object file

- `test/`: Test programs.
  - `main.c` - Simple test program demonstrating usage of the Lumax API.

- `python/`: Python bindings for liblumax.
  - `python/lumaxlib.py` - ctypes wrapper for the C library
  - `python/demo.py` - Demo script showing laser frame generation
  - `python/demo_multi.py` - Multi-device demo
  - `python/libs/` - Copies of the shared library for Python to load

## Additional Files Provided
- `lumax.h`: C header with API definitions and TLumax_Point struct.
- `liblumax.h`: Internal header with helper functions and device structure.
- `liblumax.c`: Main implementation of the Lumax library.

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
3. `Lumax_GetDeviceInfo` - Reverse-engineered analysis shows this function queries device information from a global structure. It takes a PhysicalDevice index and optional parameters to return different types of device info:
   - With second parameter = 1: attempts to copy 9 bytes of device data to a provided buffer
   - With second parameter = 2: attempts to write a single byte value to a provided buffer
   - Otherwise: returns status based on device validity
   Currently reserved for future developments; returns 0 in our stub implementation.
4. `Lumax_OpenDevice` - Opens a device and returns a handle (via libftdi1)
5. `Lumax_CloseDevice` - Closes a device (via libftdi1)
6. `Lumax_SendFrame` - Sends a laser frame to the card
7. `Lumax_WaitForBuffer` - Waits for buffer availability or returns its status. Reverse-engineered analysis shows this function sends a 7-byte command to query buffer status and interprets the response to determine if the buffer has changed and calculate wait time. Implemented using libftdi1 write/read functions.
8. `Lumax_StopFrame` - Halts laser output (sets all color channels to 0, positions X/Y to center, disables TTL outputs)
9. `Lumax_SetTTL` - Reverse-engineered analysis shows this function sets TTL outputs by sending a 7-byte command [0x30, TTL, 0, 0, 0, 0, 0] and verifying the response equals (TTL XOR 0x1E). Implemented using libftdi1 write/read functions.
10. `Lumax_SetDmxMode` - Activates/deactivates DMX512 sender/receiver on the MiniLumax card. Reverse-engineered analysis shows this function sends two 7-byte commands (one for TX channels, one for RX channels) and verifies single-byte responses using checksums. Implemented using libftdi1 write/read functions.
11. `Lumax_SendDmx` - Transmits DMX512 data to the MiniLumax card's output buffer. Reverse-engineered analysis shows this function sends DMX data via the Tj4 helper function; implemented as a wrapper around Tj4 with appropriate parameters.
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

## Stream Timeout Prevention
1. Do each numbered task ONE AT A TIME. Complete one task fully, confirm it worked, then move to the next.
2. Never write a file longer than ~150 lines in a single tool call. If a file will be longer, write it in multiple append/edit passes.
3. Start a fresh session if the conversation gets long (20+ tool calls). The error gets worse as the session grows.
4. Keep individual grep/search outputs short. Use flags like `--include` and `-l` (list files only) to limit output size.
5. If you do hit the timeout, retry the same step in a shorter form. Don't repeat the entire task from scratch.

## Status: Live-Device Test (as of 2026-10-17)
A physical **"Basic"** MiniLumax card (FTDI FT2232H, type 3; serial `FTPY4YIU`; VID 0x403 PID 0xC88A; 3,000,000 baud) is connected, and the port now passes the full `main.c` flow against it (reliably, even when the card starts busy after a previous run):
`GetPhysicalDevices → OpenDevice(1,0) → SetTTL → WaitForBuffer → SendFrame(1000 pts) → WaitForBuffer → StopFrame → CloseDevice`, all return codes 0.

### Fixes landed this session (all compile clean; test green on an idle card)
- **`Lumax_SendFrame`** — rewritten to the exact reference protocol (verified line-for-line vs IDA 13337 `Lumax_SendFrame` @ 0x34b8). `NumOfPoints==0` → single centered point @ 1000 PPS (def_point: Ch1=Ch2=0x8000, rest 0). ScanSpeed clamped to `[250, MaxScanSpeed]`. Chunking: `total_chunks = NumOfPoints/MaxPoints` (+1 if remainder); the **first** chunk carries the remainder, the rest carry `MaxPoints`. Per point: 6-byte prefix `[(x>>8)+16*(y>>8), x&0xFF, y&0xFF, Ch3>>8, Ch4>>8, Ch5>>8]` with `x=(Ch1>>4)&0xFFF`, `y=(Ch2>>4)&0xFFF`, plus the flavor suffix (1: +TTL byte `(TTLAvailable&2)?TTLBuffer:(p->TTL&0xFF)`; 2: none; 4: +Ch8>>8; 8: +Ch8>>8,+Ch6>>8; 16: +Ch8>>8,+Ch6>>8,+Ch7>>8). Each chunk via `Tj4(Handle, chunk, 0x8000, n_bytes, counter, 0, 0)`. Frame-end command `[3, rem_lo, rem_hi, div_lo, div_hi, final_counter, cks]` with `divisor=FrameDivisor/pps`, `final_counter=NextLoopCounts+16*(total_chunks-1)`; reads a 4-byte reply `[cks, t_lo, t_hi, flags]` — `flags&0x80` → `TimeUntilFree = 1000*tuf/pps`, else 0; `TimeOffset = 1000*NumOfPoints/pps`; `LastDivisor=pps`; toggles `NextLoopCounts` 0↔8.
- **`Lumax_WaitForBuffer`** — gate fixed to `if ((dev->WaitGate & 8) == 0) return 0;` (was `TimeUntilFree`). Timeout mode sends `[4,1,0,0,0,0,5]`, reads 2 bytes, success iff `[5,1]`.
- **`Lumax_StopFrame`** — now `SendFrame(Handle,NULL,0,0,0,NULL)` then, on success, `WaitForBuffer(Handle,2000,NULL,NULL)` (was a wrong 3-byte command).
- **`Lumax_SetTTL`** — command `[0x1E, TTL, 0,0,0,0, TTL^0x1E]` (was 0x30, missing trailing checksum); response check `resp[0] == (TTL^0x1E)` unchanged.
- **`readID`** — g4jz: write `[0xC9,0,0,0,0,0,0xC9]`, wait 50 ms, drain; 1 byte = basic (`id[4]==0`), 18 bytes = advanced (16 data + 1 XOR checksum).
- **`readMemory`** — wp8o: write `[0xCA, start_lo, start_hi, len_lo, len_hi, 0, cks]`, read `len+2` bytes.
- **`Lumax_CloseDevice`** — return convention fixed to 0 on success / 1 on invalid-or-failed-close (was inverted).
- **`Tj4`** — `buffer_ptr` widened to `intptr_t` (64-bit pointer truncation).
- **`bnQQ` + `dev_read`** — now **accumulate** bytes across repeated `ftdi_read_data` until N bytes or timeout, matching D2XX `FT_Read` (a single `ftdi_read_data` is one bulk transfer that can return after the first USB packet).
- Added `DBG(...)` tracing (silent unless `LUMAX_DEBUG=1`) to `Tj4`, `StopFrame`, `SendFrame` frame-end, and `WaitForBuffer` timeout mode.
- **`main.c`** — added `Lumax_StopFrame(Handle)` before `Lumax_CloseDevice(Handle)` (the documented "correct" usage; each run now leaves the card idle, so back-to-back runs start from a known-free card).

### RESOLVED (2026-10-17) — rapid reopen failed ~50% (deterministic alternation)
`main.c` originally did **not** call `Lumax_StopFrame` before `Lumax_CloseDevice`, so the card was left playing the previous run's 1000-point frame and the next `OpenDevice` could catch it mid-playback. (That call has since been added to `main.c`; the library fix below is the real robustness change.)
- **Card model (verified with a single-process probe):** a card plays a frame **once** (~points/pps — ~100 ms for 1000 pts @ 10000 PPS) and then **self-clears to idle**; it does *not* loop. The timeout-mode query `[4,1,0,0,0,0,5]` reports BUSY (`05 00`) for the play window, then FREE (`05 01`).
- **Root cause:** the reference `WaitForBuffer(timeout)` is *single-shot* — it queries once and fails if the card is not already free, so it fails whenever it catches the card mid-drain; a short/misaligned read (just `05`, then a leftover byte making `01 05`) made it look stuck. The reference uses the `Timeout` arg only to select this mode, not as a wait duration.
- **Fix:** the port now *honors* the `Timeout`. In the timeout branch it purges stale RX/TX before each poll, writes the query, waits ~30 ms for the reply, reads 2 bytes, and retries every ~20 ms until the card reports `[5,1]` (free) or `Timeout` ms elapse. An idle card still answers on the first poll, so the common case is unchanged.
- **Result:** the full `main.c` flow passes **12/12** under rapid open/close (was 4/8), all return codes 0.

### RESOLVED (2026-10-18) — a *second* card ("FTP4PW27") opened intermittently / no laser output
A second physical card was connected (serial `FTP4PW27`, same VID/PID `0x403/0xC88A` as the working `FTPY4YIU`). It produced no laser output, and `OpenDevice` intermittently failed. It was suspected to be "an older flavor", but raw FTDI probes proved it is a **normal basic card**: it echoes the readID `0xC9` command on 15/15 tries within ~5 ms.
- **Root cause — a read-timing race, not a flavor difference.** On the macOS libftdi1 + libusb stack, `ftdi_read_data()` returns in ~1 ms **instead of blocking** for `usb_read_timeout` (measured: a read given a 400 ms budget returned in 0.2–2.7 ms). `readID` does a 50 ms settle then a *single* drain attempt; when the `0xC9` echo's USB delivery landed in the microsecond gap after that attempt, `readID` read 0 bytes and failed (the byte is *still in the buffer* a few ms later — it is never lost; a re-read catches it). The failure then poisoned the next read (StopFrame's frame-end read saw the stale echo as `[0xc9 0x86] != [0x86 0x55]`), so `lumax_init_device` failed → `OpenDevice` returned 0 → no frames sent → no laser. Before the fix ~1/5 runs failed; the previously-working card merely won the race more often.
- **Fix:** the three single-shot readers now **poll against a monotonic deadline** (honoring `usb_read_timeout` as a real wait) instead of trusting one non-blocking read: `readID` (wait up to ~300 ms for the echo, then a ~40 ms grace window for advanced-card follow-on bytes), and `bnQQ` / `dev_read` (re-poll until the expected byte count arrives or the budget elapses; `bnQQ`/`dev_read` had the same latent race that would have hit frame-end and DMX reads).
- **Result:** the full `main.c` flow passes **20/20** back-to-back, all return codes 0 (frame-end replies carry valid checksums, e.g. `bc 01 00 81`, `a6 01 00 81`); **both cards drive the show laser** and were confirmed producing output by the operator.

### RESOLVED (2026-10-18) — a *third* card ("LXPVB77T"): opened & responded, but **no laser** (advanced flavor 4)
A third physical card (serial `LXPVB77T`, `id[4]=0x0a`) connected. It opened cleanly and answered every status command (so "the device responds"), yet produced **no laser output**. This one is genuinely a different flavor — it is an **advanced "flavor 4"** card, not the basic flavor 1 of the other two.
- **Root cause — the advanced-card config was never applied.** `lumax_init_device` read the card's config memory but left `Flavor` at the default `1`, and `hjdd` **hard-failed** whenever `DmxFlavorMask` was set instead of sending the reference's 7-byte *flavor command*. So the card was never told it is flavor 4 and never configured; it accepted the connection but never armed the beam. Flavor 4 is 7 bytes/point like flavor 1, but its 7th byte is `Ch8>>8` (an extra channel) rather than a TTL control byte, and its `MaxScanSpeed` is 65000 (vs 70000).
- **Card memory (validated by `readMemory`'s XOR checksum):** `mem[9]=0x06` (DmxFlavorMask), `mem[10]=0x04` (Flavor), `mem[11]=0xFE` (WaitGate); `mem[1..8]` = the serial "LXPVB77T". With `(Flavor & Mask)=0x04 != 0xFF` the effective flavor is **4**.
- **Fix (ported verbatim from reference `Lumax_OpenDevice` @0x4b2b / `HJdd` @0x2a5a / `gKTU` @0x294c):**
  - `lumax_init_device` advanced block now applies the memory config: `TTLAvailable |= 1`, `DmxFlavorMask=mem[9]`, `Flavor=mem[10]`, `WaitGate=mem[11]`, the `(Flavor & Mask)==0xFF→4` rule, the `id[4]>10` divisor re-derive, **and** the dongle exchange `Lumax_DongleCom(handle,1,0x49D4B13A,...)`. (The reference nulls the FTDI handle on a dongle miss, aborting the open; this port runs the exchange for fidelity and logs the result but does not abort on a miss — here the dongle is present and the exchange returns 0.)
  - `hjdd` now sends the real flavor sequence instead of failing: the `gKTU` `0x09` reset (verified by a 1-byte `0x09` echo), then the 7-byte flavor command `[7, flavor, 0, maxpts_lo, maxpts_hi, 0, cks]` with `cks = maxpts_hi ^ maxpts_lo ^ flavor ^ 7`, verified against the 2-byte reply `[checksum, raw_flavor]`; the geometry (`Flavor`/`BytesPerPoint`/`MaxPoints`/`MaxScanSpeed`) is committed only on a matching reply.
- **Result (verified by operator):** init trace shows `id[4]=0x0a`, `Lumax_DongleCom(1)=0` (dongle OK), `hjdd flavor command ok (flavor=4 max_points=4500)`, geometry `flavor=4 BytesPerPoint=7 MaxPoints=4500 MaxScanSpeed=65000`, frame sent as **7000 bytes** (1000 × 7) and played (`TimeToWait≈99`). **All three cards now drive the show laser**; output confirmed positive by the operator.

### REMAINING (all optional)
1. (Optional cleanup) Drop the `DBG(...)` instrumentation in `Tj4`/`StopFrame`/`SendFrame`/`WaitForBuffer` (silent unless `LUMAX_DEBUG=1`); remove unused statics (`dev_read_available`, `find_device_by_serial`, `find_device_by_index`) and the unused `int tmp;` in `Lumax_SetDmxMode` if `-Wunused` warnings matter.

## Next Steps for Reverse Engineering
- Core protocol work is complete and verified against a live Basic card, and the busy-on-reopen issue is **RESOLVED** (see Status above). Only optional cleanup remains.
- We have implemented `ghrkr` (the one-time lazy global constructor, guarded like the .so's `Constructor_State`). In `liblumax.so` it `malloc`s the ~10.6 MB `m_38[]` array and, for each of the 8 device slots, writes fallback config (chunk size field 1191 = 31500/7 = 4500, max scan speed field 1206 = 45000, buffer layout field 1204 = 1) plus the per-flavor scan-speed preset tables. This port keeps its device records in the static `open_devices[]`/`detected_devices[]` arrays (no malloc) and does not model the preset tables (`writeFrameBuffer` is a raw `ftdi_write_data`), so `ghrkr` reduces to: apply `apply_fallback_config()` to the open-device slots once. The fallbacks are what `Lumax_SendFrame` needs to stay safe (it divides by `MaxPoints` and clamps to `MaxScanSpeed`); `lumax_init_device` overrides them from the card's memory read, and `Lumax_OpenDevice` now calls `apply_fallback_config` after init rather than zeroing the config (previously it clobbered `MaxPoints`/`MaxScanSpeed`/`BufferLayout` back to 0, which made `SendFrame` reject every frame).
- (Done) FTDI I/O, frame sending, buffer handling, device-init sequence, and the safety stop are all ported and exercised against the live card — see the Status section above.
- Utilize the IDA Pro MCP servers running on ports 13337 (liblumax.so — primary, readable) and 13338 (lumax.dll — obfuscated) for advanced reverse engineering analysis.
- Check availability of MCP server with `curl -s http://127.0.0.1:13337/config.html 2>/dev/null || echo "IDA Pro MCP server not available"` or `curl -s http://127.0.0.1:13338/config.html 2>/dev/null || echo "IDA Pro MCP server not available"`. Don't use endpoint `/health`, as it does not exist.