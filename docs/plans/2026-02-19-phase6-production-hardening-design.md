# Phase 6: Production Hardening Design

**Date:** 2026-02-19
**Branch:** feature/twenty_six
**Approach:** Test-First, Bottom-Up (A)
**Delivery:** Focused sub-phases, each independently verifiable

## Context

Pinkcoin 2 approaches 3 million blocks with heavy UTXO activity from staking,
side-staking, and network support address distributions. Phases 1-5 modernized
the codebase (C++17, libsecp256k1, structural decomposition, 1,222 tests). Phase
6 ensures the wallet is production-ready at scale: correct staking logic, hardened
security surfaces, modernized infrastructure.

## Goals

1. **Prove staking correctness** — CreateCoinStake, AggregateStakeOut, coin
   splitting/combining, and threshold settings verified through dedicated tests
2. **Harden all security surfaces** — RAII resource management, threading safety,
   RPC validation, BDB integrity, P2P DoS resistance
3. **Modernize infrastructure** — Structured logging, macro elimination, string
   optimization
4. **Reduce dependency surface** — JSON library swap, Boost reduction, error
   handling patterns

## Sub-phase 6A: Staking Correctness Testing

**Goal:** Prove the wallet's revenue engine is mathematically correct.

**Estimated scope:** ~100-120 new test cases in `staking_tests.cpp`

### 1. CreateCoinStake() logic (wallet/wallet.cpp:2496-2734)
- Stake splitting: coins > nSplitThreshold correctly produce two outputs
- Coin combining: coins < nCombineThreshold get aggregated (up to 100 inputs)
- Combining stops at threshold; large inputs (>= nCombineThreshold) skipped
- Reserve balance exclusion: staking respects nReserveBalance
- Flash PoS vs regular PoS: different thresholds apply correctly
- Output value conservation: inputs + reward = outputs (no coin creation/loss)

### 2. AggregateStakeOut() side-staking (wallet/wallet.cpp:2736-2764)
- Percentage math: `(nReward * nPercent) / 100` precision and rounding
- Multiple side-stake addresses: total distribution does not exceed reward
- Edge cases: 0%, 100%, fractional percentages (up to 6 decimal places)
- Invalid addresses skipped correctly
- Total percentage validation (<=100%)

### 3. SelectCoinsForStaking() (wallet/wallet.cpp:1455-1492)
- Coin maturity filtering (nStakeMinAge = 3600s)
- Weight calculation correctness (coin-days)
- Empty wallet, single coin, many small coins behavior

### 4. GetStakeWeight() (wallet/wallet.cpp:2424-2494)
- Min/max/combined weight calculation
- Time weight progression (0 to nStakeMaxAge)
- Flash PoS weight (shorter max age: 7 days vs 30 days)

### 5. Threshold configuration (init.cpp:740-768)
- Validation: split > combine enforced
- Flash PoS detection: split >= 100,000 triggers Flash mode
- Command-line override behavior

### 6. StakeDB persistence
- CStakeDB write/read/erase cycle with real data
- Side-stake configurations survive simulated restart
- Corrupt/missing data handling

### 7. RPC staking commands
- `setstakesplitthreshold` — boundary values, persistence
- `addstakeout` — address validation, percentage validation, total check
- `delstakeout` — removal correctness
- `liststakeout` — accurate listing after add/delete cycles

### Verification
- All 1,222 existing tests still pass
- New staking tests pass
- Both Linux and Windows builds compile clean

---

## Sub-phase 6B: Security Hardening

**Goal:** Eliminate resource leaks, race conditions, and input validation gaps.

**Estimated scope:** ~60-80 code changes + ~40-60 new security tests

### 1. RAII Database Cursors (BDB + LevelDB)
- Wrap BerkeleyDB `Dbc*` cursors in RAII guards
- Wrap LevelDB iterator lifecycle
- Scope: db.cpp, txdb-leveldb.cpp, wallet/walletdb.cpp, stakedb.cpp
- Protects wallet data integrity under heavy UTXO churn

### 2. Threading Cleanup
- `boost::thread` -> `std::thread` at ~8 sites
- Audit global state access (pindexBest, mapBlockIndex, cs_main)
- Fix race conditions in staking path (CreateCoinStake)
- Scope: net/net.cpp, main.cpp, wallet/wallet.cpp, init.cpp, miner.cpp

### 3. RPC Input Validation Audit
- Systematic review of all 100 RPC commands for validation gaps
- Focus: integer overflow, string injection, unvalidated address formats
- Harden staking RPC commands from 6A
- Add fuzz-style boundary tests for critical parameters

### 4. BDB 4.8 Integrity Hardening
- Add checksums/verification on wallet.dat open
- Improve backup robustness (atomic backup during staking)
- Test behavior under simulated I/O errors
- Document known BDB 4.8 CVEs and mitigations

### 5. P2P Network Hardening
- Audit 2012-era net code for DoS vectors
- Connection flooding limits
- Malformed message handling
- Ban logic review
- Peer address validation
- Scope: net/net.cpp, protocol.cpp

### Verification
- All existing + 6A tests pass
- New security tests pass
- Both builds compile clean
- No resource leaks under Valgrind (Linux)

---

## Sub-phase 6C: Infrastructure Modernization

**Goal:** Modernize logging, macros, and string handling for maintainability.

**Estimated scope:** ~100-150 code changes across ~20 files, ~20-30 new tests

### 1. Structured Logging
- Lightweight Logger class with levels: ERROR, WARN, INFO, DEBUG
- Log categories: net, wallet, stake, rpc, consensus
- LogPrintf backward-compatible macro routing through new system
- Runtime log level control via RPC (`setloglevel`)
- Remove printf debugging from blockprocessing.cpp and consensus code

### 2. Macro Elimination
- `BEGIN(a)`/`END(a)` -> `std::begin(a)`/`std::end(a)` or range-based
- `PushMessage` variadic macro -> variadic template function
- `IMPLEMENT_SERIALIZE` -> evaluate modern serialization scope
- `LOCK`/`LOCK2` — keep (standard Bitcoin pattern), audit usage

### 3. String Optimization (light touch)
- `std::string_view` for read-only parameters in hot paths
- Focus on RPC parsing and address validation
- Only where profiling shows benefit

### Verification
- All existing + 6A + 6B tests pass
- New logging tests pass
- Log output is correct and categorized
- Both builds compile clean

---

## Sub-phase 6D: Dependency Modernization

**Goal:** Reduce external dependency surface and modernize third-party libraries.

**Estimated scope:** ~200-300 code changes, ~30-40 new/updated tests

### 1. JSON Library Migration
- Replace json_spirit with nlohmann/json (header-only, C++17)
- json_spirit: unmaintained since ~2014, non-standard API
- nlohmann/json: single-header, well-tested, standard-like API
- Scope: all RPC handlers (~15 files), bitcoinrpc.cpp core
- Tier I's 100 RPC command tests serve as regression safety net

### 2. Boost Reduction
- `boost::signals2` -> `std::function` + simple signal class
- `boost::filesystem` -> `std::filesystem` (C++17)
- `boost::thread` already migrated in 6B
- Audit remaining Boost usage; keep only what's needed (Boost.Test stays)

### 3. Error Handling Pattern (lightweight)
- Introduce `Result<T>` pattern for new code paths
- Don't retrofit entire codebase
- Replace `throw JSONRPCError` where it improves clarity

### Verification
- All existing + 6A + 6B + 6C tests pass
- JSON migration has zero RPC behavior changes (byte-for-byte output)
- Both builds compile clean

---

## Constraints

- No `-j` flag in builds (single-threaded for hardware protection)
- Both Linux release and Windows MXE cross-compile must pass after each sub-phase
- All 1,222+ existing tests must continue passing (zero regressions)
- BerkeleyDB 4.8 remains pinned (wallet.dat compatibility)
- No consensus rule changes (the integer division bug stays as-is)
- GUI changes deferred to a future phase

## Success Criteria

| Metric | Target |
|--------|--------|
| Staking test coverage | 100+ dedicated tests for CreateCoinStake, side-staking, thresholds |
| Resource leaks | Zero under Valgrind |
| RPC validation | All 100 commands audited for input validation |
| Threading | All boost::thread sites migrated to std::thread |
| Logging | Structured, categorized, runtime-configurable |
| JSON library | nlohmann/json replacing json_spirit |
| Boost dependencies | Reduced to Boost.Test + essential only |
| Build health | Both Linux (276+ units) and Windows (228+ units) compile clean |
| Test suite | 1,400+ tests (current 1,222 + ~200 new), zero failures |
