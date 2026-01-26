# Cross-compiling Pinkcoin for Windows using MXE

This directory contains scripts and documentation for cross-compiling
Pinkcoin for Windows from Linux using MXE (M Cross Environment).

## Prerequisites

### MXE Installation

MXE should be installed with the following packages:
- `boost`
- `openssl`
- `zlib`

On Debian/Ubuntu with MXE from packages:
```bash
sudo apt-get install mxe-x86-64-w64-mingw32.static-boost
sudo apt-get install mxe-x86-64-w64-mingw32.static-openssl
sudo apt-get install mxe-x86-64-w64-mingw32.static-zlib
```

Or build from source: https://mxe.cc/

### BerkeleyDB 4.8.30

**IMPORTANT:** MXE ships with BerkeleyDB 6.x, which is NOT compatible with
wallet.dat files from older versions. For backwards compatibility, you MUST
use BerkeleyDB 4.8.30.

Build BDB 4.8 using the provided script:
```bash
./build-bdb48.sh
```

This will:
1. Download BerkeleyDB 4.8.30 from Oracle
2. Apply MinGW compatibility patches
3. Cross-compile for Windows using MXE
4. Install to `/opt/mxe-bdb48`

### Build Tools

```bash
sudo apt-get install cmake ninja-build
```

## Building

### Headless Daemon Only

```bash
# Configure
cmake --preset windows-mxe-daemon

# Build
cmake --build build/windows-mxe-daemon

# Strip (optional, reduces size)
/usr/lib/mxe/usr/bin/x86_64-w64-mingw32.static-strip \
    build/windows-mxe-daemon/src/pink2d.exe
```

Output: `build/windows-mxe-daemon/src/pink2d.exe`

### Full Build (with Qt GUI)

*Note: Requires Qt5 built for MXE, which is not covered here.*

```bash
cmake --preset windows-mxe
cmake --build build/windows-mxe
```

## CMake Presets

| Preset | Description |
|--------|-------------|
| `windows-mxe-daemon` | Headless daemon only, no GUI |
| `windows-mxe` | Full build with Qt GUI |

## Toolchain Configuration

The MXE toolchain file is located at:
`cmake/toolchains/mxe-x86_64.cmake`

Key settings:
- MXE root: `/usr/lib/mxe` (configurable via `MXE_ROOT`)
- BDB 4.8 path: `/opt/mxe-bdb48` (required for wallet compatibility)
- Target: `x86_64-w64-mingw32.static` (64-bit, static linking)

## Dependencies Summary

| Library | Version | Source |
|---------|---------|--------|
| Boost | 1.60+ | MXE |
| OpenSSL | 1.1.x | MXE |
| zlib | 1.2.x | MXE |
| BerkeleyDB | **4.8.30** | Custom build (`/opt/mxe-bdb48`) |

## Wallet Compatibility Note

BerkeleyDB version is critical for wallet.dat compatibility:
- BDB 4.8: Standard for Bitcoin-derived cryptocurrencies
- BDB 5.x/6.x: NOT backwards compatible

Wallets created with BDB 6.x cannot be read by clients built with BDB 4.8.
Always use BDB 4.8.30 for production builds.
