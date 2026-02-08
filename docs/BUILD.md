# Building Pinkcoin

This document covers building Pinkcoin for Linux and Windows from a Debian/Ubuntu-based system.

## Prerequisites

### Linux Build Dependencies

```bash
sudo apt install -y build-essential cmake ninja-build \
    libboost-all-dev libssl-dev libdb++-dev zlib1g-dev \
    qtbase5-dev qt5-qmake libqt5svg5-dev libqt5dbus5 \
    libminiupnpc-dev libqrencode-dev
```

### Windows Cross-Compilation (MXE)

MXE (M Cross Environment) is required for building Windows binaries from Linux.

#### 1. Install MXE build prerequisites

```bash
sudo apt install -y autoconf automake autopoint bash bison bzip2 flex \
    g++ g++-multilib gettext git gperf intltool libc6-dev-i386 \
    libgdk-pixbuf2.0-dev libltdl-dev libgl-dev libpcre3-dev libssl-dev \
    libtool-bin libxml-parser-perl lzip make openssl p7zip-full patch \
    perl python3 python3-mako python3-packaging python-is-python3 \
    ruby sed unzip wget xz-utils
```

#### 2. Clone and build MXE

```bash
sudo git clone https://github.com/mxe/mxe.git /opt/mxe
cd /opt/mxe
sudo make MXE_TARGETS='x86_64-w64-mingw32.static' boost openssl zlib qt5
```

This builds the MinGW cross-compiler and all required libraries (Boost, OpenSSL, zlib, Qt5) as static Windows libraries. Qt5 pulls in many transitive dependencies automatically.

#### 3. Symlink MXE to expected path

The CMake toolchain expects MXE at `/usr/lib/mxe`:

```bash
sudo ln -s /opt/mxe /usr/lib/mxe
```

#### 4. Build BerkeleyDB 4.8 for Windows

MXE ships with BerkeleyDB 6.x, which is **not compatible** with existing wallet.dat files. A custom build of BerkeleyDB 4.8.30 is required:

```bash
cd contrib/mxe
MXE_ROOT=/opt/mxe ./build-bdb48.sh
```

This downloads, patches, cross-compiles, and installs BDB 4.8.30 to `/opt/mxe-bdb48`.

---

## Build Targets

All builds use CMake presets. Run commands from the project root.

**Note:** The `-j` flag for parallel jobs is optional. Omitting it uses single-threaded compilation, which is recommended for systems with limited resources or when running multiple tasks.

### Linux Daemon (headless)

```bash
cmake --preset linux-daemon-only
cmake --build build/linux-daemon-only
```

**Output:** `build/linux-daemon-only/src/pink2d`

### Linux GUI Wallet

QT5 translation dependencies:

```bash
sudo apt-get install qttools5-dev-tools qttools5-dev
```

```bash
cmake --preset linux-release
cmake --build build/linux-release
```

**Output:** `build/linux-release/src/qt/Pinkcoin-Qt`

The daemon is also built: `build/linux-release/src/pink2d`

### Windows Daemon (headless)

Requires MXE setup (see Prerequisites above).

```bash
cmake --preset windows-mxe-daemon
cmake --build build/windows-mxe-daemon
```

**Output:** `build/windows-mxe-daemon/src/pink2d.exe`

### Windows GUI Wallet

Requires MXE setup with Qt5 (see Prerequisites above).

```bash
cmake --preset windows-mxe
cmake --build build/windows-mxe
```

**Output:** `build/windows-mxe/src/qt/Pinkcoin-Qt.exe`

The daemon is also built: `build/windows-mxe/src/pink2d.exe`

---

## Running Tests (Linux only)

Unit tests are built with the `linux-release` preset.

```bash
cmake --preset linux-release
cmake --build build/linux-release
cd build/linux-release
ctest --output-on-failure
```

Or run the test binary directly:

```bash
./build/linux-release/src/test/test_pinkcoin --log_level=test_suite
```

---

## Clean Rebuild

**IMPORTANT:** The `--clean-first` flag is not always reliable. For a guaranteed clean rebuild, manually delete the build directory contents:

### Linux Clean Rebuild

```bash
cd build/linux-release
rm -rf *
cmake --preset linux-release ../..
cmake --build .
```

### Windows Clean Rebuild

```bash
cd build/windows-mxe
rm -rf *
cmake --preset windows-mxe ../..
cmake --build .
```

### Verify Build Freshness

Always check timestamps after a rebuild to confirm executables were actually rebuilt:

```bash
# Linux
ls -la build/linux-release/src/qt/Pinkcoin-Qt
ls -la build/linux-release/src/pink2d

# Windows
ls -la build/windows-mxe/src/qt/Pinkcoin-Qt.exe
ls -la build/windows-mxe/src/pink2d.exe
```

Compare timestamps to the current time (`date`) to verify the build is fresh.

---

## Stripping Binaries (optional)

Strip debug symbols to reduce binary size for release:

```bash
# Linux
strip build/linux-release/src/qt/Pinkcoin-Qt
strip build/linux-daemon-only/src/pink2d

# Windows (use MXE strip)
/opt/mxe/usr/bin/x86_64-w64-mingw32.static-strip build/windows-mxe/src/qt/Pinkcoin-Qt.exe
/opt/mxe/usr/bin/x86_64-w64-mingw32.static-strip build/windows-mxe-daemon/src/pink2d.exe
```

---

## Notes

- **BerkeleyDB version** is pinned to 4.8 for wallet.dat backwards compatibility. Do not use BDB 5.x or 6.x.
- **OpenSSL 3.0** deprecation warnings are expected and harmless. The deprecated APIs still function correctly.
- **WSL2 users:** Build on the Linux native filesystem (`~/`) rather than `/mnt/` to avoid permission errors during CMake configuration.
- **macOS:** See `CMAKE_MIGRATION_PLAN.md` for macOS-specific instructions (requires macOS hardware).
- **ARM64 / Raspberry Pi:** Build natively on the device using the `linux-daemon-only` preset with the same Linux dependencies.
- **Parallel builds:** Use `-j N` where N is the number of parallel jobs (e.g., `-j 2`). Omit for single-threaded builds on resource-constrained systems.
