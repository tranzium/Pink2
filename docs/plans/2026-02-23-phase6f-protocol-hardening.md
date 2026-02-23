# Phase 6F: Protocol Hardening — Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Eliminate CBigNum/OpenSSL BIGNUM from production code, modernize the thread model, and complete code hygiene — zero `CBigNum`, zero `using namespace std`, zero `#define printf`, zero `void* parg` thread functions.

**Architecture:** Three work streams: (A) CBigNum elimination with `arith_uint256` + `CScriptNum` + byte-array base58, (B) thread model overhaul with atomic counters + typed `NewThread` + wrapper merge, (C) hygiene sweep. Edge-case pinning tests first as safety net.

**Tech Stack:** C++17, Boost.Test (test framework only), OpenSSL (EVP/AES only after bignum removal), standalone Asio, Qt5

---

## Task 1: Edge-Case Pinning Tests (Safety Net)

Write pinning tests that lock down current CBigNum behavior before any production changes.

**Files:**
- Create: `src/test/bignum_pinning_tests.cpp`
- Modify: `src/test/CMakeLists.txt` — add `bignum_pinning_tests.cpp` to `PINKCOIN_TEST_SOURCES`

**Context:** The codebase currently uses `CBigNum` (OpenSSL BIGNUM wrapper in `src/bignum.h`) for difficulty targets, script arithmetic, and base58 encoding. We're about to replace it with three lighter classes. These tests pin the exact behavior so any replacement bug is caught instantly.

**What to test (minimum ~26 tests across 4 suites):**

**Suite: compact_encoding_pinning** (~10 tests)
- Roundtrip all 4 Pinkcoin difficulty limits — construct `CBigNum` from `uint256`, call `GetCompact()`, pin the exact `uint32_t` value, then `SetCompact()` back and verify `getuint256()` matches original:
  - `bnProofOfWorkLimit = CBigNum(~uint256(0) >> 20)` — pin GetCompact result
  - `bnProofOfStakeLimit = CBigNum(~uint256(0) >> 10)` — pin GetCompact result
  - `bnProofOfFlashStakeLimit = CBigNum(~uint256(0) >> 10)` — pin GetCompact result
  - `bnProofOfWorkLimitTestNet = CBigNum(~uint256(0) >> 16)` — pin GetCompact result
- Edge cases for `SetCompact`/`GetCompact`:
  - Zero: `SetCompact(0x00000000).IsZero()` should be true
  - Size=1 mantissa: `SetCompact(0x01003456)` — pin getuint256 hex
  - Max mantissa: `SetCompact(0x05009234)` — pin getuint256 hex
  - High-bit mantissa (sign bit): `SetCompact(0x04800000)` — verify behavior (should be treated as negative in original Bitcoin, but GetCompact strips sign)
  - Large size byte: `SetCompact(0x20ffffff)` — pin getuint256 hex
  - Roundtrip: For each, verify `SetCompact(x).GetCompact()` roundtrips correctly (note: some values are not perfectly roundtrippable due to normalization — pin the actual behavior)

**Suite: script_number_pinning** (~8 tests)
- `CBigNum(0).getvch()` → empty vector `{}`
- `CBigNum(1).getvch()` → `{0x01}`
- `CBigNum(-1).getvch()` → `{0x81}`
- `CBigNum(127).getvch()` → `{0x7f}`
- `CBigNum(128).getvch()` → `{0x80, 0x00}`
- `CBigNum(255).getvch()` → `{0xff, 0x00}`
- `CBigNum(-255).getvch()` → `{0xff, 0x80}`
- `CBigNum(-128).getvch()` → `{0x80, 0x80}`
- `CastToBigNum` with 5-byte input → throws `runtime_error` (nMaxNumSize=4 enforcement)
- Roundtrip: construct CBigNum from int, getvch, setvch back, getint — verify same value

**Suite: base58_edge_pinning** (~5 tests)
- Empty input: `EncodeBase58({})` → `""`
- Single zero byte: `EncodeBase58({0x00})` → `"1"` (leading zero maps to '1')
- Multiple leading zeros: `EncodeBase58({0x00, 0x00, 0x01})` → pin exact string
- Known Pinkcoin address roundtrip: encode → decode → compare bytes
- Known Bitcoin Core test vector: `EncodeBase58({0x00, 0x00, 0x00, 0x00, 0x00})` → `"11111"`

**Suite: pos_target_pinning** (~3 tests)
- Construct `bnTargetPerCoinDay` via `SetCompact(bnProofOfStakeLimit.GetCompact())`, construct `bnCoinDayWeight` from a known int64_t (e.g., 1000000), compute `(bnCoinDayWeight * bnTargetPerCoinDay).getuint256()` → pin exact uint256 hex
- Comparison test: construct `CBigNum(knownHash)`, verify `> bnCoinDayWeight * bnTargetPerCoinDay` returns expected bool for a known pass and known fail case
- Multiply-then-GetCompact: `(CBigNum(1000) * CBigNum(~uint256(0) >> 20)).GetCompact()` → pin value

**Build and verify:**
```bash
rm -rf build/linux-release && cmake --preset linux-release && cmake --build build/linux-release
cd build/linux-release && ctest --output-on-failure
```

All new pinning tests must pass against the current CBigNum implementation.

**Commit:** `Phase 6F: Add edge-case pinning tests for CBigNum replacement safety net`

---

## Task 2: Convert Remaining printf to LogPrintf

Complete the Phase 6C logging migration for the 5 files that still use the `#define printf OutputDebugStringF` pattern.

**Files:**
- Modify: `src/rpc/bitcoinrpc.cpp` — remove `#define printf OutputDebugStringF` (line 29), remove `#undef printf` (line 14), add `#include "logging.h"`, replace ~24 `printf(...)` calls with `LogPrintf(...)`
- Modify: `src/rpc/rpcdump.cpp` — remove `#define printf OutputDebugStringF` (line 19), add `#include "logging.h"`, replace ~11 `printf(...)` calls with `LogPrintf(...)`
- Modify: `src/alert.cpp` — add `#include "logging.h"` if missing, replace ~7 `printf(...)` calls with `LogPrintf(...)`
- Modify: `src/checkpoints.cpp` — add `#include "logging.h"` if missing, replace ~9 `printf(...)` calls with `LogPrintf(...)`
- Modify: `src/kernel.cpp` — add `#include "logging.h"` if missing, replace ~4 `printf(...)` calls with `LogPrintf(...)`

**Context:** Phase 6C converted 311 printf calls across 13 files. These 5 files were missed. The `#define printf OutputDebugStringF` macro in bitcoinrpc.cpp and rpcdump.cpp redirects `printf` to the debug log — we replace this indirection with direct `LogPrintf` calls. The other 3 files (alert, checkpoints, kernel) have `printf` calls that route through this macro via their include chain.

**Transformation pattern:**
```cpp
// Before:
printf("CheckStakeKernelHash() : using modifier 0x%016"  PRIx64" at height=%d\n", nStakeModifier, nHeight);
// After:
LogPrintf("CheckStakeKernelHash() : using modifier 0x%016"  PRIx64" at height=%d\n", nStakeModifier, nHeight);
```

The format strings and arguments are identical — just change the function name. Keep `.c_str()` calls (LogPrintf is format-compatible).

Also convert any `printf` calls in `src/util.h` templates `LoopForever` and `TraceThread` (lines 676, 699, 701) to `LogPrintf`.

**Verification:** `grep -rn "#define printf\|OutputDebugStringF" src/ --include="*.cpp" --include="*.h" | grep -v test/ | grep -v leveldb/` returns zero.

**Build and test.**

**Commit:** `Phase 6F: Complete printf to LogPrintf migration — 5 remaining files`

---

## Task 3: Implement arith_uint256 Class

Port Bitcoin Core's `arith_uint256` — a 256-bit unsigned integer for difficulty target arithmetic.

**Files:**
- Create: `src/arith_uint256.h`
- Create: `src/arith_uint256.cpp`
- Modify: `src/CMakeLists.txt` — add `arith_uint256.cpp` to `PINKCOIN_CORE_SOURCES`

**Context:** This replaces CBigNum for difficulty calculations. It needs: `SetCompact(uint32_t)`, `GetCompact()`, `operator*`, all comparisons, `IsZero()`, and bridge functions `UintToArith256(uint256)` / `ArithToUint256(arith_uint256)`.

**Implementation source:** Port from Bitcoin Core's `src/arith_uint256.h` and `src/arith_uint256.cpp` (tag v0.15.0 or later). The class stores 256 bits as `uint32_t pn[8]` (little-endian limbs). Key methods:

- `SetCompact(uint32_t nCompact, bool* pfNegative = nullptr, bool* pfOverflow = nullptr)` — decodes compact target encoding. The mantissa is in bits 0-23, the exponent (byte count) is in bits 24-31. Sign bit is bit 23.
- `GetCompact(bool fNegative = false)` — encodes back to compact form.
- `operator*=(const arith_uint256&)` — 256×256 multiplication using 32-bit limb schoolbook method.
- All comparison operators delegate to `CompareTo()`.
- `IsZero()` — checks all limbs are 0.
- `UintToArith256` / `ArithToUint256` — memcpy between `uint256` (a raw 32-byte hash) and `arith_uint256` (an arithmetic type). Both have the same in-memory layout (little-endian).

**Critical detail:** `SetCompact`/`GetCompact` must match CBigNum's behavior exactly. The pinning tests from Task 1 verify this.

**No production code changes yet** — just the new files. The tests can include unit tests for arith_uint256 itself in the pinning test file or a new file.

**Build and test** (new class compiles, links, existing tests still pass).

**Commit:** `Phase 6F: Add arith_uint256 class for difficulty target arithmetic`

---

## Task 4: Implement CScriptNum Class

Port Bitcoin Core's `CScriptNum` — a lightweight int64_t wrapper for script interpreter arithmetic.

**Files:**
- Create: `src/scriptnum.h`

**Context:** This replaces CBigNum for all script opcode arithmetic. Script numbers are limited to 4 bytes (nMaxNumSize=4), so int64_t is more than sufficient. The critical part is the encoding: little-endian, sign-magnitude with the sign in the high bit of the last byte.

**Implementation source:** Port from Bitcoin Core's `src/scriptnum.h` (tag v0.15.0 or later). Key elements:

- Constructor: `CScriptNum(const std::vector<unsigned char>& vch, size_t nMaxNumSize)` — decodes script number encoding, throws `scriptnum_error` if `vch.size() > nMaxNumSize`
- Constructor: `CScriptNum(int64_t n)` — wraps an integer
- `getvch()` — encodes int64_t back to script stack bytes (little-endian sign-magnitude)
- `getint()` — returns clamped int (INT_MAX/INT_MIN for overflow)
- All arithmetic operators: `+`, `-`, unary `-`, `*`, `/`, `%`
- All comparison operators: `==`, `!=`, `<`, `<=`, `>`, `>=`
- Comparisons against `int64_t` directly (e.g., `sn == 0`)
- `static int64_t set_vch(const std::vector<unsigned char>&)` — internal decode helper
- `static std::vector<unsigned char> serialize(int64_t)` — internal encode helper
- Exception class: `class scriptnum_error : public std::runtime_error`

**Encoding format (must match CBigNum::getvch/setvch for values within nMaxNumSize):**
- `0` → `{}` (empty)
- `1` → `{0x01}`
- `-1` → `{0x81}` (high bit of last byte is sign)
- `128` → `{0x80, 0x00}` (needs extra byte for sign since 0x80 bit is used)
- `-128` → `{0x80, 0x80}`

The pinning tests from Task 1 verify encoding compatibility.

**No production code changes yet.**

**Build and test** (header-only, compiles, links, existing tests still pass).

**Commit:** `Phase 6F: Add CScriptNum class for script interpreter arithmetic`

---

## Task 5: Rewrite Base58 with Byte-Array Algorithm

Replace the CBigNum-based `EncodeBase58` and `DecodeBase58` with Bitcoin Core's carry-based byte-array algorithm.

**Files:**
- Modify: `src/base58.h` — rewrite `EncodeBase58(const unsigned char*, const unsigned char*)` and `DecodeBase58(const char*, std::vector<unsigned char>&)`. Remove `#include "bignum.h"`.

**Context:** The current implementation uses `CBigNum` for base conversion (divide by 58, multiply by 58). Bitcoin Core replaced this with a pure byte-array algorithm that works directly on the input bytes using carry arithmetic — no big number class needed.

**Implementation source:** Port from Bitcoin Core's `src/base58.cpp` (`EncodeBase58` and `DecodeBase58` functions, tag v0.15.0 or later).

**EncodeBase58 algorithm:**
```cpp
inline std::string EncodeBase58(const unsigned char* pbegin, const unsigned char* pend)
{
    // Skip leading zeroes, count them
    int zeroes = 0;
    int length = 0;
    while (pbegin != pend && *pbegin == 0) {
        pbegin++;
        zeroes++;
    }
    // Allocate enough space in big-endian base58 representation
    int size = (pend - pbegin) * 138 / 100 + 1; // log(256) / log(58), rounded up
    std::vector<unsigned char> b58(size);
    while (pbegin != pend) {
        int carry = *pbegin;
        int i = 0;
        // Apply "b58 = b58 * 256 + ch"
        for (auto it = b58.rbegin(); (carry != 0 || i < length) && it != b58.rend(); it++, i++) {
            carry += 256 * (*it);
            *it = carry % 58;
            carry /= 58;
        }
        length = i;
        pbegin++;
    }
    // Skip leading zeroes in base58 result
    auto it = b58.begin() + (size - length);
    while (it != b58.end() && *it == 0) it++;
    // Translate to base58 characters
    std::string str;
    str.reserve(zeroes + (b58.end() - it));
    str.assign(zeroes, '1');
    while (it != b58.end())
        str += pszBase58[*(it++)];
    return str;
}
```

**DecodeBase58 algorithm:** Similar carry-based reverse. For each base58 character, multiply accumulator by 58 and add digit value, then convert from base58 digits to base256 bytes.

**Key:** Remove `#include "bignum.h"` from base58.h. Also remove `CAutoBN_CTX` usage. Keep all other includes (`key.h`, `script.h`, `allocators.h`).

**The existing base58_tests.cpp (5 suites, 21 references)** plus the new pinning tests from Task 1 verify byte-for-byte compatibility.

**Build and test.**

**Commit:** `Phase 6F: Rewrite base58 encoding with byte-array algorithm — no CBigNum`

---

## Task 6: Migrate Difficulty Math to arith_uint256

Replace all CBigNum usage in kernel.cpp, main.cpp, miner.cpp, and main.h with arith_uint256.

**Files:**
- Modify: `src/main.h:8` — change `#include "bignum.h"` to `#include "arith_uint256.h"`
- Modify: `src/main.cpp:39-44` — change 5 global CBigNum declarations to arith_uint256:
  ```cpp
  arith_uint256 bnProofOfWorkLimit;       // initialized in InitBlockIndex or similar
  arith_uint256 bnProofOfStakeLimit;
  arith_uint256 bnProofOfFlashStakeLimit;
  arith_uint256 bnProofOfWorkLimitTestNet;
  arith_uint256 nBaseStakeTrust;
  ```
  Initialize them using `UintToArith256(~uint256(0) >> 20)` etc. (the `~uint256(0) >> 20` expression produces a uint256, then convert to arith_uint256).
- Modify: `src/main.cpp:965-979` — replace checkpoint difficulty check:
  ```cpp
  arith_uint256 bnNewBlock;
  bnNewBlock.SetCompact(pblock->nBits);
  arith_uint256 bnRequired;
  // ... SetCompact calls same as before ...
  if (bnNewBlock > bnRequired) { ... }
  ```
- Modify: `src/main.cpp:1244` — replace `CBigNum(42)` with `CScriptNum(42)` (add `#include "scriptnum.h"`)
- Modify: `src/main.cpp:1260` — replace `CBigNum().SetCompact(block.nBits).getuint256()` with `ArithToUint256(arith_uint256().SetCompact(block.nBits))`
- Modify: `src/kernel.cpp:275-335` — replace all CBigNum usage:
  ```cpp
  arith_uint256 bnTargetPerCoinDay;
  bnTargetPerCoinDay.SetCompact(nBits);
  // ...
  arith_uint256 bnCoinDayWeight = UintToArith256(uint256(bnCoinDayWeight_Calc));
  // ... or construct directly if arith_uint256 has int64_t constructor
  targetProofOfStake = ArithToUint256(bnCoinDayWeight * bnTargetPerCoinDay);
  // ...
  if (UintToArith256(hashProofOfStake) > bnCoinDayWeight * bnTargetPerCoinDay)
      return false;
  ```
- Modify: `src/miner.cpp:400` — replace `CBigNum(nExtraNonce)` with `CScriptNum(nExtraNonce)` (add `#include "scriptnum.h"`)
- Modify: `src/miner.cpp:455` — replace `CBigNum().SetCompact(pblock->nBits).getuint256()` with `ArithToUint256(arith_uint256().SetCompact(pblock->nBits))`
- Modify: `src/consensus/rewards.cpp` and `src/consensus/validation.cpp` — check for any `CBigNum` or `GetCompact()`/`SetCompact()` usage and update. These may reference `bnProofOfWorkLimit` etc. through `main.h`.

**Verification:** After this task, `grep -rn "CBigNum" src/kernel.cpp src/main.cpp src/miner.cpp src/main.h` returns zero.

**Build and test.** The pinning tests from Task 1 must all still pass — this is the critical verification.

**Commit:** `Phase 6F: Migrate difficulty math from CBigNum to arith_uint256`

---

## Task 7: Migrate Script Interpreter to CScriptNum

Replace all CBigNum usage in script.cpp and script.h with CScriptNum.

**Files:**
- Modify: `src/script.h` — remove `#include "bignum.h"`, add `#include "scriptnum.h"`
- Modify: `src/script.cpp:6-36` — replace includes, constants, and CastToBigNum:
  ```cpp
  // Remove: #include "bignum.h"
  // Add: #include "scriptnum.h"

  static const CScriptNum bnZero(0);
  static const CScriptNum bnOne(1);
  static const CScriptNum bnFalse(0);
  static const CScriptNum bnTrue(1);

  // Replace CastToBigNum:
  CScriptNum CastToBigNum(const valtype& vch)
  {
      if (vch.size() > nMaxNumSize)
          throw std::runtime_error("CastToBigNum() : overflow");
      return CScriptNum(vch, nMaxNumSize);
  }
  ```
  Note: Keep the function name `CastToBigNum` for now to minimize diff — it's called throughout the interpreter. The name is misleading after migration but renaming is out of scope.
- Modify: `src/script.cpp` opcode handlers — everywhere that constructs `CBigNum(intValue)` changes to `CScriptNum(intValue)`, everywhere that calls `.getvch()` stays the same (CScriptNum has the same method).

**Key changes in opcode handlers:**
- Line 396: `CBigNum bn(static_cast<int>(opcode) - ...)` → `CScriptNum bn(static_cast<int>(opcode) - ...)`
- Line 572: `CBigNum bn(stack.size())` → `CScriptNum bn(static_cast<int64_t>(stack.size()))`
- Line 728: `CBigNum bn(stacktop(-1).size())` → `CScriptNum bn(static_cast<int64_t>(stacktop(-1).size()))`
- Lines 826, 866-868, 895, 901, 938-940: Same pattern — `CBigNum` → `CScriptNum`, `CastToBigNum` stays
- Line 895: `CBigNum(2048)` → `CScriptNum(2048)` (shift limit check)

**Script test vectors** (`data/script_valid.json`, `data/script_invalid.json`) exercise all opcodes. The script_tests.cpp suite runs these vectors. Plus the script_number_pinning tests from Task 1 verify encoding compatibility.

**Build and test.**

**Commit:** `Phase 6F: Migrate script interpreter from CBigNum to CScriptNum`

---

## Task 8: Delete bignum.h

Remove the CBigNum class now that all production consumers are migrated.

**Files:**
- Delete: `src/bignum.h`
- Modify: Any remaining files that `#include "bignum.h"` — remove the include. After Tasks 5-7, the remaining includers should only be test files. Check:
  - `src/main.h` — should already be changed to `arith_uint256.h` in Task 6
  - `src/script.h` — should already be changed to `scriptnum.h` in Task 7
  - `src/base58.h` — should already have bignum.h removed in Task 5

**Verification:**
```bash
grep -rn "bignum\|CBigNum\|CAutoBN_CTX\|BN_" src/ --include="*.cpp" --include="*.h" | grep -v test/ | grep -v leveldb/
```
Must return zero matches.

**Note:** Test files may still reference `bignum.h` (e.g., `bignum_tests.cpp`, the new pinning tests). The pinning tests should be updated to test through the new classes instead, OR `bignum.h` can be kept as a test-only include. Preferred approach: update pinning tests to use the new classes (arith_uint256, CScriptNum, EncodeBase58/DecodeBase58) — this validates the replacements directly.

**Build both targets and test.**

**Commit:** `Phase 6F: Delete bignum.h — OpenSSL BIGNUM eliminated from production code`

---

## Task 9: Atomic Thread Counters + ThreadCountGuard

Fix the data race in `vnThreadsRunning` and add the RAII guard for thread counting.

**Files:**
- Modify: `src/net.h:101` — change `extern std::array<int, THREAD_MAX> vnThreadsRunning;` to `extern std::array<std::atomic<int>, THREAD_MAX> vnThreadsRunning;`. Add `#include <atomic>`.
- Modify: `src/net/net.cpp:56` — change definition to `std::array<std::atomic<int>, THREAD_MAX> vnThreadsRunning;` (default-initialized to 0).
- Create: `src/thread_guard.h` — the RAII guard:
  ```cpp
  #ifndef PINKCOIN_THREAD_GUARD_H
  #define PINKCOIN_THREAD_GUARD_H

  #include "net.h"  // for vnThreadsRunning, threadId

  struct ThreadCountGuard {
      int index;
      explicit ThreadCountGuard(int i) : index(i) { vnThreadsRunning[index]++; }
      ~ThreadCountGuard() { vnThreadsRunning[index]--; }
      ThreadCountGuard(const ThreadCountGuard&) = delete;
      ThreadCountGuard& operator=(const ThreadCountGuard&) = delete;
  };

  #endif
  ```

**Compatibility note:** `std::atomic<int>` supports `++` and `--` operators, so all existing `vnThreadsRunning[X]++` / `vnThreadsRunning[X]--` code compiles without change. The comparison `vnThreadsRunning[X] < 1` also works (atomic has implicit conversion to int via `.load()`).

**Build and test.**

**Commit:** `Phase 6F: Make vnThreadsRunning atomic and add ThreadCountGuard RAII`

---

## Task 10: Typed NewThread + Merge Wrappers + Type void* Casts

Modernize the thread creation pattern across the codebase.

**Files:**
- Modify: `src/util.h:602` — replace `bool NewThread(void(*pfn)(void*), void* parg);` declaration with:
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
- Modify: `src/util.cpp:1328-1339` — remove old `NewThread` definition (now a template in the header)
- Modify: `src/net/net.cpp` — for each wrapper/inner pair:
  1. Merge the wrapper (e.g., `ThreadSocketHandler`) and inner (e.g., `ThreadSocketHandler2`) into a single function
  2. Use `ThreadCountGuard` from Task 9 instead of manual increment/decrement
  3. Remove the `void* parg` parameter if unused
  4. Remove forward declarations for the eliminated `2` functions

  **Pairs to merge in net.cpp:**
  - `ThreadSocketHandler` + `ThreadSocketHandler2` → `ThreadSocketHandler()`
  - `ThreadMapPort` + `ThreadMapPort2` → `ThreadMapPort()`
  - `ThreadDNSAddressSeed` + `ThreadDNSAddressSeed2` → `ThreadDNSAddressSeed()`
  - `ThreadOpenConnections` + `ThreadOpenConnections2` → `ThreadOpenConnections()`
  - `ThreadOpenAddedConnections` + `ThreadOpenAddedConnections2` → `ThreadOpenAddedConnections()`
  - `ThreadMessageHandler` + `ThreadMessageHandler2` → `ThreadMessageHandler()`
  - `ThreadDumpAddress` + `ThreadDumpAddress2` → `ThreadDumpAddress()`
  - `ThreadStakeMiner(void* parg)` → `ThreadStakeMiner(CWallet* pwallet)` (uses parg as CWallet*)

- Modify: `src/rpc/bitcoinrpc.cpp`:
  - Merge `ThreadRPCServer` + `ThreadRPCServer2` → `ThreadRPCServer()`
  - Change `ThreadRPCServer3(void* parg)` → `ThreadRPCServer3(AcceptedConnection* conn)` (update the call site where it was spawned with `NewThread`)

- Modify: `src/rpc/rpc_wallet_mgmt.cpp`:
  - `ThreadTopUpKeyPool(void* parg)` → `ThreadTopUpKeyPool()` (parg unused)
  - `ThreadCleanWalletPassphrase(void* parg)` → `ThreadCleanWalletPassphrase(int64_t nSleepMs)` (parg was cast to `int64_t*`)
  - Update the call site that creates `new int64_t(nSleep)` — instead pass `nSleepMs` directly to `NewThread(ThreadCleanWalletPassphrase, nSleepMs)`

- Modify: `src/wallet/walletdb.cpp`:
  - `ThreadFlushWalletDB(void* parg)` → `ThreadFlushWalletDB(const std::string& strFile)` (parg was `(const string*)parg[0]`)
  - Update call site

- Modify: `src/stakedb.cpp`:
  - `ThreadFlushStakeDB(void* parg)` → `ThreadFlushStakeDB(const std::string& strFile)` (same pattern)
  - Update call site

- Modify: `src/smessage.cpp`:
  - `ThreadSecureMsg(void* parg)` → `ThreadSecureMsg()` (parg unused)
  - `ThreadSecureMsgPow(void* parg)` → `ThreadSecureMsgPow()` (parg unused)

- Modify: `src/init.cpp`:
  - `ExitTimeout(void* parg)` → `ExitTimeout()` (parg unused)
  - `Shutdown(void* parg)` → `Shutdown()` (parg unused)
  - Update all `NewThread(func, nullptr)` calls to `NewThread(func)`

- Modify: header declarations in `src/bitcoinrpc.h`, `src/db.h`, `src/net.h` — update forward declarations to match new signatures

**Pattern for merging a wrapper pair:**
```cpp
// BEFORE: Two functions
void ThreadSocketHandler(void* parg)  // wrapper
{
    IMPLEMENT_RANDOMIZE_STACK(ThreadSocketHandler(parg));
    try { vnThreadsRunning[THREAD_SOCKETHANDLER]++;
          ThreadSocketHandler2(parg);
          vnThreadsRunning[THREAD_SOCKETHANDLER]--;
    } catch (...) { vnThreadsRunning[THREAD_SOCKETHANDLER]--; throw; }
}
void ThreadSocketHandler2(void* parg) { /* actual work */ }

// AFTER: Single function
void ThreadSocketHandler()
{
    ThreadCountGuard guard(THREAD_SOCKETHANDLER);
    RenameThread("pinkcoin-net");
    // ... actual work (was in ThreadSocketHandler2) ...
}
```

**Verification:** `grep -rn "void\*.*parg" src/ --include="*.cpp" --include="*.h" | grep -v test/ | grep -v leveldb/` returns zero.

**Build and test.**

**Commit:** `Phase 6F: Modernize thread model — typed NewThread, RAII guards, no void* casts`

---

## Task 11: Remove using-namespace-std — Core Files (Batch 1 of 3)

Remove `using namespace std;` from 8 core daemon files.

**Files:**
- Modify: `src/main.cpp` — remove `using namespace std;` (line 22), add `std::` prefix to all unqualified std types
- Modify: `src/init.cpp` — same
- Modify: `src/kernel.cpp` — same
- Modify: `src/db.cpp` — same
- Modify: `src/util.cpp` — same
- Modify: `src/miner.cpp` — same
- Modify: `src/script.cpp` — same
- Modify: `src/ntp.cpp` — same

**Common types that need `std::` prefix:** `string`, `vector`, `map`, `set`, `multimap`, `pair`, `make_pair`, `runtime_error`, `min`, `max`, `sort`, `reverse`, `reverse_copy`, `ifstream`, `ofstream`, `cerr`, `cout`, `endl`, `hex`, `setw`, `tuple`, `get`.

**Transformation is mechanical:** Search for each unqualified type name, add `std::` prefix. The compiler catches any missed ones.

**Build and test.**

**Commit:** `Phase 6F: Remove using-namespace-std from 8 core daemon files`

---

## Task 12: Remove using-namespace-std — Network + Consensus Files (Batch 2 of 3)

**Files:**
- Modify: `src/net/net.cpp` — remove `using namespace std;` (line 23)
- Modify: `src/net/netbase.cpp` — same
- Modify: `src/addrman.cpp` — same
- Modify: `src/alert.cpp` — same
- Modify: `src/checkpoints.cpp` — same
- Modify: `src/stakedb.cpp` — same
- Modify: `src/txdb-leveldb.cpp` — same

Same mechanical transformation as Task 11.

**Build and test.**

**Commit:** `Phase 6F: Remove using-namespace-std from 7 network and consensus files`

---

## Task 13: Remove using-namespace-std — RPC + Qt Files (Batch 3 of 3)

**Files:**
- Modify: `src/rpc/bitcoinrpc.cpp` — remove `using namespace std;` AND `using namespace asio;` — prefix asio types with `asio::`
- Modify: `src/rpc/rpcblockchain.cpp` — remove `using namespace std;`
- Modify: `src/rpc/rpcdump.cpp` — same
- Modify: `src/rpc/rpcmining.cpp` — same
- Modify: `src/rpc/rpcnet.cpp` — same
- Modify: `src/rpc/rpcrawtransaction.cpp` — same
- Modify: `src/rpc/rpcsmessage.cpp` — same
- Modify: `src/qt/coincontroldialog.cpp` — same

**Special case for bitcoinrpc.cpp:** Also replace unqualified `asio::` types. Common ones: `ip::tcp`, `ssl::stream`, `io_context`, `ip::address`.

**Verification:** `grep -rn "using namespace std\|using namespace asio" src/ --include="*.cpp" | grep -v test/` returns zero.

**Build both targets (Linux + Windows MXE) and test.**

**Commit:** `Phase 6F: Remove using-namespace-std from 8 RPC and Qt files`

---

## Task 14: File Path Validation for dumpwallet/importwallet

Add path validation to prevent directory traversal attacks.

**Files:**
- Modify: `src/rpc/rpcdump.cpp` — add `#include <filesystem>`, add `ValidateWalletFilePath()` helper, call it before `file.open()` in both `importwallet` (line ~161) and `dumpwallet` (line ~273)

**Implementation:**
```cpp
static std::filesystem::path ValidateWalletFilePath(const std::string& input)
{
    namespace fs = std::filesystem;
    fs::path p = fs::weakly_canonical(fs::path(input));

    fs::path dataDir = fs::weakly_canonical(GetDataDir());
    fs::path cwd = fs::weakly_canonical(fs::current_path());

    bool inDataDir = strutil::starts_with(p.string(), dataDir.string());
    bool inCwd = strutil::starts_with(p.string(), cwd.string());

    if (!inDataDir && !inCwd)
        throw JSONRPCError(RPC_INVALID_PARAMETER,
            "File path must be within the data directory or current working directory");

    return p;
}
```

**Usage in importwallet:**
```cpp
auto validPath = ValidateWalletFilePath(params[0].get<std::string>());
std::ifstream file;
file.open(validPath.string().c_str());
```

**Usage in dumpwallet:** Same pattern with `std::ofstream`.

**Add test for path validation** in existing `rpcdump_tests.cpp` or in a new suite in `bignum_pinning_tests.cpp`:
- Test that a path within datadir passes
- Test that `../../etc/passwd` is rejected with `RPC_INVALID_PARAMETER`

**Build and test.**

**Commit:** `Phase 6F: Add file path validation for dumpwallet and importwallet`

---

## Task 15: Final Build Verification

Full clean rebuild of both targets with comprehensive verification.

**Steps:**
```bash
cd /mnt/projects-windows/Pink2
rm -rf build/linux-release && cmake --preset linux-release && cmake --build build/linux-release
rm -rf build/windows-mxe && cmake --preset windows-mxe && cmake --build build/windows-mxe
echo "=== LINUX ===" && stat -c '%y' build/linux-release/src/test/test_pinkcoin
echo "=== WINDOWS ===" && stat -c '%y' build/windows-mxe/src/pink2d.exe && stat -c '%y' build/windows-mxe/src/qt/Pinkcoin-Qt.exe
echo "=== NOW ===" && date '+%Y-%m-%d %H:%M:%S %z'
cd build/linux-release && ctest --output-on-failure
```

**Verification checklist:**
```bash
# Zero CBigNum in production code
grep -rn "CBigNum\|CAutoBN_CTX\|BN_" src/ --include="*.cpp" --include="*.h" | grep -v test/ | grep -v leveldb/ | wc -l
# Expected: 0

# Zero using-namespace-std in production .cpp files
grep -rn "using namespace std" src/ --include="*.cpp" | grep -v test/ | wc -l
# Expected: 0

# Zero #define printf in production code
grep -rn "#define printf" src/ --include="*.cpp" --include="*.h" | grep -v test/ | grep -v leveldb/ | wc -l
# Expected: 0

# Zero void* parg thread functions
grep -rn "void\*.*parg" src/ --include="*.cpp" --include="*.h" | grep -v test/ | grep -v leveldb/ | wc -l
# Expected: 0

# vnThreadsRunning is atomic
grep -n "atomic" src/net.h | head -5
# Expected: std::atomic<int> in vnThreadsRunning declaration

# Test count
./build/linux-release/src/test/test_pinkcoin --log_level=test_suite 2>&1 | tail -5
# Expected: ~1,643 tests, 0 failures
```

All timestamps must be within the current session. Both builds must succeed with zero warnings related to our changes.

**No commit needed** — this is verification only.

---

## Summary

| Task | Stream | What | Files | Tests |
|------|--------|------|-------|-------|
| 1 | Safety | Edge-case pinning tests | +bignum_pinning_tests.cpp | +~26 |
| 2 | C | printf→LogPrintf completion | 5 files | 0 new |
| 3 | A | arith_uint256 class | +arith_uint256.h/.cpp | 0 new |
| 4 | A | CScriptNum class | +scriptnum.h | 0 new |
| 5 | A | Base58 byte-array rewrite | base58.h | 0 new |
| 6 | A | Migrate difficulty math | kernel.cpp, main.cpp/h, miner.cpp | 0 new |
| 7 | A | Migrate script interpreter | script.cpp/h | 0 new |
| 8 | A | Delete bignum.h | -bignum.h | 0 new |
| 9 | B | Atomic counters + guard | net.h, net.cpp, +thread_guard.h | 0 new |
| 10 | B | Typed NewThread + merge wrappers | ~12 files | 0 new |
| 11 | C | Remove using-namespace-std (batch 1) | 8 core files | 0 new |
| 12 | C | Remove using-namespace-std (batch 2) | 7 net/consensus files | 0 new |
| 13 | C | Remove using-namespace-std (batch 3) | 8 RPC/Qt files | 0 new |
| 14 | C | File path validation | rpcdump.cpp | +~2 |
| 15 | — | Final verification | — | — |

**Target:** ~1,645 tests, zero failures, both builds clean.
