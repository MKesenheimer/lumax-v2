# liblumax

A portable (Linux / macOS) reimplementation of the **MiniLumax** laser-show
controller library, reverse-engineered from the original `lumax.dll` /
`liblumax.so`. It drives Lumax laser cards over USB (FTDI FT2232H) through
`libftdi1`.

## Features

- Enumerate, open, and close one or more connected cards
- Send laser frames (multi-card supported — all cards can play the same frame)
- TTL outputs, buffer wait, and safety stop
- Supports both basic (flavor 1) and advanced (e.g. flavor 4) card flavors,
  including the advanced-card config and dongle handshake

## Requirements

- `libftdi1` (and its `libusb` dependency)

  - macOS: `brew install libftdi`
  - Linux: `pkg-config --exists libftdi1` (install your distro's `libftdi-dev` if missing)

## Build

```sh
make            # builds libs/liblumax.dylib (or .so on Linux) + python/libs copies
```

## Test

A test program lives in `test/`. Build and run it against a connected card:

```sh
cd test
gcc -o test -L../libs -llumax -I.. -Wl,-rpath,@loader_path/../libs main.c
./test
```

It opens every connected card and sends each the same frame. Set
`LUMAX_DEBUG=1` in the environment to see the protocol trace on stderr.

## Layout

| Path | Description |
|------|-------------|
| `liblumax.c` / `liblumax.h` | Library implementation and internal header |
| `lumax.h` | Public API (`Lumax_*` functions, `TLumax_Point`) |
| `Makefile` | Builds the shared library |
| `test/` | C test program |
| `python/` | ctypes bindings and demo scripts |
| `Example_VB/` | Original Visual Basic 6 example (reference) |
| `resources/` | Original DLL/`.so`, FTDI drivers, and reference files |

## API (public)

`Lumax_GetPhysicalDevices`, `Lumax_OpenDevice`, `Lumax_CloseDevice`,
`Lumax_SendFrame`, `Lumax_WaitForBuffer`, `Lumax_StopFrame`, `Lumax_SetTTL`,
`Lumax_SetDmxMode`, `Lumax_SendDmx`, `Lumax_ReceiveDmx`.

See `lumax.h` for signatures and `CLAUDE.md` for full reverse-engineering
notes.
