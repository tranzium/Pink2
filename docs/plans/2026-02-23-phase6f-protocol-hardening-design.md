# Phase 6F: Protocol Hardening — Design Document

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Eliminate CBigNum/OpenSSL BIGNUM from production code, modernize the thread model, and complete code hygiene — achieving zero `boost::` references, zero OpenSSL BIGNUM usage, zero `using namespace std`, and zero `void* parg` thread functions.

**Architecture:** Three independent work streams executed in dependency order: (A) CBigNum replacement with `arith_uint256` + `CScriptNum` + byte-array base58, (B) thread model overhaul with atomic counters and typed functions, (C) hygiene sweep for namespace pollution, legacy printf, and file path validation.

**Pre-condition:** Edge-case pinning tests written and passing BEFORE any production code changes.

---

## Section 1: Edge-Case Pinning Tests (Safety Net)

Before touching any production code, we write pinning tests that lock down current behavior. If any replacement introduces a bug, these tests catch it immediately.

**New test file:** `src/test/bignum_pinning_tests.cpp`

### SetCompact/GetCompact Roundtrip Tests (~10 tests)

- All 4 Pinkcoin difficulty limits: `bnProofOfWorkLimit`, `bnProofOfStakeLimit`, `bnProofOfFlashStakeLimit`, `bnProofOfWorkLimitTestNet`
- Edge cases: zero mantissa (`0x00000000`), max mantissa (`0x00ffffff`), sign bit set (`0x008000xx`), size=0, size=1, size=32
- Roundtrip: `SetCompact(x).GetCompact() == x` and `SetCompact(x).getuint256()` pinned to exact hex values

### Script Number Encoding Tests (~8 tests)

- CastToBigNum roundtrips for: 0, 1, -1, 127, -128, 255, -255, INT32_MAX, INT32_MIN+1
- `getvch()` output bytes pinned exactly (little-endian sign-magnitude format)
- 5-byte input rejects (`nMaxNumSize = 4` enforcement)

### Base58 Golden Tests (~5 tests)

- Known address encode/decode roundtrips (add edge cases beyond existing tests)
- Empty input, single-byte input, leading zero bytes (which map to '1' characters)

### PoS Target Multiplication Tests (~3 tests)

- Pin `(bnCoinDayWeight * bnTargetPerCoinDay).getuint256()` for known input values
- Pin the comparison `CBigNum(hash) > target` for a known pass and known fail case

**Total: ~26 new pinning tests.**

---

## Section 2: Stream A — CBigNum Elimination

Three replacement classes, each handling a distinct domain.

### 2a. `arith_uint256` — Difficulty Target Arithmetic

**New files:** `src/arith_uint256.h`, `src/arith_uint256.cpp`

Ported from Bitcoin Core 0.10+. A 256-bit unsigned integer with:

- `SetCompact(uint32_t)` / `GetCompact()` — compact difficulty encoding (same algorithm as current CBigNum)
- `operator*`, `operator>`, `operator<`, `operator==`, all comparisons
- Conversion: `UintToArith256(uint256)` / `ArithToUint256(arith_uint256)` — bridging between hash type and arithmetic type
- No division, no modulo (not needed for difficulty math)

**Files changed:**

- `src/kernel.cpp` — replace `CBigNum bnTargetPerCoinDay` with `arith_uint256`, use `SetCompact`, multiply, compare
- `src/main.cpp` — replace 5 global `CBigNum` difficulty limits with `arith_uint256`, replace block trust comparison
- `src/miner.cpp` — replace `SetCompact().getuint256()` with `ArithToUint256(arith_uint256().SetCompact(nBits))`
- `src/main.h` — change extern declarations from `CBigNum` to `arith_uint256`
- `src/consensus/rewards.cpp` and `src/consensus/validation.cpp` — update any `GetCompact()`/`SetCompact()` references

### 2b. `CScriptNum` — Script Interpreter Arithmetic

**New file:** `src/scriptnum.h`

Ported from Bitcoin Core (post-BIP62). Internally just an `int64_t` with:

- Constructor from `std::vector<unsigned char>` (script stack encoding: little-endian, sign in high bit of last byte)
- `getvch()` — encode back to script stack bytes
- `getint()` — clamped int return
- All arithmetic operators: `+`, `-`, `*`, `/`, `%`, negation, `==`, `!=`, `<`, `>`, `<=`, `>=`
- Shift operators: `<<`, `>>`
- Bool conversion: `operator==` against 0

**Files changed:**

- `src/script.cpp` — replace `CBigNum` constants (`bnZero`, `bnOne`, `bnTrue`, `bnFalse`) with `CScriptNum`. Replace `CastToBigNum()` with `CScriptNum(vch, nMaxNumSize)`. All opcode arithmetic uses `CScriptNum` operators.
- `src/script.h` — remove `#include "bignum.h"`, add `#include "scriptnum.h"`

### 2c. Base58 — Byte-Array Algorithm

**No new class.** Replace the `CBigNum` division/multiplication loops with Bitcoin Core's carry-based byte-array algorithm directly in `base58.h`.

**Files changed:**

- `src/base58.h` — rewrite `EncodeBase58()` and `DecodeBase58()` using carry arithmetic. Remove `#include "bignum.h"`.

### 2d. Genesis Coinbase & Miner Script

- `main.cpp:1244` — replace `CBigNum(42)` with `CScriptNum(42)`
- `miner.cpp:400` — replace `CBigNum(nExtraNonce)` with `CScriptNum(nExtraNonce)`

### 2e. Delete `bignum.h`

Once all consumers are migrated, delete `src/bignum.h` entirely. This removes the `#include <openssl/bn.h>` dependency from production code.

**Verification:** `grep -r "bignum\|CBigNum\|CAutoBN_CTX\|BN_" src/ --include="*.cpp" --include="*.h" | grep -v test/ | grep -v leveldb/` returns zero matches.

---

## Section 3: Stream B — Thread Model Overhaul

### 3a. Atomic Thread Counters

Replace `std::array<int, THREAD_MAX> vnThreadsRunning` with `std::array<std::atomic<int>, THREAD_MAX>`.

**Files changed:**

- `src/net.h` — change declaration to `std::atomic<int>`, add `#include <atomic>`
- `src/net/net.cpp` — change definition. Increment/decrement operators work directly on atomics.

This is the safety-critical fix — eliminates the data race. Done first, independently testable.

### 3b. Typed `NewThread` Replacement

Replace the current signature:

```cpp
bool NewThread(void(*pfn)(void*), void* parg)
```

With a variadic template:

```cpp
template<typename Callable, typename... Args>
bool NewThread(Callable&& func, Args&&... args)
{
    try {
        std::thread t(std::forward<Callable>(func), std::forward<Args>(args)...);
        t.detach();
    } catch (const std::system_error& e) {
        LogPrintf("Error creating thread: %s\n", e.what());
        return false;
    }
    return true;
}
```

**Files changed:**

- `src/util.cpp` — remove old `NewThread` definition
- `src/util.h` — add template definition (must be in header)

### 3c. Eliminate Wrapper Pattern

Replace paired functions (ThreadX wrapper + ThreadX2 real work) with single functions using an RAII guard:

```cpp
struct ThreadCountGuard {
    int index;
    ThreadCountGuard(int i) : index(i) { vnThreadsRunning[index]++; }
    ~ThreadCountGuard() { vnThreadsRunning[index]--; }
};
```

Then merge each pair into a single function:

```cpp
void ThreadSocketHandler()  // was ThreadSocketHandler + ThreadSocketHandler2
{
    ThreadCountGuard guard(THREAD_SOCKETHANDLER);
    RenameThread("pinkcoin-net");
    // ... actual work (was in ThreadSocketHandler2) ...
}
```

**18 thread functions become ~12** (eliminating the 6+ `2`-suffix wrappers).

**Files changed:**

- `src/net/net.cpp` — merge 8 wrapper pairs into 8 single functions
- `src/rpc/bitcoinrpc.cpp` — merge ThreadRPCServer/ThreadRPCServer2 pair
- `src/rpc/rpc_wallet_mgmt.cpp` — simplify ThreadTopUpKeyPool, ThreadCleanWalletPassphrase

### 3d. Type the Remaining `void*` Casts

| Function | Current Cast | New Signature |
|----------|-------------|---------------|
| `ThreadStakeMiner` | `(CWallet*)parg` | `void ThreadStakeMiner(CWallet* pwallet)` |
| `ThreadFlushWalletDB` | `((const string*)parg)[0]` | `void ThreadFlushWalletDB(const std::string& strFile)` |
| `ThreadFlushStakeDB` | `((const string*)parg)[0]` | `void ThreadFlushStakeDB(const std::string& strFile)` |
| `ThreadCleanWalletPassphrase` | `(int64_t*)parg` | `void ThreadCleanWalletPassphrase(int64_t nSleepMs)` |
| `ThreadRPCServer3` | `(AcceptedConnection*)parg` | `void ThreadRPCServer3(AcceptedConnection* conn)` |

All functions that pass `nullptr` become zero-parameter: `void ThreadX()`.

### 3e. Scope Boundary

Unchanged: `ThreadGroup`, `CSemaphore`/`CSemaphoreGrant` (already modern), `fShutdown` coordination (working, well-tested).

---

## Section 4: Stream C — Hygiene Sweep

### 4a. Remove `using namespace std;` — 24 Production Files

Mechanical transformation: remove the directive, prefix all unqualified std types with `std::`.

**Files (24):**

- Core: `main.cpp`, `init.cpp`, `kernel.cpp`, `script.cpp`, `db.cpp`, `util.cpp`, `miner.cpp`, `ntp.cpp`, `stakedb.cpp`, `txdb-leveldb.cpp`, `addrman.cpp`, `alert.cpp`, `checkpoints.cpp`
- Net: `net/net.cpp`, `net/netbase.cpp`
- RPC: `rpc/bitcoinrpc.cpp`, `rpc/rpcblockchain.cpp`, `rpc/rpcdump.cpp`, `rpc/rpcmining.cpp`, `rpc/rpcnet.cpp`, `rpc/rpcrawtransaction.cpp`, `rpc/rpcsmessage.cpp`
- Qt: `qt/coincontroldialog.cpp`

Also remove `using namespace asio;` from `rpc/bitcoinrpc.cpp` — prefix with `asio::`.

Done in bulk batches (5-6 files per commit) to keep diffs reviewable.

### 4b. Convert Remaining `printf` to `LogPrintf`

Two files still define `#define printf OutputDebugStringF`:

- `src/rpc/bitcoinrpc.cpp` (24 printf calls)
- `src/rpc/rpcdump.cpp` (11 printf calls)

Three more files have printf calls routed through this macro:

- `src/alert.cpp` (7 calls)
- `src/checkpoints.cpp` (9 calls)
- `src/kernel.cpp` (4 calls)

**Transformation:** Remove `#define printf OutputDebugStringF`, replace each `printf(...)` with `LogPrintf(...)`, add `#include "logging.h"` where missing. ~55 printf calls total.

### 4c. File Path Validation in dumpwallet/importwallet

Add path validation before `file.open()` in `src/rpc/rpcdump.cpp`:

```cpp
std::filesystem::path ValidateWalletFilePath(const std::string& input)
{
    namespace fs = std::filesystem;
    fs::path p = fs::weakly_canonical(fs::path(input));

    fs::path dataDir = fs::weakly_canonical(GetDataDir());
    fs::path cwd = fs::weakly_canonical(fs::current_path());

    if (!strutil::starts_with(p.string(), dataDir.string()) &&
        !strutil::starts_with(p.string(), cwd.string()))
        throw JSONRPCError(RPC_INVALID_PARAMETER,
            "File path must be within the data directory or current directory");

    return p;
}
```

Apply to both `importwallet` (line 161) and `dumpwallet` (line 273).

### 4d. Scope Boundary

Not in scope: variable/function renaming, error handling refactoring, `CCriticalSection`/`LOCK` macro changes.

---

## Section 5: Ordering, Dependencies, and Verification

### 5a. Task Ordering

```
Stream C (Hygiene)     Stream A (BigNum)          Stream B (Threading)
─────────────────     ──────────────────          ────────────────────
                      1. Pinning tests
                         │
2. printf→LogPrintf   3. arith_uint256 class
   (removes #define      │
    printf from files  4. CScriptNum class
    that Stream A         │
    also touches)      5. Base58 byte-array
                         │
                      6. Migrate kernel.cpp
                         │
                      7. Migrate main.cpp
                         │
                      8. Migrate miner.cpp        9. Atomic counters
                         │                           │
                      10. Migrate script.cpp      11. Typed NewThread
                         │                           │
                      12. Migrate base58.h        13. Merge wrapper pairs
                         │                           │
                      14. Delete bignum.h          15. Type void* casts
                                                     │
16. using-namespace-std removal (bulk, last — touches almost every file)
   │
17. File path validation (rpcdump.cpp)
   │
18. Final build verification (both targets)
```

**Key dependency:** printf→LogPrintf (task 2) before migrating kernel.cpp (task 6) — both touch the same file, separate concerns into separate commits.

**Key independence:** Stream B (threading) is fully independent of Stream A (bignum) — different files.

**using-namespace-std goes last** — almost every other task touches these same files.

### 5b. Commit Strategy

One commit per logical unit:

1. Pinning tests (safety net before any production change)
2. printf→LogPrintf completion (~55 calls across 5 files)
3. arith_uint256 class (new files only)
4. CScriptNum class (new file only)
5. Base58 byte-array rewrite (base58.h only)
6. Migrate difficulty math (kernel.cpp + main.cpp + miner.cpp + main.h)
7. Migrate script interpreter (script.cpp + script.h)
8. Delete bignum.h
9. Atomic counters + ThreadCountGuard
10. Typed NewThread + merge wrappers + type void* casts
11. using-namespace-std removal (2-3 commits if diff is large)
12. File path validation

### 5c. Verification Gates

**After every commit:** Linux build + all tests pass.

**After task 1 (pinning tests):** Run new tests in isolation to confirm they pass against current CBigNum implementation. These become the regression safety net.

**After task 8 (delete bignum.h):** `grep -r "bignum\|CBigNum\|CAutoBN_CTX\|BN_" src/ --include="*.cpp" --include="*.h" | grep -v test/ | grep -v leveldb/` returns zero. Full rebuild of both targets.

**After task 10 (threading done):** `grep -r "void\*.*parg" src/ --include="*.cpp" --include="*.h" | grep -v test/ | grep -v leveldb/` returns zero.

**After task 12 (all done):** Full clean rebuild of both Linux and Windows MXE. All tests pass. Verification checklist:

- Zero `CBigNum` in production code
- Zero `using namespace std` in production code
- Zero `#define printf` in production code
- Zero `void* parg` thread functions in production code
- `vnThreadsRunning` is `std::atomic<int>`
- File path validation on dumpwallet/importwallet

### 5d. Test Count Target

Current: 1,617 tests. New pinning tests add ~26. Final target: **~1,643 tests**, zero failures.

---

## Risk Assessment

| Stream | Risk | Mitigation |
|--------|------|------------|
| A (BigNum) | Consensus divergence | Pinning tests written first; arith_uint256/CScriptNum are Bitcoin Core-proven; golden tests verify wire format |
| A (BigNum) | SetCompact edge cases | Dedicated edge-case tests for zero/max/sign-bit compact values |
| A (Base58) | Address encoding change | Existing base58_tests.cpp with 5 suites + new edge-case tests |
| B (Threading) | Race during transition | Atomic counters deployed first (standalone fix); wrapper merge is mechanical |
| C (Hygiene) | Typo in std:: prefix | Build catches immediately; zero behavior change |
| C (File path) | Overly restrictive validation | Only blocks paths outside datadir/cwd; existing usage patterns preserved |

**Overall risk: LOW.** All three streams use well-proven patterns from Bitcoin Core. The pinning tests provide a safety net that catches any implementation error before it reaches production.
