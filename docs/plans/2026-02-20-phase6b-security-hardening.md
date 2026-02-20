# Phase 6B: Security Hardening Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Eliminate resource leaks via RAII database cursor wrappers, migrate threading from boost::thread to std::thread, and add security-focused tests proving correct cleanup and enforcement.

**Architecture:** Bottom-up — RAII primitives first, then apply them to all leak sites, then migrate threading, then write tests proving correctness.

**Tech Stack:** C++17 (unique_ptr RAII, std::thread), BerkeleyDB 4.8, LevelDB, Boost.Test

---

## Inventory of Changes

### RAII Cursor Leak Sites (6 production files)

| # | File | Function | Issue |
|---|------|----------|-------|
| 1 | db.h:222 | `GetCursor()` | Returns raw `Dbc*` — no RAII |
| 2 | db.cpp:394 | `CDB::Rewrite()` | Cursor leaks if loop body throws |
| 3 | wallet/walletdb.cpp:68 | `ListAccountCreditDebit()` | Cursor leaks if `entries.push_back()` throws |
| 4 | wallet/walletdb.cpp:480 | `LoadWallet()` | `return DB_CORRUPT` at line 498 without `pcursor->close()` |
| 5 | stakedb.cpp:102 | `LoadWallet()` | `return SDB_CORRUPT` at line 120 without `pcursor->close()` |
| 6 | rpc/rpc_wallet_mgmt.cpp:443 | `clearwallettransactions` | `throw` at line 499 without `pcursor->close()` or `TxnAbort()` |

### LevelDB Iterator Leak Sites (2 production files)

| # | File | Function | Issue |
|---|------|----------|-------|
| 7 | txdb-leveldb.cpp:318 | `LoadBlockIndex()` | Raw `delete iterator` — leaks on exception |
| 8 | qt/messagemodel.cpp:80,108 | `refreshMessageTable()` | Raw `delete it` — leaks on exception |

### Already RAII (no change needed)

| File | Usage | Status |
|------|-------|--------|
| rpc/rpcsmessage.cpp:608,630,718,735 | `std::unique_ptr<leveldb::Iterator>` | Already RAII |

### Threading Migration Sites (3 files)

| # | File | Line | Change |
|---|------|------|--------|
| 1 | util.cpp:1329 | `NewThread()` | `boost::thread` → `std::thread` + `t.detach()` |
| 2 | util.h:659 | `LoopForever` | Remove `catch (boost::thread_interrupted)` |
| 3 | util.h:682 | `TraceThread` | Remove `catch (boost::thread_interrupted)` |
| 4 | util.h:23 | include | Remove `#include <boost/thread.hpp>` |
| 5 | util.cpp:13 | include | Remove `#include <boost/thread.hpp>` |
| 6 | util.h:642-645 | comments | Update stale boost::thread references |
| 7 | util.h:648,674 | thread name | `"blackcoin-%s"` → `"pinkcoin-%s"` |

---

## Task 1: Create BDB cursor RAII guard

**Files:**
- Create: `src/db_cursor_guard.h`

**Step 1: Write the RAII guard header**

```cpp
#ifndef DB_CURSOR_GUARD_H
#define DB_CURSOR_GUARD_H

#include <db_cxx.h>

// RAII guard for BerkeleyDB Dbc* cursors.
// Ensures cursor is closed on scope exit, even on exception.
class BdbCursorGuard {
    Dbc* m_cursor;
public:
    explicit BdbCursorGuard(Dbc* cursor) noexcept : m_cursor(cursor) {}
    ~BdbCursorGuard() { if (m_cursor) m_cursor->close(); }

    // Non-copyable, non-movable
    BdbCursorGuard(const BdbCursorGuard&) = delete;
    BdbCursorGuard& operator=(const BdbCursorGuard&) = delete;

    Dbc* get() const noexcept { return m_cursor; }
    explicit operator bool() const noexcept { return m_cursor != nullptr; }

    // Release ownership without closing (for rare cases)
    Dbc* release() noexcept {
        Dbc* p = m_cursor;
        m_cursor = nullptr;
        return p;
    }
};

#endif // DB_CURSOR_GUARD_H
```

**Step 2: Verify it compiles**

Run: `cmake --build build/linux-release --target test_pinkcoin 2>&1 | tail -5`
Expected: Build succeeds (header not yet included anywhere)

**Step 3: Commit**

```bash
git add src/db_cursor_guard.h
git commit -m "Add BdbCursorGuard RAII wrapper for BDB cursors"
```

---

## Task 2: Apply BdbCursorGuard to walletdb.cpp LoadWallet()

**Files:**
- Modify: `src/wallet/walletdb.cpp:462-526`

**Context:** This function has a CRITICAL cursor leak: if `ReadAtCursor()` returns an error at line 495-498, it does `return DB_CORRUPT` without closing the cursor. The try/catch at line 523 swallows exceptions but the cursor is already lost.

**Step 1: Apply RAII guard**

Add `#include "db_cursor_guard.h"` at top of file.

Replace the raw cursor pattern (lines 480-521):

```cpp
// Before (leaks on error return):
Dbc* pcursor = GetCursor();
if (!pcursor) { ... return DB_CORRUPT; }
while (true) {
    int ret = ReadAtCursor(pcursor, ssKey, ssValue);
    if (ret == DB_NOTFOUND) break;
    else if (ret != 0) {
        printf("Error...\n");
        return DB_CORRUPT;   // LEAK: cursor not closed
    }
    // ... process
}
pcursor->close();

// After (RAII — cursor always closed):
BdbCursorGuard cursor(GetCursor());
if (!cursor) { ... return DB_CORRUPT; }
while (true) {
    int ret = ReadAtCursor(cursor.get(), ssKey, ssValue);
    if (ret == DB_NOTFOUND) break;
    else if (ret != 0) {
        printf("Error...\n");
        return DB_CORRUPT;   // cursor closed by destructor
    }
    // ... process
}
// cursor closed by destructor at scope exit
```

**Step 2: Build and test**

Run: `cmake --build build/linux-release && cd build/linux-release && ctest --output-on-failure`
Expected: All 1,317 tests pass

**Step 3: Commit**

```bash
git add src/wallet/walletdb.cpp
git commit -m "Fix cursor leak in CWalletDB::LoadWallet via BdbCursorGuard"
```

---

## Task 3: Apply BdbCursorGuard to stakedb.cpp LoadWallet()

**Files:**
- Modify: `src/stakedb.cpp:84-149`

**Context:** Identical leak pattern to Task 2 — `return SDB_CORRUPT` at line 120 without `pcursor->close()`.

**Step 1: Apply RAII guard**

Add `#include "db_cursor_guard.h"` at top. Replace raw `Dbc* pcursor` with `BdbCursorGuard cursor(GetCursor())`. Remove explicit `pcursor->close()` call at line 133.

**Step 2: Build and test**

Run: `cmake --build build/linux-release && cd build/linux-release && ctest --output-on-failure`
Expected: All 1,317 tests pass

**Step 3: Commit**

```bash
git add src/stakedb.cpp
git commit -m "Fix cursor leak in CStakeDB::LoadWallet via BdbCursorGuard"
```

---

## Task 4: Apply BdbCursorGuard to walletdb.cpp ListAccountCreditDebit()

**Files:**
- Modify: `src/wallet/walletdb.cpp:68-109`

**Context:** Cursor leaks if `entries.push_back()` at line 105 throws (unlikely but possible under memory pressure with heavy UTXO churn).

**Step 1: Apply RAII guard**

Replace raw `Dbc* pcursor = GetCursor()` with `BdbCursorGuard cursor(GetCursor())`.
Remove both explicit `pcursor->close()` calls (lines 89 and 108).
Change `ReadAtCursor(pcursor, ...)` → `ReadAtCursor(cursor.get(), ...)`.

**Step 2: Build and test**

Run: `cmake --build build/linux-release && cd build/linux-release && ctest --output-on-failure`
Expected: All 1,317 tests pass

**Step 3: Commit**

```bash
git add src/wallet/walletdb.cpp
git commit -m "Fix cursor leak in ListAccountCreditDebit via BdbCursorGuard"
```

---

## Task 5: Apply BdbCursorGuard to db.cpp Rewrite()

**Files:**
- Modify: `src/db.cpp:394-426`

**Context:** Cursor leaks if `pdbCopy->put()` or other loop body operations throw. Both explicit close calls (lines 403, 408) are inside `while` loop — an exception before reaching them leaks.

**Step 1: Apply RAII guard**

Add `#include "db_cursor_guard.h"` at top of db.cpp.
Replace `Dbc* pcursor = db.GetCursor()` with `BdbCursorGuard cursor(db.GetCursor())`.
Remove both explicit `pcursor->close()` calls inside the loop.
Change `db.ReadAtCursor(pcursor, ...)` → `db.ReadAtCursor(cursor.get(), ...)`.

**Step 2: Build and test**

Run: `cmake --build build/linux-release && cd build/linux-release && ctest --output-on-failure`
Expected: All 1,317 tests pass

**Step 3: Commit**

```bash
git add src/db.cpp
git commit -m "Fix cursor leak in CDB::Rewrite via BdbCursorGuard"
```

---

## Task 6: Apply BdbCursorGuard to rpc_wallet_mgmt.cpp clearwallettransactions

**Files:**
- Modify: `src/rpc/rpc_wallet_mgmt.cpp:438-533`

**Context:** CRITICAL — `throw std::runtime_error(cbuf)` at line 499 leaks cursor AND skips `TxnCommit()`. Transaction is also not aborted on error path.

**Step 1: Apply RAII guard and fix transaction safety**

Add `#include "db_cursor_guard.h"` at top.

Replace the raw cursor with RAII:
```cpp
// Before:
Dbc* pcursor = walletdb.GetTxnCursor();
if (!pcursor)
    throw std::runtime_error("Cannot get wallet DB cursor");
// ... loop with throw ...
pcursor->close();
walletdb.TxnCommit();

// After:
BdbCursorGuard cursor(walletdb.GetTxnCursor());
if (!cursor)
    throw std::runtime_error("Cannot get wallet DB cursor");
// ... loop (cursor auto-closed on throw) ...
// cursor closed by destructor
walletdb.TxnCommit();
```

Note: The throw at line 499 will now properly close the cursor via RAII before the exception propagates. The `TxnCommit()` is skipped on throw — BDB auto-aborts uncommitted transactions when `CWalletDB` destructor runs (activeTxn->abort() in ~CDB or txn expires).

**Step 2: Build and test**

Run: `cmake --build build/linux-release && cd build/linux-release && ctest --output-on-failure`
Expected: All 1,317 tests pass

**Step 3: Commit**

```bash
git add src/rpc/rpc_wallet_mgmt.cpp
git commit -m "Fix cursor leak in clearwallettransactions via BdbCursorGuard"
```

---

## Task 7: Apply unique_ptr to LevelDB iterators

**Files:**
- Modify: `src/txdb-leveldb.cpp:308-376`
- Modify: `src/qt/messagemodel.cpp:80-130`

**Context:** `txdb-leveldb.cpp` has `delete iterator` at two sites (lines 366, 376) but leaks on exception. `messagemodel.cpp` has similar raw `delete it` pattern. The rpcsmessage.cpp sites already use `std::unique_ptr` — match that pattern.

**Step 1: Fix txdb-leveldb.cpp LoadBlockIndex()**

```cpp
// Before:
leveldb::Iterator *iterator = pdb->NewIterator(leveldb::ReadOptions());
// ... loop ...
if (!pindexNew->CheckIndex()) {
    delete iterator;
    return error(...);
}
// ...
delete iterator;

// After:
std::unique_ptr<leveldb::Iterator> iterator(pdb->NewIterator(leveldb::ReadOptions()));
// ... loop (use iterator.get() or iterator-> directly — unique_ptr supports -> operator) ...
if (!pindexNew->CheckIndex()) {
    return error(...);  // iterator freed by unique_ptr
}
// iterator freed by unique_ptr at scope exit
```

**Step 2: Fix qt/messagemodel.cpp refreshMessageTable()**

Replace both raw `delete it` sites with `std::unique_ptr<leveldb::Iterator>`.

**Step 3: Build and test**

Run: `cmake --build build/linux-release && cd build/linux-release && ctest --output-on-failure`
Expected: All 1,317 tests pass

**Step 4: Commit**

```bash
git add src/txdb-leveldb.cpp src/qt/messagemodel.cpp
git commit -m "Fix iterator leaks in LoadBlockIndex and messagemodel via unique_ptr"
```

---

## Task 8: Migrate NewThread() from boost::thread to std::thread

**Files:**
- Modify: `src/util.cpp:1329-1339`
- Modify: `src/util.h:23,642-693`
- Modify: `src/util.cpp:13`

**Context:** `NewThread()` is the ONLY production boost::thread usage. All synchronization (mutexes, locks, sleep) already uses std. `LoopForever`/`TraceThread` templates catch `boost::thread_interrupted` but are never used in production code.

**Step 1: Migrate NewThread()**

```cpp
// Before (util.cpp:1329):
bool NewThread(void(*pfn)(void*), void* parg)
{
    try
    {
        boost::thread(pfn, parg); // thread detaches when out of scope
    } catch(boost::thread_resource_error &e) {
        printf("Error creating thread: %s\n", e.what());
        return false;
    }
    return true;
}

// After:
bool NewThread(void(*pfn)(void*), void* parg)
{
    try
    {
        std::thread t(pfn, parg);
        t.detach();
    } catch(const std::system_error& e) {
        printf("Error creating thread: %s\n", e.what());
        return false;
    }
    return true;
}
```

**Step 2: Update LoopForever and TraceThread templates**

Remove `catch (boost::thread_interrupted)` blocks from both templates (lines 659-663 and 682-686). These are dead code — no production code uses thread interruption, and std::thread has no interruption mechanism.

Update comments (lines 640-645) to remove boost::thread references.

Fix thread name prefix: `"blackcoin-%s"` → `"pinkcoin-%s"` at lines 648 and 674.

**Step 3: Remove boost::thread includes**

- `util.h:23`: Remove `#include <boost/thread.hpp>` (keep `#include <thread>` at line 26)
- `util.cpp:13`: Remove `#include <boost/thread.hpp>`

**Step 4: Check for other boost/thread.hpp includes that may break**

Run: `grep -rn 'boost/thread' src/ --include="*.h" --include="*.cpp" | grep -v leveldb/ | grep -v test/`

If any other files include `boost/thread.hpp` transitively via `util.h`, they need no change (they won't get it anymore, but they don't use it).

**Step 5: Build and test**

Run: `cmake --build build/linux-release && cd build/linux-release && ctest --output-on-failure`
Expected: All 1,317 tests pass

**Step 6: Commit**

```bash
git add src/util.cpp src/util.h
git commit -m "Migrate NewThread from boost::thread to std::thread"
```

---

## Task 9: Write security tests for RAII cursor guards

**Files:**
- Create: `src/test/security_tests.cpp`
- Modify: `src/test/CMakeLists.txt`

**Context:** Prove that RAII guards properly close cursors on all exit paths. Also test existing security enforcement (ban scoring, connection limits, message sizes).

**Step 1: Write the test file**

Test suites:

**Suite 1: `bdb_cursor_guard_tests`** (~12 tests)
- Guard closes cursor on normal scope exit
- Guard closes cursor on exception
- Guard with nullptr is safe (no crash on destruction)
- Guard bool conversion works
- Guard release() prevents auto-close
- LoadWallet succeeds with guard (no cursor leak)
- ListAccountCreditDebit succeeds with guard
- StakeDB LoadWallet succeeds with guard
- Guard is non-copyable (compile-time check — documented, not runtime)
- Multiple guards in nested scopes
- Guard after early return
- Rewrite with guard

**Suite 2: `leveldb_iterator_guard_tests`** (~4 tests)
- unique_ptr iterator cleaned up on scope exit
- unique_ptr iterator cleaned up on exception
- LoadBlockIndex uses unique_ptr correctly
- rpcsmessage already uses unique_ptr (regression guard)

**Suite 3: `network_security_tests`** (~8 tests)
- MAX_SIZE constant is 32 MB
- MAX_INV_SZ constant is 50,000
- MAX_BLOCK_SIZE constant is 1,000,000
- MAX_BLOCK_SIGOPS constant is 20,000
- Default ban threshold is 100 (GetArg default)
- Default ban time is 86400 (24 hours)
- CNode::Misbehaving accumulates score correctly
- MoneyRange rejects values outside [0, MAX_MONEY]

**Suite 4: `threading_tests`** (~4 tests)
- NewThread creates and detaches successfully
- fShutdown flag is accessible
- vnThreadsRunning array has THREAD_MAX entries
- THREAD_MAX == 10 (constant pinning)

**Step 2: Register in CMakeLists.txt**

Add `security_tests.cpp` to PINKCOIN_TEST_SOURCES.

**Step 3: Build and test**

Run: `cmake --build build/linux-release && cd build/linux-release && ctest --output-on-failure`
Expected: All tests pass (1,317 + ~28 new)

**Step 4: Commit**

```bash
git add src/test/security_tests.cpp src/test/CMakeLists.txt
git commit -m "Phase 6B: Security tests — RAII cursors, network constants, threading"
```

---

## Task 10: Windows MXE cross-compile verification

**Files:** None (build verification only)

**Step 1: Clean and rebuild Windows target**

```bash
rm -rf build/windows-mxe && cmake --preset windows-mxe && cmake --build build/windows-mxe
```

**Step 2: Verify timestamps**

```bash
echo "=== WINDOWS ===" && stat -c '%y' build/windows-mxe/src/pink2d.exe && stat -c '%y' build/windows-mxe/src/qt/Pinkcoin-Qt.exe
echo "=== NOW ===" && date '+%Y-%m-%d %H:%M:%S %z'
```

Both timestamps must be within current session.

**Step 3: Commit (if any Windows-specific fixes needed)**

---

## Task 11: Update memory files and coverage analysis

**Files:**
- Modify: `/home/lisa/.claude/projects/-mnt-projects-windows-Pink2/memory/MEMORY.md`
- Modify: `/home/lisa/.claude/projects/-mnt-projects-windows-Pink2/memory/coverage_analysis.md`

Update test counts, add Phase 6B row to tier summary, update coverage analysis with new security_tests.cpp file.

---

## Summary

| Task | Description | Risk |
|------|-------------|------|
| 1 | Create BdbCursorGuard header | None (new file) |
| 2 | Fix walletdb.cpp LoadWallet leak | CRITICAL fix |
| 3 | Fix stakedb.cpp LoadWallet leak | CRITICAL fix |
| 4 | Fix ListAccountCreditDebit leak | HIGH fix |
| 5 | Fix db.cpp Rewrite leak | HIGH fix |
| 6 | Fix clearwallettransactions leak | CRITICAL fix |
| 7 | Fix LevelDB iterator leaks | CRITICAL fix |
| 8 | Migrate boost::thread → std::thread | MODERATE |
| 9 | Security tests | None (new tests) |
| 10 | Windows build verification | None (build only) |
| 11 | Update memory files | None (documentation) |
