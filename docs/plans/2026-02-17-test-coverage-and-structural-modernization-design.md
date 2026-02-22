# Test Coverage Completion & Structural Modernization Design

**Date**: 2026-02-17
**Branch**: feature/twenty_six
**Status**: Approved

## Goals

1. **Complete test coverage** for the entire codebase — ensuring backwards compatibility, blockchain continuity, security, and efficiency are verifiable
2. **Modernize the codebase** without chain forks — all improvements are internal, behavior-preserving changes that require no community coordination

## Sequencing

Tests first, then modernization. The complete test safety net must exist before any production code is restructured.

## Current State

- 962 test cases across 73 suites, all passing (Tiers P0-P9, C-H, Golden)
- Code modernization Phases 1-4 complete (override, auto, static_cast, smart pointers)
- Crypto modernization complete (libsecp256k1, EVP APIs)
- 8 security audit items resolved

---

# Part 1: Test Coverage (Tiers I, J, K)

Target: ~210-250 new tests, bringing total to ~1,170-1,210.

## Tier I — Consensus Pinning (~80-100 tests)

Pin every consensus code path in main.cpp with golden regression tests that verify exact outputs for known inputs. Any accidental consensus change breaks a test.

### I.1 ConnectBlock Internals (~20 tests)
- PoW coinbase reward validation (exact amounts at known heights)
- PoS coinstake reward validation with coin age calculation
- BIP30 replay protection (duplicate tx rejection)
- Signature operation counting (MAX_BLOCK_SIGOPS = 20,000)
- Money supply tracking (nMoneySupply accumulation)

### I.2 Chain Reorganization (~15 tests)
- Reorganize() with shorter competing chain (should fail)
- Equal-length chain with more cumulative work (should succeed)
- Multi-block reorg (2-3 block depth)
- Reorg that invalidates mempool transactions
- Reorg across difficulty adjustment boundary

### I.3 Orphan Block Handling (~15 tests)
- Orphan acceptance into orphan map
- Orphan resolution when parent block arrives
- Orphan limit enforcement (memory protection)
- Recursive orphan chain resolution
- Orphan expiry/cleanup

### I.4 AcceptBlock Validation (~15 tests)
- Block version checking: pre-Sept 30, 2018 rules (reject > CURRENT_VERSION)
- Block version checking: post-Sept 30, 2018 rules (allow CURRENT_VERSION + 1)
- Checkpoint enforcement (CheckHardened, CheckSync)
- Coinbase height enforcement (scriptSig must serialize nHeight)
- Timestamp monotonicity (block time > previous block time)

### I.5 Transaction Validation (~15 tests)
- IsFinal() at various heights and lock times
- ConnectInputs edge cases (missing inputs, insufficient value)
- Double-spend rejection within same block
- Script validation for standard transaction types

### I.6 ProcessBlock Dispatch (~10 tests)
- PoW disable after nTimeV231 (Aug 9, 2019)
- Duplicate block rejection
- Stake duplication prevention
- Difficulty filtering for orphan protection (ComputeMinWork/ComputeMinStake)

## Tier J — RPC Integration (~90-100 tests)

Test all 95 RPC commands through actual dispatch framework, verifying argument parsing, error responses, and output format.

### J.1 Wallet RPCs (~35 tests)
- sendtoaddress, getbalance, listunspent, listtransactions
- getnewaddress, dumpprivkey, importprivkey
- encryptwallet, walletpassphrase, walletlock
- getreceivedbyaddress, getreceivedbyaccount
- sendmany, sendfrom, move, settxfee
- listaccounts, getaccount, setaccount

### J.2 Blockchain RPCs (~15 tests)
- getblock (verbosity levels), getblockhash, getblockcount
- getdifficulty, getrawmempool, getbestblockhash
- gettxout, gettxoutsetinfo

### J.3 Mining/Staking RPCs (~10 tests)
- getmininginfo, getstakinginfo
- getblocktemplate (output format and field presence)
- getnetworkhashps

### J.4 Raw Transaction RPCs (~10 tests)
- createrawtransaction (valid and malformed inputs)
- signrawtransaction, decoderawtransaction
- sendrawtransaction (rejection of invalid)

### J.5 Network RPCs (~10 tests)
- getpeerinfo, getconnectioncount
- addnode, getnetworkinfo
- ping (with mock CNode state)

### J.6 Messaging RPCs (~10 tests)
- smsgsend, smsginbox, smsgoutbox
- smsgbuckets, smsgscanbuckets
- With mock smessage state

### J.7 Error Handling (~10 tests)
- Wrong parameter count for each category
- Wrong parameter types
- Unauthorized methods (wallet locked)
- Missing wallet scenarios
- Every RPC must reject bad inputs gracefully

## Tier K — Database & Edge Cases (~40-50 tests)

### K.1 BerkeleyDB Error Paths (~15 tests)
- Write failures (simulated disk full)
- Read after close
- Corrupt data handling
- Flush during concurrent access
- Salvage verification

### K.2 LevelDB Edge Cases (~10 tests)
- Missing keys (graceful return)
- Iterator bounds (past end, before begin)
- Batch write atomicity
- Lock file contention
- Large value handling

### K.3 NTP Edge Cases (~5 tests)
- Epoch math boundary conditions (expand existing Tier H coverage)
- Timezone edge cases
- Overflow conditions

### K.4 Mempool Edge Cases (~10 tests)
- Pool size limits and eviction
- Orphan transaction handling
- Transaction expiry
- Priority/fee-based ordering
- Conflicting transaction rejection

### K.5 Script Edge Cases (~10 tests)
- Non-standard script rejection
- OP_RETURN handling
- Multisig validation (m-of-n edge cases)
- P2SH validation edge cases
- Script size limits

---

# Part 2: Structural Modernization (Phases 5, 6, 7)

All changes are fork-safe: zero consensus impact, zero behavior change. Verified by the complete test suite from Part 1.

## Phase 5 — Structural Decomposition

### 5A. Decompose main.cpp (4,101 lines -> ~5-6 files)

Extract into logical modules with identical function signatures:

| New File | Contents | Lines (est.) |
|----------|----------|-------------|
| validation.cpp | CheckBlock, AcceptBlock, ConnectBlock, CheckProofOfWork, CheckProofOfStake | ~800 |
| rewards.cpp | GetProofOfWorkReward, GetProofOfStakeReward, GetNextTargetRequired | ~400 |
| mempool.cpp | CTxMemPool, AcceptToMemoryPool, orphan transaction handling | ~300 |
| blockprocessing.cpp | ProcessBlock, Reorganize, SetBestChain | ~600 |
| main.cpp | Global state, LoadBlockIndex, entry point coordination | ~2,000 |

Each extracted file gets its own header. Verification: both builds compile, all tests pass, `nm` output shows identical symbols.

### 5B. Split RPC Handlers

rpcwallet.cpp (2,562 lines) -> 4 files:
- rpc_wallet_send.cpp — sendtoaddress, sendmany, sendfrom
- rpc_wallet_query.cpp — getbalance, listunspent, listtransactions
- rpc_wallet_keys.cpp — getnewaddress, dumpprivkey, importprivkey
- rpc_wallet_mgmt.cpp — encryptwallet, walletpassphrase, walletlock

### 5C. Organize stakedb.cpp (10,228 lines -> ~3-4 files)
- Core CRUD operations
- Sync/migration logic
- Query interface

### 5D. Module Directory Structure

```
src/
  consensus/     <- validation.cpp, rewards.cpp
  rpc/           <- split RPC files
  wallet/        <- wallet.cpp, walletdb.cpp
  net/           <- net.cpp, netbase.cpp
  main.cpp       <- slimmed entry point
```

## Phase 6 — Infrastructure Modernization

### 6A. Structured Logging
- Custom Logger class (no external dependency)
- Log levels: ERROR, WARN, INFO, DEBUG, TRACE
- Format: `[timestamp] [level] [module] message`
- LogPrintf macro wraps Logger for backward compatibility
- Existing fDebug flag maps to DEBUG level

### 6B. Threading Modernization
- boost::thread -> std::thread (8 thread launch sites)
- NewThread(func, arg) -> std::thread(func, args...) with lambdas
- MilliSleep() -> std::this_thread::sleep_for()
- Keep CCriticalSection/LOCK macros (already wrap std::recursive_mutex)

### 6C. RAII Database Cursors
- Wrap BDB Dbc* in RAII class with iterator interface
- Wrap LevelDB iterator similarly
- Eliminate manual free() calls in ReadAtCursor

### 6D. String Optimization
- std::string_view for read-only parameters in network message handling
- std::string_view for RPC command name lookups
- Keep std::string for owned data

### 6E. Macro Elimination
- BEGIN(a)/END(a) -> as_bytes<T>() template function
- PushMessage 9 overloads -> single variadic template
- strprintf macro -> modern formatting

## Phase 7 — Dependency Modernization

### 7A. JSON Library Migration
- json_spirit -> nlohmann/json (header-only)
- Phased: add nlohmann alongside json_spirit, migrate file by file, remove json_spirit
- No new build system complexity

### 7B. Boost Reduction
- boost::signals2 -> std::function callback registry (wallet notifications)
- boost::filesystem -> std::filesystem (complete partial migration)
- Keep boost::test (test framework, no reason to migrate)

### 7C. Error Handling Pattern
- Introduce Result<T> type (std::variant<T, Error>) for new RPC code
- Don't retrofit existing code — use only in newly written/modernized functions
- Gradual migration as files are touched

---

# Consensus Safety Invariants

These rules apply to ALL phases:

1. **Never modify** consensus constants (rewards, difficulty, timing thresholds)
2. **Never modify** the integer division bug at main.cpp:1040 — it is a consensus rule
3. **Never modify** checkpoint hashes or activation timestamps
4. **Never modify** serialization format (golden tests guard this)
5. **File splits** must preserve identical function signatures and behavior
6. **Every change** verified by: Linux build + Windows cross-compile + full test suite
7. **Structural changes** verified by `nm` symbol comparison (before/after identical)

---

# Success Criteria

1. **Test coverage**: ~1,170+ tests, all passing, covering every consensus path, all 95 RPC commands, and database edge cases
2. **Structural**: No file > 2,500 lines (except stakedb.cpp which targets ~3,500)
3. **Infrastructure**: Zero boost::thread usage, structured logging, RAII everywhere
4. **Dependencies**: json_spirit removed, Boost footprint reduced to test + program_options
5. **Builds**: Both Linux and Windows MXE compile cleanly with zero warnings
6. **Behavior**: Byte-identical consensus behavior — same blocks accepted/rejected as before
