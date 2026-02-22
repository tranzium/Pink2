# Phase 6E: Boost Extraction & Serialization Modernization — Design

**Date:** 2026-02-22
**Branch:** feature/twenty_six
**Prerequisite:** Phase 6D complete (json_spirit removed, boost::signals2 removed, 1,612 tests)
**Approach:** Mechanical migration with golden test pinning for consensus-critical paths

## Context

Phases 6A-6D brought the codebase from a 2012-era Bitcoin fork to modern C++17 with
structured logging, libsecp256k1, custom signals, and nlohmann/json. Four Boost libraries
remain in production code:

| Library | File(s) | Purpose |
|---------|---------|---------|
| boost::asio | rpc/bitcoinrpc.cpp | HTTP/HTTPS RPC server |
| boost::iostreams | rpc/bitcoinrpc.cpp | SSL stream device |
| boost::interprocess | init.cpp, qt/qtipcserver.cpp | File lock, Qt URI IPC |
| boost::date_time | qt/qtipcserver.cpp | Timeout calculations |

Additionally, the serialization system uses a 2012-era `IMPLEMENT_SERIALIZE` macro
across 33 classes in 12 header files, and `PushMessage` exists as 10 template overloads
(0-9 parameters) instead of a single variadic template.

## Goals

1. **Zero Boost in production code** — Boost.Test stays for the test framework only
2. **Eliminate IMPLEMENT_SERIALIZE macro** — Replace with explicit member functions
3. **Consolidate PushMessage** — Single variadic template with C++17 fold expression
4. **Clean stale build configuration** — Remove dead CMake defines

## Work Streams

### Stream 1: Standalone Asio Migration

**Scope:** Replace boost::asio + boost::iostreams in `src/rpc/bitcoinrpc.cpp`

**Approach:**
- Vendor standalone Asio headers into `src/asio/` (same pattern as `src/json/nlohmann/`)
- Namespace swap: `boost::asio::` → `asio::` (~150 occurrences)
- Smart pointer swap: `boost::shared_ptr` → `std::shared_ptr` (~40 occurrences)
- Error code swap: `boost::system::error_code` → `asio::error_code`
- Remove Boost 1.70+ `io_service`/`io_context` compatibility macros (standalone only has `io_context`)
- Replace `SSLIOStreamDevice` (boost::iostreams-based, ~60 lines) with `std::streambuf`-based
  `AsioSSLStreamBuf` (~100 lines)

**CMake changes:**
- Add `asio` INTERFACE library target (header-only, like nlohmann_json)
- Remove `Boost::system` and `Boost::chrono` from pinkcoin_core link targets
- Remove `BOOST_BIND_GLOBAL_PLACEHOLDERS` define (no Boost headers in production)

**Risk:** LOW — standalone Asio is API-identical to boost::asio by the same author.

**Testing:** Existing RPC tests (Tier C, F, G, I — 291 tests) serve as regression safety net.

### Stream 2: Platform File Lock

**Scope:** Replace `boost::interprocess::file_lock` in `src/init.cpp`

**Approach:**
- Create `src/filelock.h` — cross-platform `FileLock` class (~50 lines)
- POSIX path: `open()` + `flock(fd, LOCK_EX | LOCK_NB)`
- Windows path: `CreateFileA()` + `LockFileEx(LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY)`
- RAII: destructor calls `flock(fd, LOCK_UN)` / `UnlockFileEx()` + close handle
- Replace single usage site in init.cpp

**Testing:** Add FileLock unit tests (lock, contention detection, RAII release).

### Stream 3: Qt IPC Migration

**Scope:** Replace `boost::interprocess::message_queue` + `boost::date_time` in `src/qt/qtipcserver.cpp`

**Approach:**
- Replace with `QLocalServer` + `QLocalSocket` (already available in Qt5)
- Server: listen on named socket matching data directory, accept connections, read URI strings
- Client: connect to named socket, write URI string, disconnect
- Eliminates boost::date_time (Qt event loop handles timeouts)
- Eliminates boost::version.hpp include

**Testing:** GUI-only feature, tested manually. Existing Qt build verifies compilation.

### Stream 4: Serialization Modernization

**Scope:** Replace `IMPLEMENT_SERIALIZE` macro with explicit member functions across 33 classes.

**Existing pattern (3 classes already use it):**
- `base_uint<WIDTH>` in uint256.h — simple binary read/write
- `CBigNum` in bignum.h — vector delegation
- `CAddrMan` in addrman.h — asymmetric serialize/deserialize

**Transformation:**

```cpp
// Before (macro):
IMPLEMENT_SERIALIZE(
    READWRITE(field1);
    READWRITE(field2);
)

// After (explicit):
unsigned int GetSerializeSize(int nType, int nVersion) const {
    unsigned int nSize = 0;
    nSize += ::GetSerializeSize(field1, nType, nVersion);
    nSize += ::GetSerializeSize(field2, nType, nVersion);
    return nSize;
}
template<typename Stream>
void Serialize(Stream& s, int nType, int nVersion) const {
    ::Serialize(s, field1, nType, nVersion);
    ::Serialize(s, field2, nType, nVersion);
}
template<typename Stream>
void Unserialize(Stream& s, int nType, int nVersion) {
    ::Unserialize(s, field1, nType, nVersion);
    ::Unserialize(s, field2, nType, nVersion);
}
```

**Phase order:**

1. **Non-consensus classes first (15 classes):**
   - CMasterKey, CPubKey, CKeyPool, CWalletKey, CAccount, CAccountingEntry
   - CKeyMetadata, CStealthKeyMetadata, CStealthAddress
   - SecMsgAddress, SecMsgStored
   - CUnsignedAlert, CAlert, CUnsignedSyncCheckpoint, CSyncCheckpoint

2. **Protocol classes (3 classes):**
   - CMessageHeader, CAddress, CInv
   - Wire protocol bytes already pinned by golden tests

3. **Consensus classes (10 classes):**
   - CDiskTxPos, COutPoint, CTxIn, CTxOut, CTransaction, CMerkleTx
   - CTxIndex, CBlock, CDiskBlockIndex, CBlockLocator
   - Add golden tests for any class not yet pinned before transforming
   - CBlock has conditional SER_BLOCKHEADERONLY logic — preserve exactly

**Special cases:**
- `CBlock::Serialize` uses `nType & SER_BLOCKHEADERONLY` conditional — must preserve
- `CTransaction` version assignment `nVersion = this->nVersion` — must preserve
- `CWalletTx` inherits CMerkleTx — serialization chain must be maintained

### Stream 5: PushMessage Consolidation

**Scope:** Replace 10 PushMessage overloads in `src/net.h` with single variadic template.

```cpp
template<typename... Args>
void PushMessage(const char* pszCommand, const Args&... args) {
    try {
        BeginMessage(pszCommand);
        (void)(ssSend << ... << args);  // C++17 fold expression
        EndMessage();
    } catch (...) {
        AbortMessage();
        throw;
    }
}
```

Also add the zero-argument case:
```cpp
void PushMessage(const char* pszCommand) {
    try {
        BeginMessage(pszCommand);
        EndMessage();
    } catch (...) {
        AbortMessage();
        throw;
    }
}
```

**Testing:** All network tests (Tier D — 48 tests) + protocol tests (Tier F — 56 tests) serve as regression net.

### Stream 6: Build Cleanup

**Remove dead CMake defines:**
- `BOOST_SPIRIT_THREADSAFE` — no Boost.Spirit usage
- `BOOST_THREAD_USE_LIB` — no boost::thread usage
- `BOOST_BIND_GLOBAL_PLACEHOLDERS` — no boost::bind usage

**Remove dead include:**
- `boost/variant/get.hpp` in rpc/rpcdump.cpp (unused)

**Update find_package:**
- `find_package(Boost REQUIRED COMPONENTS unit_test_framework)` — no optional components
- Boost only needed for test executable, not pinkcoin_core

## Classes Using IMPLEMENT_SERIALIZE (Complete List)

| # | Class | File | Category |
|---|-------|------|----------|
| 1 | CDiskTxPos | main.h | Consensus |
| 2 | COutPoint | main.h | Consensus |
| 3 | CTxIn | main.h | Consensus |
| 4 | CTxOut | main.h | Consensus |
| 5 | CTransaction | main.h | Consensus |
| 6 | CMerkleTx | main.h | Consensus |
| 7 | CTxIndex | main.h | Consensus |
| 8 | CBlock | main.h | Consensus |
| 9 | CDiskBlockIndex | main.h | Consensus |
| 10 | CBlockLocator | main.h | Consensus |
| 11 | CAddrInfo | addrman.h | Network (has custom Serialize already) |
| 12 | CMessageHeader | protocol.h | Protocol |
| 13 | CAddress | protocol.h | Protocol |
| 14 | CInv | protocol.h | Protocol |
| 15 | CUnsignedAlert | alert.h | Alert |
| 16 | CAlert | alert.h | Alert |
| 17 | CUnsignedSyncCheckpoint | checkpoints.h | Checkpoint |
| 18 | CSyncCheckpoint | checkpoints.h | Checkpoint |
| 19 | CMasterKey | crypter.h | Wallet |
| 20 | CPubKey | key.h | Crypto |
| 21 | CNetAddr | netbase.h | Network |
| 22 | CService | netbase.h | Network |
| 23 | SecMsgAddress | smessage.h | Messaging |
| 24 | SecMsgStored | smessage.h | Messaging |
| 25 | CStealthAddress | stealth.h | Stealth |
| 26 | CKeyPool | wallet.h | Wallet |
| 27 | CWalletTx | wallet.h | Wallet |
| 28 | CWalletKey | wallet.h | Wallet |
| 29 | CAccount | wallet.h | Wallet |
| 30 | CAccountingEntry | wallet.h | Wallet |
| 31 | CKeyMetadata | walletdb.h | Wallet |
| 32 | CStealthKeyMetadata | walletdb.h | Wallet |

Note: CAddrInfo in addrman.h uses IMPLEMENT_SERIALIZE but CAddrMan already has explicit
member functions. CAddrInfo is #11 but has simple symmetric serialization.

## Build Checkpoints

| After | Verify |
|-------|--------|
| Asio vendored | Linux compile test |
| Asio migration complete | Linux + Windows |
| File lock + Qt IPC | Linux + Windows |
| Non-consensus serialization (15 classes) | Both builds + all tests |
| Protocol serialization (3 classes) | Both builds + all tests + golden tests |
| Consensus serialization (10 classes) | Both builds + all tests + golden tests |
| PushMessage + build cleanup | Both builds + all tests (FINAL) |

## Success Criteria

| Metric | Target |
|--------|--------|
| boost:: in production code | ZERO references |
| IMPLEMENT_SERIALIZE uses | ZERO (macro removed) |
| PushMessage overloads | 1 variadic + 1 zero-arg |
| Test count | 1,612+ (add golden + filelock tests) |
| Test failures | 0 |
| Linux build | Clean (278 units, may change with new files) |
| Windows MXE build | Clean (223 units, may change) |
| Dead CMake defines | All removed |

## Constraints

- No `-j` flag in builds
- Both Linux and Windows MXE must pass after each stream
- All 1,612 existing tests must continue passing
- Consensus serialization byte layout must not change (golden tests verify)
- BerkeleyDB 4.8 remains pinned
- Boost.Test stays for test framework (separate future project)
- No consensus rule changes
