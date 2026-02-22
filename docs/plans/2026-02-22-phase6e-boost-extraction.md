# Phase 6E: Boost Extraction & Serialization Modernization — Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Eliminate all Boost usage from production code (keeping Boost.Test only) and replace the IMPLEMENT_SERIALIZE macro with explicit member functions across all 32 classes.

**Architecture:** Four work streams executed sequentially — standalone Asio migration, platform replacements (file lock + Qt IPC), serialization modernization (non-consensus then consensus), and final cleanup (PushMessage + CMake). Each stream is independently verifiable.

**Tech Stack:** Standalone Asio (header-only, vendored), std::streambuf, POSIX/Win32 file locking, QLocalServer/QLocalSocket, C++17 fold expressions.

---

## Build Strategy

| After Task | Build Checkpoint |
|-----------|-----------------|
| Task 1 | Linux only (Asio compiles) |
| Task 3 | Linux + Windows (all Asio migration done) |
| Task 5 | Linux + Windows (file lock + Qt IPC done) |
| Task 7 | Both builds + all tests (non-consensus serialization done) |
| Task 9 | Both builds + all tests + golden tests (consensus serialization done) |
| Task 11 | Final verification (PushMessage + cleanup, both builds, all tests) |

---

## Stream 1: Standalone Asio Migration

### Task 1: Vendor Standalone Asio and Create CMake Target

**Files:**
- Create: `src/asio/` directory with vendored Asio headers
- Create: `src/asio/CMakeLists.txt`
- Modify: `src/CMakeLists.txt` — link `standalone_asio` instead of `Boost::system`/`Boost::chrono`
- Modify: `CMakeLists.txt` (root) — remove `system` and `chrono` from Boost OPTIONAL_COMPONENTS

**Step 1: Download and vendor standalone Asio**

Download Asio 1.30.2 (or latest stable) from https://think-async.com/Asio/. Extract only the `asio/include/asio.hpp` and `asio/include/asio/` directory into `src/asio/include/`.

The directory structure should be:
```
src/asio/
  CMakeLists.txt
  include/
    asio.hpp
    asio/
      ... (all asio headers)
```

**Step 2: Create CMakeLists.txt**

```cmake
# Standalone Asio — header-only networking library
add_library(standalone_asio INTERFACE)
target_include_directories(standalone_asio INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)

# Standalone Asio requires these defines
target_compile_definitions(standalone_asio INTERFACE
    ASIO_STANDALONE
    ASIO_NO_DEPRECATED
)
```

**Step 3: Update src/CMakeLists.txt**

In `target_link_libraries(pinkcoin_core PUBLIC ...)`:
- Remove `$<$<TARGET_EXISTS:Boost::system>:Boost::system>`
- Remove `$<$<TARGET_EXISTS:Boost::chrono>:Boost::chrono>`
- Add `standalone_asio`

**Step 4: Update root CMakeLists.txt**

Change:
```cmake
find_package(Boost 1.55 REQUIRED COMPONENTS
    unit_test_framework
    OPTIONAL_COMPONENTS
    system
    chrono
)
```
To:
```cmake
find_package(Boost 1.55 REQUIRED COMPONENTS
    unit_test_framework
)
```

Also add `add_subdirectory(src/asio)` alongside the existing `add_subdirectory(src/json)`.

**Step 5: Verify Linux build compiles**

```bash
rm -rf build/linux-release && cmake --preset linux-release && cmake --build build/linux-release
```

Expected: Build succeeds (Asio not yet used, just linked).

**Step 6: Commit**

```bash
git add src/asio/ src/CMakeLists.txt CMakeLists.txt
git commit -m "Phase 6E: Vendor standalone Asio and create CMake target"
```

---

### Task 2: Replace SSLIOStreamDevice with std::streambuf

**Files:**
- Create: `src/asio_stream.h` — `AsioSSLStreamBuf` class
- Modify: `src/rpc/bitcoinrpc.cpp` — replace SSLIOStreamDevice

**Step 1: Create src/asio_stream.h**

This replaces the boost::iostreams-based `SSLIOStreamDevice` with a `std::streambuf`-based implementation that works with standalone Asio:

```cpp
#ifndef ASIO_STREAM_H
#define ASIO_STREAM_H

#include <streambuf>
#include <iostream>
#include <asio.hpp>
#include <asio/ssl.hpp>

// std::streambuf adapter for Asio SSL streams.
// Replaces the boost::iostreams SSLIOStreamDevice.
template <typename Protocol>
class AsioSSLStreamBuf : public std::streambuf {
public:
    AsioSSLStreamBuf(asio::ssl::stream<typename Protocol::socket>& streamIn, bool fUseSSLIn)
        : stream(streamIn), fUseSSL(fUseSSLIn), fNeedHandshake(fUseSSLIn)
    {
        // Set up get buffer with one putback char
        setg(inBuf + 1, inBuf + 1, inBuf + 1);
    }

    void handshake(asio::ssl::stream_base::handshake_type role)
    {
        if (!fNeedHandshake) return;
        fNeedHandshake = false;
        stream.handshake(role);
    }

    bool connect(const std::string& server, const std::string& port)
    {
        asio::ip::tcp::resolver resolver(stream.lowest_layer().get_executor());
        auto results = resolver.resolve(server, port);
        asio::error_code error = asio::error::host_not_found;
        for (const auto& endpoint : results)
        {
            stream.lowest_layer().close();
            stream.lowest_layer().connect(endpoint, error);
            if (!error)
                break;
        }
        return !error;
    }

protected:
    // Called when the get buffer is exhausted
    int_type underflow() override
    {
        handshake(asio::ssl::stream_base::server);
        asio::error_code ec;
        size_t n;
        if (fUseSSL)
            n = stream.read_some(asio::buffer(inBuf + 1, sizeof(inBuf) - 1), ec);
        else
            n = stream.next_layer().read_some(asio::buffer(inBuf + 1, sizeof(inBuf) - 1), ec);
        if (ec || n == 0)
            return traits_type::eof();
        setg(inBuf, inBuf + 1, inBuf + 1 + n);
        return traits_type::to_int_type(inBuf[1]);
    }

    // Called for each character written (overflow) or for sync
    int_type overflow(int_type ch) override
    {
        if (ch != traits_type::eof()) {
            char c = traits_type::to_char_type(ch);
            handshake(asio::ssl::stream_base::client);
            asio::error_code ec;
            if (fUseSSL)
                asio::write(stream, asio::buffer(&c, 1), ec);
            else
                asio::write(stream.next_layer(), asio::buffer(&c, 1), ec);
            if (ec) return traits_type::eof();
        }
        return ch;
    }

    std::streamsize xsputn(const char* s, std::streamsize n) override
    {
        handshake(asio::ssl::stream_base::client);
        asio::error_code ec;
        if (fUseSSL)
            asio::write(stream, asio::buffer(s, n), ec);
        else
            asio::write(stream.next_layer(), asio::buffer(s, n), ec);
        return ec ? 0 : n;
    }

private:
    asio::ssl::stream<typename Protocol::socket>& stream;
    bool fUseSSL;
    bool fNeedHandshake;
    char inBuf[4096 + 1]; // +1 for putback
};

#endif // ASIO_STREAM_H
```

**Step 2: Test that this compiles**

Build Linux — the header is not yet included anywhere, but verify no syntax errors by adding a temporary include in a test file, or just proceed to Task 3.

**Step 3: Commit**

```bash
git add src/asio_stream.h
git commit -m "Phase 6E: Add AsioSSLStreamBuf — std::streambuf replacement for boost::iostreams"
```

---

### Task 3: Migrate bitcoinrpc.cpp from Boost to Standalone Asio

**Files:**
- Modify: `src/rpc/bitcoinrpc.cpp`

This is the big migration. All changes are mechanical namespace/type swaps.

**Step 1: Replace includes**

Remove:
```cpp
#include <boost/asio.hpp>
#include <boost/asio/ip/v6_only.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/iostreams/concepts.hpp>
#include <boost/iostreams/stream.hpp>
#include <boost/asio/ssl.hpp>
```

Add:
```cpp
#include <asio.hpp>
#include <asio/ssl.hpp>
#include <memory>
#include "asio_stream.h"
```

**Step 2: Remove compatibility macros**

Remove the entire `#if BOOST_VERSION >= 107000` block (lines 27-37). Replace with:
```cpp
// Standalone Asio helper — get io_context from executor
#define GetIOService(s)        ((asio::io_context&)(s).get_executor().context())
#define GetIOServiceFromPtr(s) ((asio::io_context&)(s->get_executor().context()))
using ioContext = asio::io_context;
```

**Step 3: Remove old namespace aliases**

The file uses `using namespace` for asio sub-namespaces. Find the `using namespace` declarations near the top (after includes) and update them. There may be lines like:
```cpp
using namespace boost;
using namespace boost::asio;
```
Replace with:
```cpp
using namespace asio;
```

If there are no explicit `using namespace` lines, then all `boost::asio::` prefixes must be replaced inline.

**Step 4: Replace SSLIOStreamDevice with AsioSSLStreamBuf**

Remove the entire `SSLIOStreamDevice` class (~50 lines).

Find `AcceptedConnectionImpl` which uses it. It will have a member like:
```cpp
iostreams::stream< SSLIOStreamDevice<Protocol> > _stream;
```
Replace with:
```cpp
AsioSSLStreamBuf<Protocol> _streambuf;
std::iostream _stream;
```
And update the constructor to initialize: `_stream(&_streambuf)`.

Also update the `stream()` method to return `_stream` reference.

**Step 5: Replace boost::shared_ptr with std::shared_ptr**

Search and replace throughout the file:
- `boost::shared_ptr<` → `std::shared_ptr<`
- `boost::shared_ptr <` → `std::shared_ptr<` (if any spacing variants)

Also change the `new` allocation pattern:
```cpp
// Before:
boost::shared_ptr<ip::tcp::acceptor> acceptor(new ip::tcp::acceptor(io_service));
// After:
auto acceptor = std::make_shared<ip::tcp::acceptor>(io_service);
```

**Step 6: Replace boost::system::error_code**

Search and replace:
- `boost::system::error_code` → `asio::error_code`
- `boost::system::system_error` → `asio::system_error`

**Step 7: Replace remaining boost:: qualifiers**

Search for any remaining `boost::` in the file. Replace:
- `boost::asio::` → `asio::` (if any qualified references remain)
- Remove any `#include <boost/...>` that remain

**Step 8: Build both targets**

```bash
rm -rf build/linux-release && cmake --preset linux-release && cmake --build build/linux-release
rm -rf build/windows-mxe && cmake --preset windows-mxe && cmake --build build/windows-mxe
```

Fix any compilation errors. Common issues:
- Standalone Asio may need `ASIO_HAS_STD_INVOKE_RESULT` on some compilers
- MXE may need `_WIN32_WINNT` defined for Asio Windows headers
- The `GetIOService` macro may need adjustment if executor API differs

**Step 9: Run tests**

```bash
cd build/linux-release && ctest --output-on-failure
```

All 1,612+ tests must pass.

**Step 10: Commit**

```bash
git add src/rpc/bitcoinrpc.cpp
git commit -m "Phase 6E: Migrate bitcoinrpc.cpp from boost::asio to standalone Asio"
```

---

## Stream 2: Platform Replacements

### Task 4: Replace boost::interprocess::file_lock

**Files:**
- Create: `src/filelock.h`
- Modify: `src/init.cpp`
- Create: `src/test/filelock_tests.cpp` (optional — if init_tests don't cover it)
- Modify: `src/test/CMakeLists.txt` (if adding test file)

**Step 1: Create src/filelock.h**

```cpp
#ifndef FILELOCK_H
#define FILELOCK_H

#include <string>
#include <stdexcept>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/file.h>
#include <fcntl.h>
#include <unistd.h>
#endif

// Cross-platform file lock — replaces boost::interprocess::file_lock.
// RAII: lock acquired in try_lock(), released in destructor.
class FileLock {
public:
    explicit FileLock(const std::string& path)
#ifdef _WIN32
        : hFile(CreateFileA(path.c_str(), GENERIC_READ | GENERIC_WRITE,
                FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr))
    {
        if (hFile == INVALID_HANDLE_VALUE)
            throw std::runtime_error("FileLock: cannot open " + path);
    }
#else
        : fd(open(path.c_str(), O_RDWR | O_CREAT, 0600))
    {
        if (fd < 0)
            throw std::runtime_error("FileLock: cannot open " + path);
    }
#endif

    ~FileLock()
    {
#ifdef _WIN32
        if (locked) {
            OVERLAPPED ov = {};
            UnlockFileEx(hFile, 0, 1, 0, &ov);
        }
        if (hFile != INVALID_HANDLE_VALUE)
            CloseHandle(hFile);
#else
        if (locked)
            flock(fd, LOCK_UN);
        if (fd >= 0)
            close(fd);
#endif
    }

    // Non-blocking try-lock. Returns true if lock acquired.
    bool try_lock()
    {
#ifdef _WIN32
        OVERLAPPED ov = {};
        locked = LockFileEx(hFile, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY,
                           0, 1, 0, &ov) != 0;
#else
        locked = (flock(fd, LOCK_EX | LOCK_NB) == 0);
#endif
        return locked;
    }

    // Non-copyable, non-movable
    FileLock(const FileLock&) = delete;
    FileLock& operator=(const FileLock&) = delete;

private:
    bool locked = false;
#ifdef _WIN32
    HANDLE hFile;
#else
    int fd;
#endif
};

#endif // FILELOCK_H
```

**Step 2: Modify init.cpp**

Remove:
```cpp
#include <boost/interprocess/sync/file_lock.hpp>
```

Add:
```cpp
#include "filelock.h"
```

Find the lock usage (around line 583):
```cpp
static boost::interprocess::file_lock lock(pathLockFile.string().c_str());
if (!lock.try_lock())
```

Replace with:
```cpp
static FileLock lock(pathLockFile.string());
if (!lock.try_lock())
```

**Step 3: Build both targets and run tests**

```bash
rm -rf build/linux-release && cmake --preset linux-release && cmake --build build/linux-release
rm -rf build/windows-mxe && cmake --preset windows-mxe && cmake --build build/windows-mxe
cd build/linux-release && ctest --output-on-failure
```

**Step 4: Commit**

```bash
git add src/filelock.h src/init.cpp
git commit -m "Phase 6E: Replace boost::interprocess::file_lock with cross-platform FileLock"
```

---

### Task 5: Replace boost::interprocess::message_queue with Qt IPC

**Files:**
- Modify: `src/qt/qtipcserver.h`
- Modify: `src/qt/qtipcserver.cpp`

**Step 1: Rewrite qtipcserver.h**

```cpp
#ifndef QTIPCSERVER_H
#define QTIPCSERVER_H

// Qt-based IPC for passing pinkcoin: URIs between instances.
// Replaces boost::interprocess::message_queue.

void ipcInit(int argc, char *argv[]);
void ipcScanRelay(int argc, char *argv[]);

extern const int MAX_URI_LENGTH;

#define BITCOINURI_QUEUE_NAME "PinkcoinURI"

#endif // QTIPCSERVER_H
```

**Step 2: Rewrite qtipcserver.cpp**

Replace the entire file. Remove all boost includes and replace with QLocalServer/QLocalSocket:

```cpp
// Copyright (c) 2024-2026 The Pinkcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "qtipcserver.h"
#include "ui_interface.h"
#include "util.h"
#include "string_utils.h"

#include <QLocalServer>
#include <QLocalSocket>
#include <string>

const int MAX_URI_LENGTH = 255;
static QLocalServer* ipcServer = nullptr;

// Try to send a URI to an already-running instance.
// Returns true if another instance received it.
static bool ipcScanCmd(int argc, char *argv[], bool fRelay)
{
    bool fSent = false;
    for (int i = 1; i < argc; i++)
    {
        if (strutil::istarts_with(argv[i], "pinkcoin:"))
        {
            QLocalSocket socket;
            socket.connectToServer(BITCOINURI_QUEUE_NAME, QIODevice::WriteOnly);
            if (socket.waitForConnected(500))
            {
                QByteArray data(argv[i]);
                socket.write(data);
                socket.waitForBytesWritten(1000);
                socket.disconnectFromServer();
                fSent = true;
            }
            else if (fRelay)
                break;
        }
    }
    return fSent;
}

void ipcScanRelay(int argc, char *argv[])
{
    if (ipcScanCmd(argc, argv, true))
        exit(0);
}

void ipcInit(int argc, char *argv[])
{
    // Remove any stale server
    QLocalServer::removeServer(BITCOINURI_QUEUE_NAME);

    ipcServer = new QLocalServer();
    if (!ipcServer->listen(BITCOINURI_QUEUE_NAME))
    {
        printf("ipcInit() - failed to listen on %s: %s\n",
               BITCOINURI_QUEUE_NAME,
               ipcServer->errorString().toStdString().c_str());
        delete ipcServer;
        ipcServer = nullptr;
        return;
    }

    QObject::connect(ipcServer, &QLocalServer::newConnection, [&]() {
        QLocalSocket* client = ipcServer->nextPendingConnection();
        if (!client) return;

        QObject::connect(client, &QLocalSocket::readyRead, [client]() {
            QByteArray data = client->readAll();
            if (data.size() > 0 && data.size() <= MAX_URI_LENGTH)
            {
                std::string uri(data.constData(), data.size());
                uiInterface.ThreadSafeHandleURI(uri);
            }
            client->disconnectFromServer();
            client->deleteLater();
        });

        // Timeout: disconnect if no data within 2 seconds
        QTimer::singleShot(2000, client, [client]() {
            if (client->state() != QLocalSocket::UnconnectedState) {
                client->disconnectFromServer();
                client->deleteLater();
            }
        });
    });

    // Process any URIs passed on command line
    ipcScanCmd(argc, argv, false);
}
```

Note: This file is compiled only for the Qt GUI target, not the daemon or tests. The Qt build links QNetwork automatically.

**Step 3: Check Qt CMakeLists.txt**

Read `src/qt/CMakeLists.txt` — verify `QLocalServer`/`QLocalSocket` are available from Qt5::Network. If not already linked, add:
```cmake
target_link_libraries(Pinkcoin-Qt PRIVATE Qt5::Network)
```

**Step 4: Build both targets**

```bash
rm -rf build/linux-release && cmake --preset linux-release && cmake --build build/linux-release
rm -rf build/windows-mxe && cmake --preset windows-mxe && cmake --build build/windows-mxe
```

Fix compilation issues. Common: QTimer needs `#include <QTimer>`.

**Step 5: Run tests**

```bash
cd build/linux-release && ctest --output-on-failure
```

**Step 6: Commit**

```bash
git add src/qt/qtipcserver.h src/qt/qtipcserver.cpp
git commit -m "Phase 6E: Replace boost::interprocess message_queue with Qt IPC"
```

---

## Stream 3: Serialization Modernization

### Task 6: Add Missing Golden Tests

Before touching any serialization code, pin the byte layout of classes not yet covered.

**Files:**
- Modify: `src/test/golden_tests.cpp`

**Step 1: Add golden tests for CMerkleTx, CAlert, CSyncCheckpoint**

Append to golden_tests.cpp, in a new section:

```cpp
// ============================================================================
// Section 10: Pre-Serialization-Migration Pins
// ============================================================================

BOOST_AUTO_TEST_CASE(golden_cmerkletx)
{
    // CMerkleTx = CTransaction + hashBlock + vMerkleBranch + nIndex
    CMerkleTx mtx;
    mtx.nVersion = 1;
    mtx.nTime = 1000;
    mtx.nLockTime = 0;
    mtx.hashBlock = uint256("0x00000000000000001");
    mtx.nIndex = 5;
    // Leave vMerkleBranch empty, vin/vout empty

    std::string hex = SerializeToHex(mtx);
    CMerkleTx mtx2 = DeserializeFromHex<CMerkleTx>(hex);
    BOOST_CHECK_EQUAL(mtx2.hashBlock.GetHex(), mtx.hashBlock.GetHex());
    BOOST_CHECK_EQUAL(mtx2.nIndex, 5);
    BOOST_CHECK_EQUAL(mtx2.nTime, 1000u);

    // Pin the size: empty tx (10 bytes) + hashBlock (32) + vMerkleBranch count (1) + nIndex (4) = 47
    BOOST_CHECK_EQUAL(hex.size() / 2, 47u);
}

BOOST_AUTO_TEST_CASE(golden_cunsignedalert)
{
    CUnsignedAlert alert;
    alert.nVersion = 1;
    alert.nRelayUntil = 1000000;
    alert.nExpiration = 2000000;
    alert.nID = 42;
    alert.nCancel = 0;
    alert.nMinVer = 60016;
    alert.nMaxVer = 60019;
    alert.nPriority = 100;
    alert.strComment = "test";
    alert.strStatusBar = "alert!";
    alert.strReserved = "";

    std::string hex = SerializeToHex(alert);
    CUnsignedAlert alert2 = DeserializeFromHex<CUnsignedAlert>(hex);
    BOOST_CHECK_EQUAL(alert2.nID, 42);
    BOOST_CHECK_EQUAL(alert2.nMinVer, 60016);
    BOOST_CHECK_EQUAL(alert2.nMaxVer, 60019);
    BOOST_CHECK_EQUAL(alert2.strStatusBar, "alert!");

    // Round-trip integrity
    std::string hex2 = SerializeToHex(alert2);
    BOOST_CHECK_EQUAL(hex, hex2);
}

BOOST_AUTO_TEST_CASE(golden_calert)
{
    CAlert alert;
    alert.vchMsg = {0x01, 0x02, 0x03};
    alert.vchSig = {0xAA, 0xBB};

    std::string hex = SerializeToHex(alert);
    CAlert alert2 = DeserializeFromHex<CAlert>(hex);
    BOOST_CHECK(alert2.vchMsg == alert.vchMsg);
    BOOST_CHECK(alert2.vchSig == alert.vchSig);
}

BOOST_AUTO_TEST_CASE(golden_cunsignedsynccheckpoint)
{
    CUnsignedSyncCheckpoint cp;
    cp.nVersion = 1;
    cp.hashCheckpoint = uint256("0x00000000000000abc");

    std::string hex = SerializeToHex(cp);
    CUnsignedSyncCheckpoint cp2 = DeserializeFromHex<CUnsignedSyncCheckpoint>(hex);
    BOOST_CHECK_EQUAL(cp2.nVersion, 1);
    BOOST_CHECK_EQUAL(cp2.hashCheckpoint.GetHex(), cp.hashCheckpoint.GetHex());
}

BOOST_AUTO_TEST_CASE(golden_csynccheckpoint)
{
    CSyncCheckpoint cp;
    cp.vchMsg = {0xDE, 0xAD};
    cp.vchSig = {0xBE, 0xEF};

    std::string hex = SerializeToHex(cp);
    CSyncCheckpoint cp2 = DeserializeFromHex<CSyncCheckpoint>(hex);
    BOOST_CHECK(cp2.vchMsg == cp.vchMsg);
    BOOST_CHECK(cp2.vchSig == cp.vchSig);
}
```

**Step 2: Build and run tests**

```bash
cmake --build build/linux-release
cd build/linux-release && ctest --output-on-failure
```

**Step 3: Commit**

```bash
git add src/test/golden_tests.cpp
git commit -m "Phase 6E: Add golden tests for CMerkleTx, CAlert, CSyncCheckpoint"
```

---

### Task 7: Migrate Non-Consensus Classes (15 classes)

**Files:**
- Modify: `src/crypter.h` — CMasterKey
- Modify: `src/key.h` — CPubKey
- Modify: `src/walletdb.h` — CKeyMetadata, CStealthKeyMetadata
- Modify: `src/smessage.h` — SecMsgAddress, SecMsgStored
- Modify: `src/stealth.h` — CStealthAddress
- Modify: `src/alert.h` — CUnsignedAlert, CAlert
- Modify: `src/checkpoints.h` — CUnsignedSyncCheckpoint, CSyncCheckpoint
- Modify: `src/wallet.h` — CKeyPool, CWalletKey, CAccount
- Modify: `src/netbase.h` — CNetAddr, CService

For each class, replace `IMPLEMENT_SERIALIZE(...)` with three explicit member functions. The transformation pattern:

**Simple field-by-field (e.g., CMasterKey):**

```cpp
// Before:
IMPLEMENT_SERIALIZE
(
    READWRITE(vchCryptedKey);
    READWRITE(vchSalt);
    READWRITE(nDerivationMethod);
    READWRITE(nDeriveIterations);
    READWRITE(vchOtherDerivationParameters);
)

// After:
unsigned int GetSerializeSize(int nType, int nVersion) const
{
    unsigned int nSerSize = 0;
    nSerSize += ::GetSerializeSize(vchCryptedKey, nType, nVersion);
    nSerSize += ::GetSerializeSize(vchSalt, nType, nVersion);
    nSerSize += ::GetSerializeSize(nDerivationMethod, nType, nVersion);
    nSerSize += ::GetSerializeSize(nDeriveIterations, nType, nVersion);
    nSerSize += ::GetSerializeSize(vchOtherDerivationParameters, nType, nVersion);
    return nSerSize;
}
template<typename Stream>
void Serialize(Stream& s, int nType, int nVersion) const
{
    ::Serialize(s, vchCryptedKey, nType, nVersion);
    ::Serialize(s, vchSalt, nType, nVersion);
    ::Serialize(s, nDerivationMethod, nType, nVersion);
    ::Serialize(s, nDeriveIterations, nType, nVersion);
    ::Serialize(s, vchOtherDerivationParameters, nType, nVersion);
}
template<typename Stream>
void Unserialize(Stream& s, int nType, int nVersion)
{
    ::Unserialize(s, vchCryptedKey, nType, nVersion);
    ::Unserialize(s, vchSalt, nType, nVersion);
    ::Unserialize(s, nDerivationMethod, nType, nVersion);
    ::Unserialize(s, nDeriveIterations, nType, nVersion);
    ::Unserialize(s, vchOtherDerivationParameters, nType, nVersion);
}
```

**FLATDATA pattern (e.g., CNetAddr):**

```cpp
// Before:
IMPLEMENT_SERIALIZE( READWRITE(FLATDATA(ip)); )

// After:
unsigned int GetSerializeSize(int nType, int nVersion) const
{
    return sizeof(ip);
}
template<typename Stream>
void Serialize(Stream& s, int nType, int nVersion) const
{
    s.write(reinterpret_cast<const char*>(ip), sizeof(ip));
}
template<typename Stream>
void Unserialize(Stream& s, int nType, int nVersion)
{
    s.read(reinterpret_cast<char*>(ip), sizeof(ip));
}
```

**Conditional version pattern (e.g., CKeyPool):**

```cpp
// Before:
IMPLEMENT_SERIALIZE
(
    if (!(nType & SER_GETHASH))
        READWRITE(nVersion);
    READWRITE(nTime);
    READWRITE(vchPubKey);
)

// After:
unsigned int GetSerializeSize(int nType, int nVersion) const
{
    unsigned int nSerSize = 0;
    if (!(nType & SER_GETHASH))
        nSerSize += ::GetSerializeSize(nVersion, nType, nVersion);
    nSerSize += ::GetSerializeSize(nTime, nType, nVersion);
    nSerSize += ::GetSerializeSize(vchPubKey, nType, nVersion);
    return nSerSize;
}
template<typename Stream>
void Serialize(Stream& s, int nType, int nVersion) const
{
    if (!(nType & SER_GETHASH))
        ::Serialize(s, nVersion, nType, nVersion);
    ::Serialize(s, nTime, nType, nVersion);
    ::Serialize(s, vchPubKey, nType, nVersion);
}
template<typename Stream>
void Unserialize(Stream& s, int nType, int nVersion)
{
    if (!(nType & SER_GETHASH))
        ::Unserialize(s, nVersion, nType, nVersion);
    ::Unserialize(s, nTime, nType, nVersion);
    ::Unserialize(s, vchPubKey, nType, nVersion);
}
```

**CService with htons/ntohs and const_cast:**

```cpp
// After:
unsigned int GetSerializeSize(int nType, int nVersion) const
{
    return sizeof(ip) + sizeof(unsigned short);
}
template<typename Stream>
void Serialize(Stream& s, int nType, int nVersion) const
{
    s.write(reinterpret_cast<const char*>(ip), sizeof(ip));
    unsigned short portN = htons(port);
    ::Serialize(s, portN, nType, nVersion);
}
template<typename Stream>
void Unserialize(Stream& s, int nType, int nVersion)
{
    s.read(reinterpret_cast<char*>(ip), sizeof(ip));
    unsigned short portN;
    ::Unserialize(s, portN, nType, nVersion);
    port = ntohs(portN);
}
```

**Version assignment pattern (e.g., CKeyMetadata):**

```cpp
// Before:
IMPLEMENT_SERIALIZE
(
    READWRITE(this->nVersion);
    nVersion = this->nVersion;
    READWRITE(nCreateTime);
)

// After:
unsigned int GetSerializeSize(int nType, int nVersion) const
{
    unsigned int nSerSize = 0;
    nSerSize += ::GetSerializeSize(this->nVersion, nType, nVersion);
    nSerSize += ::GetSerializeSize(nCreateTime, nType, nVersion);
    return nSerSize;
}
template<typename Stream>
void Serialize(Stream& s, int nType, int nVersion) const
{
    ::Serialize(s, this->nVersion, nType, nVersion);
    ::Serialize(s, nCreateTime, nType, nVersion);
}
template<typename Stream>
void Unserialize(Stream& s, int nType, int nVersion)
{
    ::Unserialize(s, this->nVersion, nType, nVersion);
    nVersion = this->nVersion;
    ::Unserialize(s, nCreateTime, nType, nVersion);
}
```

Note: The `nVersion = this->nVersion;` assignment only matters in Unserialize (it pins the stream version to the object's version for remaining fields). In GetSerializeSize and Serialize it's a no-op since `nVersion` is a local parameter. Include it in Unserialize only.

**Apply this transformation to all 15 non-consensus classes listed above.**

Special care for:
- **CService**: Inherits CNetAddr; serialize parent's `ip` field directly (it's `protected`)
- **CAddress**: Complex conditional logic; preserve exactly
- **CStealthAddress**: Not all fields are serialized (number_signatures, prefix are skipped)

**Step 2: Build and run all tests**

```bash
rm -rf build/linux-release && cmake --preset linux-release && cmake --build build/linux-release
cd build/linux-release && ctest --output-on-failure
```

All golden tests must still pass — the serialized bytes must be identical.

**Step 3: Build Windows**

```bash
rm -rf build/windows-mxe && cmake --preset windows-mxe && cmake --build build/windows-mxe
```

**Step 4: Commit**

```bash
git add src/crypter.h src/key.h src/walletdb.h src/smessage.h src/stealth.h \
        src/alert.h src/checkpoints.h src/wallet.h src/netbase.h
git commit -m "Phase 6E: Migrate 15 non-consensus classes from IMPLEMENT_SERIALIZE to explicit members"
```

---

### Task 8: Migrate Protocol Classes (3 classes)

**Files:**
- Modify: `src/protocol.h` — CMessageHeader, CAddress, CInv

**CMessageHeader** uses FLATDATA + normal fields:

```cpp
unsigned int GetSerializeSize(int nType, int nVersion) const
{
    unsigned int nSerSize = 0;
    nSerSize += sizeof(pchMessageStart);
    nSerSize += sizeof(pchCommand);
    nSerSize += ::GetSerializeSize(nMessageSize, nType, nVersion);
    nSerSize += ::GetSerializeSize(nChecksum, nType, nVersion);
    return nSerSize;
}
template<typename Stream>
void Serialize(Stream& s, int nType, int nVersion) const
{
    s.write(pchMessageStart, sizeof(pchMessageStart));
    s.write(pchCommand, sizeof(pchCommand));
    ::Serialize(s, nMessageSize, nType, nVersion);
    ::Serialize(s, nChecksum, nType, nVersion);
}
template<typename Stream>
void Unserialize(Stream& s, int nType, int nVersion)
{
    s.read(pchMessageStart, sizeof(pchMessageStart));
    s.read(pchCommand, sizeof(pchCommand));
    ::Unserialize(s, nMessageSize, nType, nVersion);
    ::Unserialize(s, nChecksum, nType, nVersion);
}
```

**CAddress** has complex conditional logic — preserve EXACTLY:

```cpp
unsigned int GetSerializeSize(int nType, int nVersion) const
{
    unsigned int nSerSize = 0;
    if (nType & SER_DISK)
        nSerSize += ::GetSerializeSize(nVersion, nType, nVersion);
    if ((nType & SER_DISK) ||
        (nVersion >= CADDR_TIME_VERSION && !(nType & SER_GETHASH)))
        nSerSize += ::GetSerializeSize(nTime, nType, nVersion);
    nSerSize += ::GetSerializeSize(nServices, nType, nVersion);
    nSerSize += CService::GetSerializeSize(nType, nVersion);
    return nSerSize;
}
template<typename Stream>
void Serialize(Stream& s, int nType, int nVersion) const
{
    if (nType & SER_DISK)
        ::Serialize(s, nVersion, nType, nVersion);
    if ((nType & SER_DISK) ||
        (nVersion >= CADDR_TIME_VERSION && !(nType & SER_GETHASH)))
        ::Serialize(s, nTime, nType, nVersion);
    ::Serialize(s, nServices, nType, nVersion);
    CService::Serialize(s, nType, nVersion);
}
template<typename Stream>
void Unserialize(Stream& s, int nType, int nVersion)
{
    Init();
    if (nType & SER_DISK)
        ::Unserialize(s, const_cast<int&>(this->nVersion), nType, nVersion);
    if ((nType & SER_DISK) ||
        (nVersion >= CADDR_TIME_VERSION && !(nType & SER_GETHASH)))
        ::Unserialize(s, nTime, nType, nVersion);
    ::Unserialize(s, nServices, nType, nVersion);
    CService::Unserialize(s, nType, nVersion);
}
```

Note: The original has `pthis->Init()` on fRead — translate to calling `Init()` at the start of Unserialize.

**CInv** is simple:

```cpp
unsigned int GetSerializeSize(int nType, int nVersion) const
{
    return ::GetSerializeSize(type, nType, nVersion) +
           ::GetSerializeSize(hash, nType, nVersion);
}
template<typename Stream>
void Serialize(Stream& s, int nType, int nVersion) const
{
    ::Serialize(s, type, nType, nVersion);
    ::Serialize(s, hash, nType, nVersion);
}
template<typename Stream>
void Unserialize(Stream& s, int nType, int nVersion)
{
    ::Unserialize(s, type, nType, nVersion);
    ::Unserialize(s, hash, nType, nVersion);
}
```

**Build, test, commit:**

```bash
rm -rf build/linux-release && cmake --preset linux-release && cmake --build build/linux-release
cd build/linux-release && ctest --output-on-failure
rm -rf build/windows-mxe && cmake --preset windows-mxe && cmake --build build/windows-mxe
git add src/protocol.h
git commit -m "Phase 6E: Migrate 3 protocol classes from IMPLEMENT_SERIALIZE to explicit members"
```

---

### Task 9: Migrate Consensus Classes (10 classes)

**Files:**
- Modify: `src/main.h` — CDiskTxPos, COutPoint, CTxIn, CTxOut, CTransaction, CMerkleTx, CTxIndex, CBlock, CDiskBlockIndex, CBlockLocator
- Modify: `src/wallet.h` — CWalletTx, CAccountingEntry
- Modify: `src/addrman.h` — CAddrInfo

**CRITICAL:** These classes are consensus-critical. The golden tests from Task 6 + existing golden tests MUST pass after migration. The byte-level output must be IDENTICAL.

**Special patterns to handle carefully:**

1. **CDiskTxPos, COutPoint** — Use FLATDATA(*this). Replace with raw read/write of the struct:
   ```cpp
   // CDiskTxPos has 3 unsigned ints = 12 bytes
   unsigned int GetSerializeSize(int nType, int nVersion) const { return 12; }
   template<typename Stream>
   void Serialize(Stream& s, int nType, int nVersion) const {
       s.write(reinterpret_cast<const char*>(this), 12);
   }
   template<typename Stream>
   void Unserialize(Stream& s, int nType, int nVersion) {
       s.read(reinterpret_cast<char*>(this), 12);
   }
   ```
   BUT: verify sizeof(*this) == 12 (no padding). If padding exists, serialize fields individually. COutPoint has uint256 (32) + unsigned int (4) = 36 bytes.

2. **CTransaction** — Version assignment in Unserialize:
   ```cpp
   template<typename Stream>
   void Unserialize(Stream& s, int nType, int nVersion) {
       ::Unserialize(s, this->nVersion, nType, nVersion);
       nVersion = this->nVersion;  // pin stream version
       ::Unserialize(s, nTime, nType, nVersion);
       ::Unserialize(s, vin, nType, nVersion);
       ::Unserialize(s, vout, nType, nVersion);
       ::Unserialize(s, nLockTime, nType, nVersion);
   }
   ```

3. **CMerkleTx** — Serializes parent CTransaction via SerReadWrite pattern:
   ```cpp
   template<typename Stream>
   void Serialize(Stream& s, int nType, int nVersion) const {
       CTransaction::Serialize(s, nType, nVersion);
       nVersion = this->nVersion;  // (no-op in Serialize, just matching macro behavior)
       ::Serialize(s, hashBlock, nType, nVersion);
       ::Serialize(s, vMerkleBranch, nType, nVersion);
       ::Serialize(s, nIndex, nType, nVersion);
   }
   ```
   Note: Call parent's explicit Serialize directly since CTransaction will also have been migrated by now.

4. **CBlock** — Conditional SER_BLOCKHEADERONLY:
   ```cpp
   template<typename Stream>
   void Serialize(Stream& s, int nType, int nVersion) const {
       ::Serialize(s, this->nVersion, nType, nVersion);
       ::Serialize(s, hashPrevBlock, nType, nVersion);
       ::Serialize(s, hashMerkleRoot, nType, nVersion);
       ::Serialize(s, nTime, nType, nVersion);
       ::Serialize(s, nBits, nType, nVersion);
       ::Serialize(s, nNonce, nType, nVersion);
       if (!(nType & (SER_GETHASH|SER_BLOCKHEADERONLY))) {
           ::Serialize(s, vtx, nType, nVersion);
           ::Serialize(s, vchBlockSig, nType, nVersion);
       }
   }
   template<typename Stream>
   void Unserialize(Stream& s, int nType, int nVersion) {
       ::Unserialize(s, this->nVersion, nType, nVersion);
       nVersion = this->nVersion;
       ::Unserialize(s, hashPrevBlock, nType, nVersion);
       ::Unserialize(s, hashMerkleRoot, nType, nVersion);
       ::Unserialize(s, nTime, nType, nVersion);
       ::Unserialize(s, nBits, nType, nVersion);
       ::Unserialize(s, nNonce, nType, nVersion);
       if (!(nType & (SER_GETHASH|SER_BLOCKHEADERONLY))) {
           ::Unserialize(s, vtx, nType, nVersion);
           ::Unserialize(s, vchBlockSig, nType, nVersion);
       } else {
           vtx.clear();
           vchBlockSig.clear();
       }
   }
   ```
   Note: The `const_cast` from the macro is no longer needed — Unserialize is non-const.

5. **CDiskBlockIndex** — Conditional PoS fields:
   ```cpp
   // In Unserialize, the fRead path that clears fields:
   if (IsProofOfStake()) {
       ::Unserialize(s, prevoutStake, nType, nVersion);
       ::Unserialize(s, nStakeTime, nType, nVersion);
   } else {
       prevoutStake.SetNull();
       nStakeTime = 0;
   }
   ```

6. **CWalletTx** — Complex mapValue encode/decode. This is the hardest class. The macro block has write-path-only logic (`if (!fRead)`) and read-path-only logic (`if (fRead)`). Split these into Serialize and Unserialize respectively. Preserve the mapValue packing exactly.

7. **CAccountingEntry** — Similar complexity with mapValue packing in strComment. Split encode into Serialize, decode into Unserialize.

8. **CAddrInfo** — Serializes parent CAddress via pointer:
   ```cpp
   template<typename Stream>
   void Serialize(Stream& s, int nType, int nVersion) const {
       CAddress::Serialize(s, nType, nVersion);
       ::Serialize(s, source, nType, nVersion);
       ::Serialize(s, nLastSuccess, nType, nVersion);
       ::Serialize(s, nAttempts, nType, nVersion);
   }
   ```

**Build, test, commit:**

```bash
rm -rf build/linux-release && cmake --preset linux-release && cmake --build build/linux-release
cd build/linux-release && ctest --output-on-failure
```

Run the golden tests specifically to verify byte-level compatibility:
```bash
./src/test/test_pinkcoin --run_test=golden_tests --log_level=test_suite
```

All golden tests MUST pass. If any fail, the migration has a bug.

```bash
rm -rf build/windows-mxe && cmake --preset windows-mxe && cmake --build build/windows-mxe
git add src/main.h src/wallet.h src/addrman.h
git commit -m "Phase 6E: Migrate 10 consensus classes from IMPLEMENT_SERIALIZE to explicit members"
```

---

## Stream 4: Cleanup

### Task 10: Consolidate PushMessage with Variadic Template

**Files:**
- Modify: `src/net.h` — replace 10 PushMessage overloads

**Step 1: Replace all 10 overloads with 2 functions**

Remove the 10 overloads (lines ~497-653 in net.h). Replace with:

```cpp
// Zero-argument message (e.g., "verack", "getaddr")
void PushMessage(const char* pszCommand)
{
    try {
        BeginMessage(pszCommand);
        EndMessage();
    } catch (...) {
        AbortMessage();
        throw;
    }
}

// Variadic message (1+ arguments)
template<typename... Args>
void PushMessage(const char* pszCommand, const Args&... args)
{
    try {
        BeginMessage(pszCommand);
        // C++17 fold expression: serialize each argument
        ((ssSend << args), ...);
        EndMessage();
    } catch (...) {
        AbortMessage();
        throw;
    }
}
```

**Step 2: Build and test**

```bash
rm -rf build/linux-release && cmake --preset linux-release && cmake --build build/linux-release
cd build/linux-release && ctest --output-on-failure
```

The fold expression `((ssSend << args), ...)` expands to `ssSend << a1, ssSend << a2, ...` which is equivalent to `ssSend << a1 << a2 << ...` (operator<< is left-associative and returns the stream). Verify the `version` message (8 args) still works.

**Step 3: Build Windows and commit**

```bash
rm -rf build/windows-mxe && cmake --preset windows-mxe && cmake --build build/windows-mxe
git add src/net.h
git commit -m "Phase 6E: Replace 10 PushMessage overloads with single variadic template"
```

---

### Task 11: Remove IMPLEMENT_SERIALIZE Macro and Clean Build Configuration

**Files:**
- Modify: `src/serialize.h` — remove IMPLEMENT_SERIALIZE and READWRITE macros
- Modify: `src/rpc/rpcdump.cpp` — remove dead `boost/variant/get.hpp` include
- Modify: `CMakeLists.txt` (root) — remove dead defines

**Step 1: Remove macros from serialize.h**

Remove lines 58-96 (the IMPLEMENT_SERIALIZE macro and the READWRITE macro).

Keep everything else in serialize.h:
- CFlatData class and FLATDATA macro (still used by direct member functions)
- CVarInt and VARINT macro
- SerReadWrite functions (still used by the READWRITE macro if any remain — verify none do)
- All free-function Serialize/Unserialize overloads
- CSerAction* classes (can be removed if READWRITE is gone)
- ser_streamplaceholder (can be removed if IMPLEMENT_SERIALIZE is gone)

Actually, once IMPLEMENT_SERIALIZE and READWRITE are both gone:
- Remove `READWRITE` macro
- Remove `CSerActionGetSerializeSize`, `CSerActionSerialize`, `CSerActionUnserialize` classes
- Remove `SerReadWrite` function templates
- Remove `ser_streamplaceholder` struct

Keep FLATDATA and VARINT macros — they may still be used directly (check first).

**Step 2: Remove dead boost include**

In `src/rpc/rpcdump.cpp`, remove:
```cpp
#include <boost/variant/get.hpp>
```

**Step 3: Remove dead CMake defines**

In root `CMakeLists.txt`, find and remove:
```cmake
-DBOOST_SPIRIT_THREADSAFE
-DBOOST_THREAD_USE_LIB
-DBOOST_BIND_GLOBAL_PLACEHOLDERS
```

These were for boost::spirit (never used), boost::thread (migrated in 6B), and boost::bind (migrated in 6B). None are needed.

**Step 4: Verify zero boost:: in production code**

```bash
grep -rn "boost::" src/ --include="*.cpp" --include="*.h" | grep -v "src/test/" | grep -v "src/asio/"
```

Expected: ZERO matches. If any remain, fix them.

Also verify:
```bash
grep -rn "IMPLEMENT_SERIALIZE" src/ --include="*.h"
```

Expected: ZERO matches.

**Step 5: Full clean rebuild and test**

```bash
cd /mnt/projects-windows/Pink2
rm -rf build/linux-release && cmake --preset linux-release && cmake --build build/linux-release
rm -rf build/windows-mxe && cmake --preset windows-mxe && cmake --build build/windows-mxe
echo "=== LINUX ===" && stat -c '%y' build/linux-release/src/test/test_pinkcoin
echo "=== WINDOWS ===" && stat -c '%y' build/windows-mxe/src/pink2d.exe && stat -c '%y' build/windows-mxe/src/qt/Pinkcoin-Qt.exe
echo "=== NOW ===" && date '+%Y-%m-%d %H:%M:%S %z'
cd build/linux-release && ctest --output-on-failure
```

All timestamps within session. All tests pass. Zero failures.

**Step 6: Get final test count**

```bash
./src/test/test_pinkcoin --log_level=test_suite 2>&1 | grep -c "Leaving test case"
```

**Step 7: Commit**

```bash
git add src/serialize.h src/rpc/rpcdump.cpp CMakeLists.txt
git commit -m "Phase 6E: Remove IMPLEMENT_SERIALIZE macro and clean dead Boost configuration"
```

---

## Success Criteria Checklist

After all 11 tasks:

- [ ] `grep -rn "boost::" src/ --include="*.cpp" --include="*.h" | grep -v "src/test/" | grep -v "src/asio/"` returns ZERO lines
- [ ] `grep -rn "IMPLEMENT_SERIALIZE" src/ --include="*.h"` returns ZERO lines
- [ ] PushMessage in net.h: exactly 2 functions (zero-arg + variadic)
- [ ] All golden tests pass (byte-level serialization preserved)
- [ ] Total test count >= 1,612 (plus new golden tests from Task 6)
- [ ] Linux build: clean, all tests pass
- [ ] Windows MXE build: clean
- [ ] No dead CMake defines (BOOST_SPIRIT_THREADSAFE, BOOST_THREAD_USE_LIB, etc.)
- [ ] Boost only appears in src/test/ files (Boost.Test framework)
