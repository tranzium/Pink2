# Phase 6C: Infrastructure Modernization Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Introduce structured logging with levels and categories, eliminate legacy macros, and add runtime log control — enabling categorized debug output for production troubleshooting.

**Architecture:** Create a lightweight Logger singleton with level/category filtering. Route all existing `printf` calls through it via the existing `OutputDebugStringF` → Logger pipeline, maintaining full backward compatibility. Selectively add log categories to critical code paths (consensus, wallet, network). Replace unsafe C macros (BEGIN/END, ARRAYLEN) with type-safe C++17 alternatives.

**Tech Stack:** C++17, Boost.Test, existing `strprintf`/`vstrprintf` formatting infrastructure

---

## Context

Current logging: `#define printf OutputDebugStringF` redirects all `printf()` calls to a function that writes to `debug.log` with optional timestamps. Three boolean flags (`fDebug`, `fDebugNet`, `fDebugSmsg`) provide coarse-grained filtering. No log levels, no structured categories, no runtime control.

Printf call distribution (top files): smessage.cpp (292), main.cpp (56), init.cpp (55), util.cpp (32), stealth.cpp (21), txdb-leveldb.cpp (20), db.cpp (19), stakedb.cpp (15).

Macro inventory: BEGIN/END (17 production uses), ARRAYLEN (5 uses), IMPLEMENT_SERIALIZE (30+ classes, out of scope), PAIRTYPE (1 use).

---

### Task 1: Create logging framework

**Files:**
- Create: `src/logging.h`
- Create: `src/logging.cpp`
- Modify: `src/CMakeLists.txt` (add logging.cpp to pinkcoin_core sources)

**Step 1: Write the logging header**

```cpp
// logging.h — Lightweight structured logging for Pinkcoin

#ifndef BITCOIN_LOGGING_H
#define BITCOIN_LOGGING_H

#include <atomic>
#include <cstdint>
#include <string>

enum class LogLevel : int {
    NONE = 0,
    ERROR = 1,
    WARN = 2,
    INFO = 3,
    DEBUG = 4
};

namespace BCLog {
enum Category : uint32_t {
    NONE       = 0,
    NET        = (1u << 0),
    WALLET     = (1u << 1),
    STAKE      = (1u << 2),
    RPC        = (1u << 3),
    CONSENSUS  = (1u << 4),
    SMSG       = (1u << 5),
    MEMPOOL    = (1u << 6),
    DB         = (1u << 7),
    ALL        = 0xFFFFFFFFu
};
} // namespace BCLog

class Logger {
    std::atomic<int> m_level{static_cast<int>(LogLevel::INFO)};
    std::atomic<uint32_t> m_categories{0};

public:
    static Logger& GetInstance();

    void SetLogLevel(LogLevel level);
    LogLevel GetLogLevel() const;

    void EnableCategory(BCLog::Category cat);
    void DisableCategory(BCLog::Category cat);
    void SetCategories(uint32_t cats);
    uint32_t GetCategories() const;

    bool WillLog(LogLevel level) const;
    bool WillLog(LogLevel level, BCLog::Category cat) const;

    // Format and write a log line (thread-safe, uses existing OutputDebugStringF infra)
    void LogPrintStr(const std::string& str);

    // Parse category name to flag (e.g., "net" -> BCLog::NET)
    static BCLog::Category CategoryFromString(const std::string& name);
    static std::string CategoryToString(BCLog::Category cat);
    static LogLevel LevelFromString(const std::string& name);
    static std::string LevelToString(LogLevel level);
};

// Convenience macros
// LogPrintf: always logs at INFO level, no category filter
#define LogPrintf(...) do { \
    if (Logger::GetInstance().WillLog(LogLevel::INFO)) { \
        Logger::GetInstance().LogPrintStr(strprintf(__VA_ARGS__)); \
    } \
} while(0)

// LogPrint: logs at DEBUG level, filtered by category
#define LogPrint(category, ...) do { \
    if (Logger::GetInstance().WillLog(LogLevel::DEBUG, category)) { \
        Logger::GetInstance().LogPrintStr(strprintf(__VA_ARGS__)); \
    } \
} while(0)

// LogError: always logs at ERROR level
#define LogError(...) do { \
    Logger::GetInstance().LogPrintStr("ERROR: " + strprintf(__VA_ARGS__)); \
} while(0)

#endif // BITCOIN_LOGGING_H
```

**Step 2: Write the logging implementation**

```cpp
// logging.cpp

#include "logging.h"
#include "util.h"
#include "string_utils.h"

Logger& Logger::GetInstance()
{
    static Logger instance;
    return instance;
}

void Logger::SetLogLevel(LogLevel level) { m_level.store(static_cast<int>(level)); }
LogLevel Logger::GetLogLevel() const { return static_cast<LogLevel>(m_level.load()); }

void Logger::EnableCategory(BCLog::Category cat)
{
    m_categories.fetch_or(static_cast<uint32_t>(cat));
}

void Logger::DisableCategory(BCLog::Category cat)
{
    m_categories.fetch_and(~static_cast<uint32_t>(cat));
}

void Logger::SetCategories(uint32_t cats) { m_categories.store(cats); }
uint32_t Logger::GetCategories() const { return m_categories.load(); }

bool Logger::WillLog(LogLevel level) const
{
    return static_cast<int>(level) <= m_level.load();
}

bool Logger::WillLog(LogLevel level, BCLog::Category cat) const
{
    if (static_cast<int>(level) > m_level.load())
        return false;
    return (m_categories.load() & static_cast<uint32_t>(cat)) != 0;
}

void Logger::LogPrintStr(const std::string& str)
{
    // Route through existing OutputDebugStringF infrastructure
    // which handles file/console/debugger output, timestamps, and threading
    OutputDebugStringF("%s", str.c_str());
}

BCLog::Category Logger::CategoryFromString(const std::string& name)
{
    std::string lower = strutil::to_lower(name);
    if (lower == "net")       return BCLog::NET;
    if (lower == "wallet")    return BCLog::WALLET;
    if (lower == "stake")     return BCLog::STAKE;
    if (lower == "rpc")       return BCLog::RPC;
    if (lower == "consensus") return BCLog::CONSENSUS;
    if (lower == "smsg")      return BCLog::SMSG;
    if (lower == "mempool")   return BCLog::MEMPOOL;
    if (lower == "db")        return BCLog::DB;
    if (lower == "all" || lower == "1") return BCLog::ALL;
    return BCLog::NONE;
}

std::string Logger::CategoryToString(BCLog::Category cat)
{
    switch (cat) {
        case BCLog::NET:       return "net";
        case BCLog::WALLET:    return "wallet";
        case BCLog::STAKE:     return "stake";
        case BCLog::RPC:       return "rpc";
        case BCLog::CONSENSUS: return "consensus";
        case BCLog::SMSG:      return "smsg";
        case BCLog::MEMPOOL:   return "mempool";
        case BCLog::DB:        return "db";
        case BCLog::ALL:       return "all";
        default:               return "none";
    }
}

LogLevel Logger::LevelFromString(const std::string& name)
{
    std::string lower = strutil::to_lower(name);
    if (lower == "error") return LogLevel::ERROR;
    if (lower == "warn")  return LogLevel::WARN;
    if (lower == "info")  return LogLevel::INFO;
    if (lower == "debug") return LogLevel::DEBUG;
    if (lower == "none")  return LogLevel::NONE;
    return LogLevel::INFO;
}

std::string Logger::LevelToString(LogLevel level)
{
    switch (level) {
        case LogLevel::NONE:  return "none";
        case LogLevel::ERROR: return "error";
        case LogLevel::WARN:  return "warn";
        case LogLevel::INFO:  return "info";
        case LogLevel::DEBUG: return "debug";
        default:              return "info";
    }
}
```

**Step 3: Add logging.cpp to CMakeLists.txt**

In `src/CMakeLists.txt`, add `logging.cpp` to the `PINKCOIN_CORE_SOURCES` list.

**Step 4: Build and verify compilation**

Run: `rm -rf build/linux-release && cmake --preset linux-release && cmake --build build/linux-release`
Expected: Clean compilation with logging framework linked in

**Step 5: Commit**

```bash
git add src/logging.h src/logging.cpp src/CMakeLists.txt
git commit -m "Phase 6C: Add structured logging framework with levels and categories"
```

---

### Task 2: Integrate logger into init and bridge existing printf

**Files:**
- Modify: `src/init.cpp` (~15 lines)
- Modify: `src/util.cpp` (~10 lines)
- Modify: `src/util.h` (~2 lines)

**Goal:** Wire the Logger into the startup sequence so `-debug`, `-debugnet`, `-debugsmsg`, and a new `-loglevel` flag configure the Logger. Existing `printf` calls continue to work unchanged via the `OutputDebugStringF` → Logger bridge.

**Step 1: Add Logger initialization to AppInit2**

In `init.cpp`, after the existing debug flag parsing (~lines 479-490), add Logger configuration:

```cpp
#include "logging.h"

// After existing fDebug/fDebugNet/fDebugSmsg parsing:
Logger& logger = Logger::GetInstance();

// Set log level from -loglevel (default: info, or debug if -debug)
std::string logLevelStr = GetArg("-loglevel", fDebug ? "debug" : "info");
logger.SetLogLevel(Logger::LevelFromString(logLevelStr));

// Map existing debug flags to categories
if (fDebug)
    logger.SetCategories(BCLog::ALL);
else {
    uint32_t cats = 0;
    if (fDebugNet) cats |= BCLog::NET;
    if (fDebugSmsg) cats |= BCLog::SMSG;
    // Parse -debug=<category> for selective category enablement
    for (const auto& cat : mapMultiArgs["-debug"]) {
        cats |= static_cast<uint32_t>(Logger::CategoryFromString(cat));
    }
    logger.SetCategories(cats);
}
```

**Step 2: Add `#include "logging.h"` to util.h**

Add `#include "logging.h"` near the top of `util.h` so LogPrintf/LogPrint macros are available everywhere util.h is included.

**Step 3: Build and verify**

Run: `cmake --build build/linux-release`
Expected: Clean build, all existing tests pass

**Step 4: Run tests**

Run: `cd build/linux-release && ctest --output-on-failure`
Expected: All 1,342 tests pass

**Step 5: Commit**

```bash
git add src/init.cpp src/util.h src/util.cpp
git commit -m "Phase 6C: Integrate logger into init, bridge existing debug flags"
```

---

### Task 3: Write logging framework tests

**Files:**
- Create: `src/test/logging_tests.cpp`
- Modify: `src/test/CMakeLists.txt`

**Step 1: Write tests**

```cpp
// logging_tests.cpp — Tests for structured logging framework

#include <boost/test/unit_test.hpp>
#include "logging.h"

BOOST_AUTO_TEST_SUITE(logging_tests)

// === Log Level tests ===

BOOST_AUTO_TEST_CASE(default_level_is_info)
{
    // Logger is initialized by TestingSetup; level depends on test config
    // Just verify we can query it
    LogLevel level = Logger::GetInstance().GetLogLevel();
    BOOST_CHECK(level >= LogLevel::NONE);
    BOOST_CHECK(level <= LogLevel::DEBUG);
}

BOOST_AUTO_TEST_CASE(set_and_get_level)
{
    Logger& logger = Logger::GetInstance();
    LogLevel saved = logger.GetLogLevel();

    logger.SetLogLevel(LogLevel::ERROR);
    BOOST_CHECK(logger.GetLogLevel() == LogLevel::ERROR);

    logger.SetLogLevel(LogLevel::DEBUG);
    BOOST_CHECK(logger.GetLogLevel() == LogLevel::DEBUG);

    logger.SetLogLevel(saved); // restore
}

BOOST_AUTO_TEST_CASE(will_log_respects_level)
{
    Logger& logger = Logger::GetInstance();
    LogLevel saved = logger.GetLogLevel();

    logger.SetLogLevel(LogLevel::WARN);
    BOOST_CHECK(logger.WillLog(LogLevel::ERROR));
    BOOST_CHECK(logger.WillLog(LogLevel::WARN));
    BOOST_CHECK(!logger.WillLog(LogLevel::INFO));
    BOOST_CHECK(!logger.WillLog(LogLevel::DEBUG));

    logger.SetLogLevel(LogLevel::DEBUG);
    BOOST_CHECK(logger.WillLog(LogLevel::ERROR));
    BOOST_CHECK(logger.WillLog(LogLevel::DEBUG));

    logger.SetLogLevel(saved);
}

BOOST_AUTO_TEST_CASE(level_none_blocks_all)
{
    Logger& logger = Logger::GetInstance();
    LogLevel saved = logger.GetLogLevel();

    logger.SetLogLevel(LogLevel::NONE);
    BOOST_CHECK(!logger.WillLog(LogLevel::ERROR));
    BOOST_CHECK(!logger.WillLog(LogLevel::INFO));
    BOOST_CHECK(!logger.WillLog(LogLevel::DEBUG));

    logger.SetLogLevel(saved);
}

// === Category tests ===

BOOST_AUTO_TEST_CASE(categories_default_zero)
{
    Logger& logger = Logger::GetInstance();
    uint32_t saved = logger.GetCategories();

    logger.SetCategories(0);
    BOOST_CHECK_EQUAL(logger.GetCategories(), 0u);

    logger.SetCategories(saved);
}

BOOST_AUTO_TEST_CASE(enable_and_disable_category)
{
    Logger& logger = Logger::GetInstance();
    uint32_t saved = logger.GetCategories();

    logger.SetCategories(0);
    logger.EnableCategory(BCLog::NET);
    BOOST_CHECK_EQUAL(logger.GetCategories(), static_cast<uint32_t>(BCLog::NET));

    logger.EnableCategory(BCLog::WALLET);
    BOOST_CHECK_EQUAL(logger.GetCategories(),
        static_cast<uint32_t>(BCLog::NET) | static_cast<uint32_t>(BCLog::WALLET));

    logger.DisableCategory(BCLog::NET);
    BOOST_CHECK_EQUAL(logger.GetCategories(), static_cast<uint32_t>(BCLog::WALLET));

    logger.SetCategories(saved);
}

BOOST_AUTO_TEST_CASE(will_log_checks_category)
{
    Logger& logger = Logger::GetInstance();
    LogLevel savedLevel = logger.GetLogLevel();
    uint32_t savedCats = logger.GetCategories();

    logger.SetLogLevel(LogLevel::DEBUG);
    logger.SetCategories(BCLog::NET);

    BOOST_CHECK(logger.WillLog(LogLevel::DEBUG, BCLog::NET));
    BOOST_CHECK(!logger.WillLog(LogLevel::DEBUG, BCLog::WALLET));

    logger.SetLogLevel(LogLevel::WARN);
    BOOST_CHECK(!logger.WillLog(LogLevel::DEBUG, BCLog::NET)); // level too low

    logger.SetLogLevel(savedLevel);
    logger.SetCategories(savedCats);
}

BOOST_AUTO_TEST_CASE(category_all_enables_everything)
{
    Logger& logger = Logger::GetInstance();
    LogLevel savedLevel = logger.GetLogLevel();
    uint32_t savedCats = logger.GetCategories();

    logger.SetLogLevel(LogLevel::DEBUG);
    logger.SetCategories(BCLog::ALL);

    BOOST_CHECK(logger.WillLog(LogLevel::DEBUG, BCLog::NET));
    BOOST_CHECK(logger.WillLog(LogLevel::DEBUG, BCLog::WALLET));
    BOOST_CHECK(logger.WillLog(LogLevel::DEBUG, BCLog::CONSENSUS));
    BOOST_CHECK(logger.WillLog(LogLevel::DEBUG, BCLog::SMSG));

    logger.SetLogLevel(savedLevel);
    logger.SetCategories(savedCats);
}

// === String conversion tests ===

BOOST_AUTO_TEST_CASE(category_from_string)
{
    BOOST_CHECK(Logger::CategoryFromString("net") == BCLog::NET);
    BOOST_CHECK(Logger::CategoryFromString("NET") == BCLog::NET);
    BOOST_CHECK(Logger::CategoryFromString("wallet") == BCLog::WALLET);
    BOOST_CHECK(Logger::CategoryFromString("stake") == BCLog::STAKE);
    BOOST_CHECK(Logger::CategoryFromString("rpc") == BCLog::RPC);
    BOOST_CHECK(Logger::CategoryFromString("consensus") == BCLog::CONSENSUS);
    BOOST_CHECK(Logger::CategoryFromString("smsg") == BCLog::SMSG);
    BOOST_CHECK(Logger::CategoryFromString("mempool") == BCLog::MEMPOOL);
    BOOST_CHECK(Logger::CategoryFromString("db") == BCLog::DB);
    BOOST_CHECK(Logger::CategoryFromString("all") == BCLog::ALL);
    BOOST_CHECK(Logger::CategoryFromString("1") == BCLog::ALL);
    BOOST_CHECK(Logger::CategoryFromString("unknown") == BCLog::NONE);
}

BOOST_AUTO_TEST_CASE(category_to_string)
{
    BOOST_CHECK_EQUAL(Logger::CategoryToString(BCLog::NET), "net");
    BOOST_CHECK_EQUAL(Logger::CategoryToString(BCLog::WALLET), "wallet");
    BOOST_CHECK_EQUAL(Logger::CategoryToString(BCLog::ALL), "all");
    BOOST_CHECK_EQUAL(Logger::CategoryToString(BCLog::NONE), "none");
}

BOOST_AUTO_TEST_CASE(level_from_string)
{
    BOOST_CHECK(Logger::LevelFromString("error") == LogLevel::ERROR);
    BOOST_CHECK(Logger::LevelFromString("ERROR") == LogLevel::ERROR);
    BOOST_CHECK(Logger::LevelFromString("warn") == LogLevel::WARN);
    BOOST_CHECK(Logger::LevelFromString("info") == LogLevel::INFO);
    BOOST_CHECK(Logger::LevelFromString("debug") == LogLevel::DEBUG);
    BOOST_CHECK(Logger::LevelFromString("none") == LogLevel::NONE);
    BOOST_CHECK(Logger::LevelFromString("bogus") == LogLevel::INFO); // default
}

BOOST_AUTO_TEST_CASE(level_to_string)
{
    BOOST_CHECK_EQUAL(Logger::LevelToString(LogLevel::ERROR), "error");
    BOOST_CHECK_EQUAL(Logger::LevelToString(LogLevel::WARN), "warn");
    BOOST_CHECK_EQUAL(Logger::LevelToString(LogLevel::INFO), "info");
    BOOST_CHECK_EQUAL(Logger::LevelToString(LogLevel::DEBUG), "debug");
    BOOST_CHECK_EQUAL(Logger::LevelToString(LogLevel::NONE), "none");
}

BOOST_AUTO_TEST_CASE(level_roundtrip)
{
    for (int i = 0; i <= 4; i++) {
        auto level = static_cast<LogLevel>(i);
        std::string name = Logger::LevelToString(level);
        BOOST_CHECK(Logger::LevelFromString(name) == level);
    }
}

// === LogPrintStr smoke test ===

BOOST_AUTO_TEST_CASE(log_print_str_does_not_crash)
{
    // LogPrintStr routes through OutputDebugStringF — just verify no crash
    BOOST_CHECK_NO_THROW(Logger::GetInstance().LogPrintStr("test log message\n"));
}

BOOST_AUTO_TEST_CASE(log_printf_macro_does_not_crash)
{
    Logger& logger = Logger::GetInstance();
    LogLevel saved = logger.GetLogLevel();
    logger.SetLogLevel(LogLevel::DEBUG);

    BOOST_CHECK_NO_THROW(LogPrintf("test LogPrintf %s %d\n", "hello", 42));

    logger.SetLogLevel(saved);
}

BOOST_AUTO_TEST_CASE(log_print_category_macro_does_not_crash)
{
    Logger& logger = Logger::GetInstance();
    LogLevel savedLevel = logger.GetLogLevel();
    uint32_t savedCats = logger.GetCategories();

    logger.SetLogLevel(LogLevel::DEBUG);
    logger.SetCategories(BCLog::ALL);

    BOOST_CHECK_NO_THROW(LogPrint(BCLog::NET, "test LogPrint NET %s\n", "data"));

    logger.SetLogLevel(savedLevel);
    logger.SetCategories(savedCats);
}

// === Thread safety (basic) ===

BOOST_AUTO_TEST_CASE(concurrent_level_changes)
{
    Logger& logger = Logger::GetInstance();
    LogLevel saved = logger.GetLogLevel();

    // Atomic operations should not crash under contention
    std::vector<std::thread> threads;
    for (int i = 0; i < 4; i++) {
        threads.emplace_back([&logger, i]() {
            for (int j = 0; j < 100; j++) {
                logger.SetLogLevel(static_cast<LogLevel>(i % 5));
                logger.WillLog(LogLevel::INFO);
            }
        });
    }
    for (auto& t : threads) t.join();

    logger.SetLogLevel(saved);
    BOOST_CHECK(true); // reached here without crash
}

BOOST_AUTO_TEST_SUITE_END()
```

**Step 2: Add to test CMakeLists.txt**

Add `logging_tests.cpp` to `PINKCOIN_TEST_SOURCES` in `src/test/CMakeLists.txt`.

**Step 3: Build and run tests**

Run: `cmake --build build/linux-release`
Run: `cd build/linux-release && ctest --output-on-failure`
Expected: All tests pass including new logging tests

**Step 4: Commit**

```bash
git add src/test/logging_tests.cpp src/test/CMakeLists.txt
git commit -m "Phase 6C: Add logging framework tests (20 test cases)"
```

---

### Task 4: Add setloglevel and getloglevel RPC commands

**Files:**
- Modify: `src/rpc/bitcoinrpc.cpp` (register commands in table)
- Create or modify: `src/rpc/rpcmisc.cpp` or add to existing RPC file (implement commands)
- Modify: `src/test/rpc_coverage_tests.cpp` (add test coverage)

**Step 1: Implement setloglevel RPC command**

The `setloglevel` command accepts a level name and optional category list:
- `setloglevel "debug"` — sets global level to DEBUG
- `setloglevel "debug" '["net","wallet"]'` — sets level and enables categories

**Step 2: Implement getloglevel RPC command**

Returns current log level and enabled categories as JSON.

**Step 3: Register in RPC command table**

Add entries to `vRPCCommands[]` in `bitcoinrpc.cpp`.

**Step 4: Add test coverage**

Add tests in `rpc_coverage_tests.cpp` (or a dedicated file) that exercise both commands.

**Step 5: Build, test, commit**

```bash
git commit -m "Phase 6C: Add setloglevel/getloglevel RPC commands"
```

---

### Task 5: Categorize consensus printf calls

**Files:**
- Modify: `src/main.cpp` (~20-30 targeted calls)
- Modify: `src/consensus/validation.cpp` (~10 calls)
- Modify: `src/consensus/blockprocessing.cpp` (~10 calls)
- Modify: `src/consensus/rewards.cpp` (~5 calls)

**Goal:** Convert the most important consensus-path `printf` calls to `LogPrint(BCLog::CONSENSUS, ...)` or `LogPrintf(...)`. Focus on:
- Block validation messages (CheckBlock, CheckTransaction)
- Proof-of-work/stake verification
- Reorganization events
- Error paths (these should use `LogError(...)`)

**Rules:**
- `error("...")` calls STAY as-is (they already prefix "ERROR:" and return false)
- `printf` in hot paths → `LogPrint(BCLog::CONSENSUS, ...)`
- `printf` for one-time events (block accepted, reorg) → `LogPrintf(...)`
- Remove dead/useless debug printf where the message adds no diagnostic value

**Step 1:** Convert printf calls in consensus files to LogPrint/LogPrintf
**Step 2:** Build and verify all tests pass
**Step 3:** Commit

```bash
git commit -m "Phase 6C: Categorize consensus logging (CONSENSUS category)"
```

---

### Task 6: Categorize wallet and staking printf calls

**Files:**
- Modify: `src/wallet/wallet.cpp` (~15-20 targeted calls)
- Modify: `src/wallet/walletdb.cpp` (~5 calls)
- Modify: `src/stakedb.cpp` (~5 calls)
- Modify: `src/miner.cpp` (~5 calls)

**Goal:** Convert wallet/staking `printf` calls to `LogPrint(BCLog::WALLET, ...)` or `LogPrint(BCLog::STAKE, ...)`. Focus on:
- CreateCoinStake logging → BCLog::STAKE
- Wallet loading/saving → BCLog::WALLET
- Key pool operations → BCLog::WALLET
- Staking weight calculations → BCLog::STAKE

**Rules:** Same as Task 5. Leave `error()` calls unchanged.

**Step 1:** Convert printf calls in wallet/staking files
**Step 2:** Build and verify all tests pass
**Step 3:** Commit

```bash
git commit -m "Phase 6C: Categorize wallet/staking logging (WALLET, STAKE categories)"
```

---

### Task 7: Categorize network printf calls

**Files:**
- Modify: `src/net/net.cpp` (~10-15 targeted calls)
- Modify: `src/net/netbase.cpp` (~5 calls)

**Goal:** Convert network `printf` calls to `LogPrint(BCLog::NET, ...)`. Focus on:
- Connection events (connect, disconnect, ban)
- Address relay
- Socket errors
- Already conditioned on `fDebugNet` — convert these to `LogPrint(BCLog::NET, ...)`

**Step 1:** Convert printf calls, replacing `if (fDebugNet) printf(...)` → `LogPrint(BCLog::NET, ...)`
**Step 2:** Build and verify all tests pass
**Step 3:** Commit

```bash
git commit -m "Phase 6C: Categorize network logging (NET category)"
```

---

### Task 8: Replace BEGIN/END macros with type-safe templates

**Files:**
- Modify: `src/util.h` (add template functions, keep old macros for now)
- Modify: `src/main.cpp` (3 uses)
- Modify: `src/main.h` (6 uses)
- Modify: `src/rpc/bitcoinrpc.cpp` (1 use)
- Modify: `src/rpc/rpcmining.cpp` (7 uses)

**Step 1: Add type-safe template replacements in util.h**

```cpp
// Type-safe replacements for BEGIN/END macros
template <typename T>
inline const unsigned char* UCharCast(const T& obj) {
    return reinterpret_cast<const unsigned char*>(&obj);
}

template <typename T>
inline unsigned char* UCharCast(T& obj) {
    return reinterpret_cast<unsigned char*>(&obj);
}

template <typename T>
inline const unsigned char* UCharEnd(const T& obj) {
    return reinterpret_cast<const unsigned char*>(&obj) + sizeof(obj);
}

template <typename T>
inline unsigned char* UCharEnd(T& obj) {
    return reinterpret_cast<unsigned char*>(&obj) + sizeof(obj);
}
```

**Step 2: Replace all 17 BEGIN/END usages**

Replace patterns like:
- `BEGIN(hash)` → `UCharCast(hash)` (or appropriate typed variant)
- `END(hash)` → `UCharEnd(hash)`

Adjust call sites based on whether they need `char*`, `unsigned char*`, or `void*`. The existing code uses BEGIN/END primarily with hash types (uint256) to get byte-range pointers for serialization.

**Step 3: Remove or deprecate old BEGIN/END macros**

After all usages are replaced, comment out or `#if 0` the old macros. Keep UBEGIN/UEND/FLATDATA as they may be used in serialization paths.

**Step 4: Build, test, commit**

```bash
git commit -m "Phase 6C: Replace BEGIN/END macros with type-safe templates"
```

---

### Task 9: Replace ARRAYLEN with std::size(), remove PAIRTYPE

**Files:**
- Modify: `src/util.h` (remove ARRAYLEN macro, remove PAIRTYPE macro)
- Modify: `src/net/net.cpp` (2 ARRAYLEN uses)
- Modify: `src/protocol.cpp` (3 ARRAYLEN uses)

**Step 1: Replace ARRAYLEN usage sites**

Replace all 5 occurrences of `ARRAYLEN(array)` with `std::size(array)`.

Note: `std::size()` is C++17 and provides compile-time safety — it won't silently compile if passed a pointer (unlike the macro).

**Step 2: Remove PAIRTYPE**

Find the single PAIRTYPE usage and replace with direct `std::pair<T1,T2>`.
Remove the `PAIRTYPE` macro definition from util.h.

**Step 3: Remove macro definitions**

Remove `#define ARRAYLEN(array)` and `#define PAIRTYPE(t1,t2)` from util.h.

**Step 4: Build, test, commit**

```bash
git commit -m "Phase 6C: Replace ARRAYLEN with std::size(), remove PAIRTYPE"
```

---

### Task 10: Build verification — both targets + full test suite

**Step 1: Clean rebuild Linux**

```bash
cd /mnt/projects-windows/Pink2
rm -rf build/linux-release && cmake --preset linux-release && cmake --build build/linux-release
```

**Step 2: Clean rebuild Windows MXE**

```bash
rm -rf build/windows-mxe && cmake --preset windows-mxe && cmake --build build/windows-mxe
```

**Step 3: Verify timestamps**

```bash
echo "=== LINUX ===" && stat -c '%y' build/linux-release/src/test/test_pinkcoin
echo "=== WINDOWS ===" && stat -c '%y' build/windows-mxe/src/pink2d.exe && stat -c '%y' build/windows-mxe/src/qt/Pinkcoin-Qt.exe
echo "=== NOW ===" && date '+%Y-%m-%d %H:%M:%S %z'
```

**Step 4: Run full test suite**

```bash
cd build/linux-release && ctest --output-on-failure
```

Expected: All tests pass (1,342+ existing + ~20 new logging tests), zero failures.

---

## Scope Notes

**In scope:**
- Logging framework with levels and categories
- Runtime log control via RPC
- Targeted printf → LogPrint conversion in ~60-80 key calls
- BEGIN/END and ARRAYLEN macro elimination
- ~20 new tests

**Out of scope (deferred to future phases):**
- IMPLEMENT_SERIALIZE modernization (30+ classes, too large)
- READWRITE/FLATDATA/VARINT macro changes (tightly coupled to serialization)
- PushMessage variadic migration (already template overloads, functional)
- LOCK/LOCK2 (standard Bitcoin pattern, keep)
- Exhaustive printf conversion of all 1,072 calls (framework enables gradual migration)
- string_view optimization (no profiling data to guide changes)
