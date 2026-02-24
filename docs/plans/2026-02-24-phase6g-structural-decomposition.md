# Phase 6G: Structural Decomposition & Code Cleanup — Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Decompose smessage.cpp (4031 lines) and wallet.cpp (3560 lines) into focused modules, eliminate remaining code debt (dead files, raw new/delete, C-style casts, unsafe functions).

**Architecture:** Three parallel streams — smessage decomposition (Tasks 1-6), wallet decomposition (Tasks 7-11), code cleanup (Tasks 12-18). Each extraction creates a new .cpp file containing functions cut from the original, with the original retaining global state and lifecycle code. All methods keep identical signatures.

**Tech Stack:** C++17, CMake, Boost.Test (1617 tests), Linux + Windows MXE cross-compile.

---

## Reference: Build & Verify

After every task:
```bash
cmake --build build/linux-release 2>&1 | tail -5
cd build/linux-release && ctest --output-on-failure 2>&1 | tail -5
```
Expected: 278 units compile, 1617 tests pass. Do NOT proceed if build or tests fail.

**CMakeLists.txt location:** `src/CMakeLists.txt`
- Sources listed in `PINKCOIN_CORE_SOURCES` (line 10+)
- Headers listed in `PINKCOIN_CORE_HEADERS` (line 66+)

---

# STREAM 1: SMESSAGE DECOMPOSITION

## Task 1: Create smessage/ directory and move files

**Goal:** Move `smessage.h` and `smessage.cpp` into `src/smessage/` directory, update all include paths and CMakeLists.txt.

**Files:**
- Move: `src/smessage.h` → `src/smessage/smessage.h`
- Move: `src/smessage.cpp` → `src/smessage/smessage.cpp`
- Modify: `src/CMakeLists.txt`
- Modify: All 11 files that include `smessage.h`

**Step 1: Create directory and git mv**
```bash
mkdir -p src/smessage
git mv src/smessage.h src/smessage/smessage.h
git mv src/smessage.cpp src/smessage/smessage.cpp
```

**Step 2: Update CMakeLists.txt**

In `src/CMakeLists.txt`, change:
- In `PINKCOIN_CORE_SOURCES`: `smessage.cpp` → `smessage/smessage.cpp`
- In `PINKCOIN_CORE_HEADERS`: `smessage.h` → `smessage/smessage.h`

**Step 3: Update all include paths**

These 11 files include `smessage.h` and need the path updated to `smessage/smessage.h`:
1. `src/smessage/smessage.cpp` (self-include)
2. `src/init.cpp`
3. `src/main.cpp`
4. `src/wallet.h`
5. `src/qt/addressbookpage.cpp`
6. `src/qt/addresstablemodel.cpp`
7. `src/qt/messagemodel.h`
8. `src/qt/sendmessagesentry.cpp`
9. `src/rpc/rpcsmessage.cpp`
10. `src/test/rpc_response_tests.cpp`
11. `src/test/smessage_tests.cpp`

Change `#include "smessage.h"` → `#include "smessage/smessage.h"` in each.

**Step 4: Also update smessage.cpp's internal includes**

The moved `src/smessage/smessage.cpp` includes files like `"base58.h"`, `"db.h"`, `"init.h"`, etc. These are found via `target_include_directories(pinkcoin_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})` which points to `src/`, so they'll still resolve correctly. No changes needed for these.

The `"lz4/lz4.c"` and `"xxhash/xxhash.h"` includes need updating: they're relative paths that need `"../lz4/lz4.c"` and `"../xxhash/xxhash.h"` etc., OR we keep them as-is if the include search path finds them. Check the actual include: if `lz4/lz4.c` is at `src/lz4/lz4.c`, then the PUBLIC include directory `src/` will find it — no change needed.

**Step 5: Build and verify**
```bash
cmake --build build/linux-release 2>&1 | tail -5
cd build/linux-release && ctest --output-on-failure 2>&1 | tail -5
```

---

## Task 2: Extract smessage_db.cpp

**Goal:** Extract SecMsgDB class implementation and SecMsgBatchScanner into `src/smessage/smessage_db.cpp`.

**Files:**
- Create: `src/smessage/smessage_db.cpp`
- Modify: `src/smessage/smessage.cpp` (remove extracted functions)
- Modify: `src/CMakeLists.txt` (add new source)

**Step 1: Create smessage_db.cpp**

Create `src/smessage/smessage_db.cpp` containing:
- The same copyright header
- Required includes: `"smessage.h"`, `"logging.h"`, plus any other includes needed by the DB functions (check what they use: leveldb, CDataStream, etc.)
- The `SecMsgBatchScanner` class definition (lines 229-257 of current smessage.cpp) — this is a local class, move the full implementation
- All SecMsgDB method implementations (lines 193-580):
  - `SecMsgDB::Open()` (193-226)
  - `SecMsgDB::ScanBatch()` (264-282)
  - `SecMsgDB::TxnBegin()` (284-290)
  - `SecMsgDB::TxnCommit()` (292-309)
  - `SecMsgDB::TxnAbort()` (312-315)
  - `SecMsgDB::ReadPK()` (318-360)
  - `SecMsgDB::WritePK()` (363-392)
  - `SecMsgDB::ExistsPK()` (395-417)
  - `SecMsgDB::NextSmesg()` (421-446)
  - `SecMsgDB::NextSmesgKey()` (449-466)
  - `SecMsgDB::ReadSmesg()` (469-508)
  - `SecMsgDB::WriteSmesg()` (511-536)
  - `SecMsgDB::ExistsSmesg()` (539-559)
  - `SecMsgDB::EraseSmesg()` (562-580)

**Step 2: Remove these functions from smessage.cpp**

Delete lines 193-580 from `src/smessage/smessage.cpp`. Keep everything before (includes, globals) and after (ThreadSecureMsg onward).

**Step 3: Add to CMakeLists.txt**

Add `smessage/smessage_db.cpp` to `PINKCOIN_CORE_SOURCES` (after `smessage/smessage.cpp`).

**Step 4: Build and verify**

---

## Task 3: Extract smessage_crypto.cpp

**Goal:** Extract cryptographic operations: SecMsgCrypter methods, encrypt, decrypt, validate, hash functions.

**Files:**
- Create: `src/smessage/smessage_crypto.cpp`
- Modify: `src/smessage/smessage.cpp` (remove extracted functions)
- Modify: `src/CMakeLists.txt`

**Step 1: Create smessage_crypto.cpp**

Create `src/smessage/smessage_crypto.cpp` containing these functions (line numbers reference the ORIGINAL file before Task 2 cuts — use function names to locate after prior edits):

- `SecMsgCrypter::SetKey()` — both overloads (originally 96-113)
- `SecMsgCrypter::Encrypt()` (originally 115-142)
- `SecMsgCrypter::Decrypt()` (originally 144-170)
- `SecureMsgValidate()` (originally 3111-3182)
- `SecureMsgSetHash()` (originally 3184-3276)
- `SecureMspinkcrypt()` (originally 3278-3566)
- `SecureMsgDecrypt()` — both overloads (originally 3739-4031)

Includes needed: `"smessage.h"`, `"logging.h"`, OpenSSL headers (`<openssl/evp.h>`, `<openssl/params.h>`), `<secp256k1.h>`, `<secp256k1_ecdh.h>`, `"base58.h"`, `"init.h"`, `"stealth.h"`, `"key.h"`, plus whatever else these functions reference.

**Step 2: Remove these functions from smessage.cpp**

**Step 3: Add `smessage/smessage_crypto.cpp` to CMakeLists.txt**

**Step 4: Build and verify**

---

## Task 4: Extract smessage_net.cpp

**Goal:** Extract network protocol handlers, thread functions, and bucket hash computation.

**Files:**
- Create: `src/smessage/smessage_net.cpp`
- Modify: `src/smessage/smessage.cpp` (remove extracted functions)
- Modify: `src/CMakeLists.txt`

**Step 1: Create smessage_net.cpp**

Functions to extract (use function names to locate — line numbers from original):
- `SecMsgBucket::hashBucket()` (originally 172-190)
- `ThreadSecureMsg()` (originally 583-690)
- `ThreadSecureMsgPow()` (originally 693-777)
- `SecureMsgReceiveData()` (originally 1310-1768)
- `SecureMsgSendData()` (originally 1771-1870)

Includes needed: `"smessage.h"`, `"logging.h"`, `"thread_guard.h"`, `"net.h"`, `"init.h"`, `"../xxhash/xxhash.h"` (or however xxhash resolves), filesystem, etc.

**Step 2: Remove from smessage.cpp**

**Step 3: Add `smessage/smessage_net.cpp` to CMakeLists.txt**

**Step 4: Build and verify**

---

## Task 5: Extract smessage_store.cpp

**Goal:** Extract message storage, retrieval, reception, and scanning functions.

**Files:**
- Create: `src/smessage/smessage_store.cpp`
- Modify: `src/smessage/smessage.cpp` (remove extracted functions)
- Modify: `src/CMakeLists.txt`

**Step 1: Create smessage_store.cpp**

Functions to extract (by name):
- `SecureMsgScanMessage()` (originally 2493-2609)
- `SecureMsgRetrieve()` (originally 2746-2808)
- `SecureMsgReceive()` (originally 2811-2915)
- `SecureMsgStoreUnscanned()` (originally 2918-2982)
- `SecureMsgStore()` — both overloads (originally 2985-3106)
- `SecureMsgSend()` (originally 3569-3736)

Includes needed: `"smessage.h"`, `"logging.h"`, `"init.h"`, `"base58.h"`, filesystem, etc.

**Step 2: Remove from smessage.cpp**

**Step 3: Add `smessage/smessage_store.cpp` to CMakeLists.txt**

**Step 4: Build and verify**

---

## Task 6: Finalize smessage decomposition — nPeerIdCounter fix + include path audit

**Goal:** Fix thread safety bug in nPeerIdCounter, audit that all remaining functions in slimmed smessage.cpp are correct, verify line counts.

**Files:**
- Modify: `src/smessage/smessage.cpp`
- Modify: `src/smessage/smessage.h` (if nPeerIdCounter is declared there)

**Step 1: Fix nPeerIdCounter**

In `src/smessage/smessage.cpp`, change:
```cpp
uint32_t nPeerIdCounter = 1;
```
to:
```cpp
std::atomic<uint32_t> nPeerIdCounter{1};
```
Add `#include <atomic>` if not already present.

Find all usages of `nPeerIdCounter` (search: `nPeerIdCounter`). Change `nPeerIdCounter++` to `nPeerIdCounter.fetch_add(1)` or `nPeerIdCounter++` (std::atomic supports `operator++`). If it appears in smessage_net.cpp (where SecureMsgReceiveData uses it for peer IDs), ensure that file also sees the atomic type via smessage.h or an extern.

If `nPeerIdCounter` is declared extern in smessage.h, update the type there too.

**Step 2: Verify slimmed smessage.cpp contents**

The remaining smessage.cpp should contain only:
- Includes and global variable definitions
- `getTimeString()`, `fsReadable()`
- `SecureMsgBuildBucketSet()`, `SecureMsgAddWalletAddresses()`
- `SecureMsgReadIni()`, `SecureMsgWriteIni()`
- `SecureMsgStart()`, `SecureMsgShutdown()`, `SecureMsgEnable()`, `SecureMsgDisable()`
- `SecureMsgInsertAddress()` (static), public `SecureMsgInsertAddress()`
- `ScanBlock()` (static), `SecureMsgScanBlock()`, `ScanChainForPublicKeys()`, `SecureMsgScanBlockChain()`, `SecureMsgScanBuckets()`
- `SecureMsgWalletUnlocked()`, `SecureMsgWalletKeyChanged()`
- `SecureMsgGetLocalKey()`, `SecureMsgGetLocalPublicKey()`, `SecureMsgGetStoredKey()`, `SecureMsgAddAddress()`

Confirm each function is present. Count lines — target ~1500 or less.

**Step 3: Build and verify both targets**
```bash
cmake --build build/linux-release 2>&1 | tail -5
cd build/linux-release && ctest --output-on-failure 2>&1 | tail -5
cmake --build build/windows-mxe 2>&1 | tail -5
```

---

# STREAM 2: WALLET DECOMPOSITION

## Task 7: Extract wallet_keys.cpp

**Goal:** Extract key management, encryption, versioning, and key pool operations.

**Files:**
- Create: `src/wallet/wallet_keys.cpp`
- Modify: `src/wallet/wallet.cpp` (remove extracted methods)
- Modify: `src/CMakeLists.txt`

**Step 1: Create wallet_keys.cpp**

Create `src/wallet/wallet_keys.cpp` with includes matching wallet.cpp's header set:
```cpp
#include "wallet.h"
#include "walletdb.h"
#include "crypter.h"
#include "base58.h"
#include "logging.h"
```

Move these CWallet methods (locate by function name in wallet.cpp):
- `GenerateNewKey()` (33-57)
- `AddKey()` (59-72)
- `AddCryptedKey()` (74-88)
- `LoadKeyMetadata()` (90-98)
- `LoadCryptedKey()` (100-103)
- `AddCScript()` (105-116)
- `LoadCScript()` (119-133)
- `Lock()` (135-168)
- `Unlock()` (169-196)
- `ChangeWalletPassphrase()` (197-244)
- `SetBestChain()` (245-250)
- `SetMinVersion()` (251-277)
- `SetMaxVersion()` (278-289)
- `EncryptWallet()` (290-399)
- `NewKeyPool()` (3058-3081)
- `TopUpKeyPool()` (3082-3112)
- `ReserveKeyFromKeyPool()` (3113-3139)
- `AddReserveKey()` (3140-3154)
- `KeepKey()` (3155-3165) — CWallet::KeepKey
- `ReturnKey()` (3166-3175) — CWallet::ReturnKey
- `GetKeyFromPool()` (3176-3199)
- `GetOldestKeyPoolTime()` (3200-3210)
- CReserveKey methods: `GetReservedKey()` (3442-3462), `KeepKey()` (3463-3470), `ReturnKey()` (3471-3478)

**Step 2: Remove from wallet.cpp**

**Step 3: Add `wallet/wallet_keys.cpp` to `PINKCOIN_CORE_SOURCES` in CMakeLists.txt**

**Step 4: Build and verify**

---

## Task 8: Extract wallet_tx.cpp

**Goal:** Extract transaction lifecycle, balance queries, address book, and repair operations.

**Files:**
- Create: `src/wallet/wallet_tx.cpp`
- Modify: `src/wallet/wallet.cpp`
- Modify: `src/CMakeLists.txt`

**Step 1: Create wallet_tx.cpp**

Includes: `"wallet.h"`, `"walletdb.h"`, `"txdb.h"`, `"init.h"`, `"ui_interface.h"`, `"base58.h"`, `"logging.h"`.

Move these static helpers (they support the transaction methods):
- `ReadOrderPos()` (434-442)
- `WriteOrderPos()` (445-450)

Move these CWallet methods:
- `IncOrderPosNext()` (400-411)
- `OrderedTxItems()` (412-436)
- `WalletUpdateSpent()` (437-481)
- `MarkDirty()` (482-490)
- `AddToWallet()` (491-627)
- `AddToWalletIfInvolvingMe()` (628-656)
- `EraseFromWallet()` (657-669)
- `IsMine(CTxIn)` (670-685)
- `GetDebit(CTxIn)` (686-701)
- `IsChange()` (702-721)
- CWalletTx methods: `GetTxTime()` (722-727), `GetRequestCount()` (728-766), `GetAmounts()` (767-830), `GetAccountAmounts()` (831-865), `AddSupportingTransactions()` (866-925), `WriteToDisk()` (926-933)
- `ScanForWalletTransactions()` (934-962)
- `ReacceptWalletTransactions()` (963-1021)
- CWalletTx: `RelayWalletTransaction()` — both overloads (1022-1049)
- `ResendWalletTransactions()` (1050-1107)
- Balance queries: `GetBalance()` (1108-1123), `GetTotalMinted()` (1124-1146), `GetUnconfirmedBalance()` (1147-1161), `GetConfirmingBalance()` (1162-1176), `GetImmatureBalance()` (1177-1192), `GetStake()` (1298-1310), `GetNewMint()` (1311-1323)
- Address book: `SetAddressBookName()` (2917-2941), `DelAddressBookName()` (2942-2963), `SetAddressBookStake()` (2964-2982), `DelAddressBookStake()` (2983-2996)
- `GetTransaction()` (3016-3029)
- `SetDefaultKey()` (3030-3040)
- `FixSpentCoins()` (3338-3420)
- `DisableTransaction()` (3421-3441)
- `UpdatedTransaction()` (3499-3508)

**Step 2: Remove from wallet.cpp**

**Step 3: Add `wallet/wallet_tx.cpp` to CMakeLists.txt**

**Step 4: Build and verify**

---

## Task 9: Extract wallet_send.cpp

**Goal:** Extract coin selection, transaction creation, and broadcasting.

**Files:**
- Create: `src/wallet/wallet_send.cpp`
- Modify: `src/wallet/wallet.cpp`
- Modify: `src/CMakeLists.txt`

**Step 1: Create wallet_send.cpp**

Includes: `"wallet.h"`, `"walletdb.h"`, `"txdb.h"`, `"init.h"`, `"base58.h"`, `"kernel.h"`, `"coincontrol.h"`, `"stealth.h"`, `"logging.h"`.

Move the static helper:
- `CompareValueOnly` struct (24-31) — used by SelectCoinsMinConf

Move these CWallet methods:
- `AvailableCoins()` (1193-1227)
- `AvailableCoinsForStaking()` (1228-1297)
- `SelectCoinsMinConf()` (1324-1428) — also needs the static `ApproximateBestSubset()` helper if it exists as a free function
- `SelectCoins()` (1429-1450)
- `SelectCoinsForStaking()` (1451-1490)
- `CreateTransaction()` — vector overload (1491-1661)
- `CreateTransaction()` — single-output overload (1662-1692)
- `CommitTransaction()` (2759-2816)
- `SendMoney()` (2817-2855)
- `SendMoneyToDestination()` (2856-2876)
- `CreateStealthTransaction()` (2012-2051)
- `SendStealthMoney()` (2052-2088)
- `SendStealthMoneyToDestination()` (2089-2171)

**Step 2: Remove from wallet.cpp**

**Step 3: Add `wallet/wallet_send.cpp` to CMakeLists.txt**

**Step 4: Build and verify**

---

## Task 10: Extract wallet_staking.cpp

**Goal:** Extract PoS staking and stealth address management.

**Files:**
- Create: `src/wallet/wallet_staking.cpp`
- Modify: `src/wallet/wallet.cpp`
- Modify: `src/CMakeLists.txt`

**Step 1: Create wallet_staking.cpp**

Includes: `"wallet.h"`, `"walletdb.h"`, `"txdb.h"`, `"init.h"`, `"kernel.h"`, `"stealth.h"`, `"base58.h"`, `"arith_uint256.h"`, `"logging.h"`.

Move these CWallet methods:
- `GetStakeWeight()` (2405-2476)
- `CreateCoinStake()` (2477-2708)
- `AggregateStakeOut()` (2709-2738)
- `CountStakeOut()` (2739-2758)
- `NewStealthAddress()` (1693-1746)
- `AddStealthAddress()` (1747-1797)
- `UnlockStealthAddresses()` (1798-1950)
- `UpdateStealthAddress()` (1951-2011)
- `FindStealthTransactions()` (2172-2404)

**Step 2: Remove from wallet.cpp**

**Step 3: Add `wallet/wallet_staking.cpp` to CMakeLists.txt**

**Step 4: Build and verify**

---

## Task 11: Finalize wallet decomposition — verify + both builds

**Goal:** Verify all methods are accounted for, confirm slimmed wallet.cpp contents, build both targets.

**Files:**
- Verify: `src/wallet/wallet.cpp`

**Step 1: Verify slimmed wallet.cpp**

The remaining wallet.cpp should contain only:
- Includes
- `LoadWallet()` (2877-2902)
- `LoadStakeDB()` (2903-2916)
- `PrintWallet()` (2997-3015)
- `GetWalletFile()` (3041-3048) — non-member
- `GetPStakeDB()` (3049-3057) — non-member
- `GetAddressBalances()` (3211-3249)
- `GetAddressGroupings()` (3251-3333)
- `GetAllReserveKeys()` (3479-3498)
- `GetKeyBirthTimes()` (3510-3560)

Confirm each is present. Count lines — target ~600 or less.

**Step 2: Build both targets**
```bash
cmake --build build/linux-release 2>&1 | tail -5
cd build/linux-release && ctest --output-on-failure 2>&1 | tail -5
cmake --build build/windows-mxe 2>&1 | tail -5
```

---

# STREAM 3: CODE CLEANUP

## Task 12: Delete bignum.h + remove from CMakeLists.txt

**Goal:** Remove the dead `bignum.h` file (zero references after Phase 6F CBigNum elimination).

**Files:**
- Delete: `src/bignum.h`
- Modify: `src/CMakeLists.txt` (remove from `PINKCOIN_CORE_HEADERS`)

**Step 1: Verify zero references**
```bash
grep -r "bignum" src/ --include="*.cpp" --include="*.h" | grep -v "^Binary"
```
Expected: zero results (or only this plan file).

**Step 2: Delete and update CMake**
```bash
git rm src/bignum.h
```
In `src/CMakeLists.txt`, remove `bignum.h` from `PINKCOIN_CORE_HEADERS`.

**Step 3: Build and verify**

---

## Task 13: Smart pointer — init.cpp (pwalletMain, pstakeDB)

**Goal:** Convert global wallet pointers from raw `new`/`delete` to `std::unique_ptr<CWallet>`.

**Files:**
- Modify: `src/init.cpp` (lines 30-31, 99-100, 887, 911)
- Modify: `src/init.h` (lines 12-13)
- Modify: All files with `extern CWallet* pwalletMain` or `extern CWallet* pstakeDB`

**Step 1: Change declarations in init.cpp**

```cpp
// Before:
CWallet* pwalletMain;
CWallet* pstakeDB;

// After:
std::unique_ptr<CWallet> pwalletMain;
std::unique_ptr<CWallet> pstakeDB;
```
Add `#include <memory>` if not already present.

**Step 2: Change extern declarations in init.h**

```cpp
// Before:
extern CWallet* pwalletMain;
extern CWallet* pstakeDB;

// After:
extern std::unique_ptr<CWallet> pwalletMain;
extern std::unique_ptr<CWallet> pstakeDB;
```

**Step 3: Change allocation in init.cpp (~line 887, 911)**

```cpp
// Before:
pwalletMain = new CWallet(strWalletFileName);
pstakeDB = new CWallet(strStakeDBFileName);

// After:
pwalletMain = std::make_unique<CWallet>(strWalletFileName);
pstakeDB = std::make_unique<CWallet>(strStakeDBFileName);
```

**Step 4: Remove delete in Shutdown() (~line 99-100)**

```cpp
// Before:
delete pwalletMain;
delete pstakeDB;

// After:
pwalletMain.reset();
pstakeDB.reset();
```

**Step 5: Update all usage sites**

Search for `pwalletMain` and `pstakeDB` across the codebase. Most usage is via `pwalletMain->Method()` which works identically with `unique_ptr`. However, any site passing the raw pointer (e.g., `RegisterWallet(pwalletMain)`) needs `.get()`:
```cpp
RegisterWallet(pwalletMain.get());
```

Search comprehensively and update all affected call sites. Common patterns:
- `pwalletMain->` — no change needed
- Passing `pwalletMain` as function argument expecting `CWallet*` — add `.get()`
- Comparisons like `if (pwalletMain)` — works with unique_ptr (implicit bool)

**Step 6: Build and verify**

---

## Task 14: Smart pointer — main.cpp (orphan blocks) + wallet.cpp (groupings) + util.cpp (buffer)

**Goal:** Convert three more raw new/delete patterns to modern alternatives.

**Files:**
- Modify: `src/main.cpp` (~lines 68-69, 1006, 1071-1078)
- Modify: `src/wallet/wallet.cpp` or appropriate split file (~lines 3299-3332)
- Modify: `src/util.cpp` (~lines 247-276)

**Step 1: main.cpp orphan blocks**

Change map types:
```cpp
// Before:
std::map<uint256, CBlock*> mapOrphanBlocks;
std::multimap<uint256, CBlock*> mapOrphanBlocksByPrev;

// After:
std::map<uint256, std::unique_ptr<CBlock>> mapOrphanBlocks;
std::multimap<uint256, CBlock*> mapOrphanBlocksByPrev;  // non-owning view
```

Note: `mapOrphanBlocksByPrev` stores the same pointers as `mapOrphanBlocks` — it's a secondary index. Only ONE map should own the memory. Keep `mapOrphanBlocks` as owning (unique_ptr) and `mapOrphanBlocksByPrev` as non-owning (raw CBlock*).

Update allocation (~line 1006):
```cpp
// Before:
CBlock* pblock2 = new CBlock(*pblock);
mapOrphanBlocks.insert(std::make_pair(hash, pblock2));
mapOrphanBlocksByPrev.insert(std::make_pair(pblock2->hashPrevBlock, pblock2));

// After:
auto pblock2 = std::make_unique<CBlock>(*pblock);
CBlock* pblockRaw = pblock2.get();
mapOrphanBlocks.insert(std::make_pair(hash, std::move(pblock2)));
mapOrphanBlocksByPrev.insert(std::make_pair(pblockRaw->hashPrevBlock, pblockRaw));
```

Update deletion (~line 1071-1078):
```cpp
// Before:
CBlock* pblockOrphan = mi->second;
...
mapOrphanBlocks.erase(pblockOrphan->GetHash());
setStakeSeenOrphan.erase(pblockOrphan->GetProofOfStake());
delete pblockOrphan;

// After:
CBlock* pblockOrphan = mi->second;  // raw pointer from non-owning multimap
...
mapOrphanBlocks.erase(pblockOrphan->GetHash());  // unique_ptr destruction happens here
setStakeSeenOrphan.erase(pblockOrphan->GetProofOfStake());
// no delete needed — unique_ptr cleaned up when erased from mapOrphanBlocks
```

**IMPORTANT:** The erase from `mapOrphanBlocks` destroys the CBlock. Make sure `pblockOrphan->GetHash()` and `pblockOrphan->GetProofOfStake()` are called BEFORE the erase. Also make sure `pblockOrphan->AcceptBlock()` runs before the erase. Read the full loop carefully and reorder if needed — the non-owning pointer in `mapOrphanBlocksByPrev` becomes dangling after `mapOrphanBlocks.erase()`.

Also check `GetOrphanRoot()` and any other functions that access `mapOrphanBlocks` — they need to handle `unique_ptr<CBlock>` values (use `.get()` or `->` as needed).

**Step 2: wallet.cpp GetAddressGroupings**

This function (in whichever split file it ended up in — likely wallet.cpp slimmed) uses `new set<CTxDestination>` and `delete`. Replace with `unique_ptr`:

```cpp
// Before:
std::set< std::set<CTxDestination>* > uniqueGroupings;
std::map< CTxDestination, std::set<CTxDestination>* > setmap;
...
std::set<CTxDestination>* merged = new std::set<CTxDestination>(grouping);
...
delete hit;
...
delete uniqueGrouping;

// After: Use shared_ptr since setmap and uniqueGroupings both reference the same objects
std::set< std::shared_ptr<std::set<CTxDestination>> > uniqueGroupings;
std::map< CTxDestination, std::shared_ptr<std::set<CTxDestination>> > setmap;
...
auto merged = std::make_shared<std::set<CTxDestination>>(grouping);
...
// No delete needed — shared_ptr handles cleanup
```

**Step 3: util.cpp vstrprintf**

Replace dynamic char array with `std::vector<char>`:
```cpp
// Before:
char buffer[50000];
char* p = buffer;
int limit = sizeof(buffer);
...
if (p != buffer) delete[] p;
limit *= 2;
p = new char[limit];
...
if (p != buffer) delete[] p;

// After:
char buffer[50000];
char* p = buffer;
int limit = sizeof(buffer);
std::vector<char> dynamic_buf;
...
// Instead of new char[]:
limit *= 2;
dynamic_buf.resize(limit);
p = dynamic_buf.data();
...
// No delete needed — vector handles cleanup
```

**Step 4: Build and verify**

---

## Task 15: Smart pointer — net.cpp (nodes) + bitcoinrpc.cpp (connections)

**Goal:** Convert node disconnect cleanup and RPC connection handling to smart pointers.

**Files:**
- Modify: `src/net/net.cpp` (~line 839)
- Modify: `src/rpc/bitcoinrpc.cpp` (~lines 796, 828, 840, 846)

**Step 1: Assess net.cpp node ownership**

Read the CNode lifecycle in net.cpp to understand ownership:
- Where are CNode objects created? (likely `ConnectNode()` or accept handler)
- What containers hold them? (`vNodes`, `vNodesDisconnected`)
- Is there shared ownership between containers?

If ownership is complex (multiple containers hold raw pointers), this conversion may be risky. In that case, **skip this conversion** and document why. The existing paired new/delete works correctly.

If ownership is clear (one container owns, others are views): convert the owning container to `unique_ptr` and leave others as raw pointers.

**Step 2: bitcoinrpc.cpp connection handling**

The current pattern:
```cpp
AcceptedConnectionImpl<Protocol>* conn = new AcceptedConnectionImpl<Protocol>(...);
// ... async_accept lambda captures conn ...
// In RPCAcceptHandler:
//   error → delete conn;
//   not allowed → delete conn;
//   success → NewThread(ThreadRPCServer3, conn);  // ownership transfers to thread
```

ThreadRPCServer3 already wraps in unique_ptr at line 1076: `std::unique_ptr<AcceptedConnection> conn(pconn);`

Convert RPCListen allocation to unique_ptr:
```cpp
// In RPCListen:
auto conn = std::make_unique<AcceptedConnectionImpl<Protocol>>(...);
auto* connRaw = conn.get();
acceptor->async_accept(
    connRaw->sslStream.lowest_layer(),
    connRaw->peer,
    [acceptor, &context, fUseSSL, conn = conn.release()](const asio::error_code& error) {
        RPCAcceptHandler<Protocol>(acceptor, context, fUseSSL, conn, error);
    });
```

In RPCAcceptHandler, convert the 3 delete paths:
```cpp
// error and not-allowed paths: use unique_ptr for cleanup
std::unique_ptr<AcceptedConnection> guard(conn);
if (error) {
    // guard destructor cleans up
} else if (tcp_conn && !ClientAllowed(tcp_conn->peer.address())) {
    if (!fUseSSL)
        conn->stream() << HTTPReply(HTTP_FORBIDDEN, "", false) << std::flush;
    // guard destructor cleans up
} else if (!NewThread(ThreadRPCServer3, conn)) {
    LogPrintf("Failed to create RPC server client thread\n");
    // guard destructor cleans up
} else {
    guard.release();  // ThreadRPCServer3 now owns conn
}
```

**Step 3: Build and verify**

---

## Task 16: C-style cast elimination

**Goal:** Replace all remaining C-style casts with `static_cast`/`reinterpret_cast`/`const_cast`.

**Files:**
- Modify: `src/base58.h` (line 219)
- Modify: `src/crypter.cpp` (lines 30, 33, 38)
- Modify: `src/db.cpp` (line 574)
- Modify: `src/db.h` (lines 138, 259, 262)
- Modify: `src/key.cpp` (lines 368, 369)
- Modify: `src/net/netbase.cpp` (lines 229, 234, 319, 361)

**Step 1: Apply each fix**

| Location | Before | After |
|----------|--------|-------|
| base58.h:219 | `(void*)pbegin` | `static_cast<const void*>(pbegin)` |
| crypter.cpp:30 | `(const void*)strKeyData.c_str()` | `static_cast<const void*>(strKeyData.c_str())` |
| crypter.cpp:33 | `(unsigned char *)&scryptHash` | `reinterpret_cast<unsigned char*>(&scryptHash)` |
| crypter.cpp:38 | `(int)WALLET_CRYPTO_KEY_SIZE` | `static_cast<int>(WALLET_CRYPTO_KEY_SIZE)` |
| db.cpp:574 | `(char *)&vchData[0]` | `reinterpret_cast<char*>(&vchData[0])` |
| db.h:138 | `(char*)datValue.get_data()` (×2) | `reinterpret_cast<char*>(datValue.get_data())` |
| db.h:259 | `(char*)datKey.get_data()` | `reinterpret_cast<char*>(datKey.get_data())` |
| db.h:262 | `(char*)datValue.get_data()` | `reinterpret_cast<char*>(datValue.get_data())` |
| key.cpp:368 | `(unsigned)vchPrivKey.size()` | `static_cast<unsigned>(vchPrivKey.size())` |
| key.cpp:369 | `(size_t)16` | `static_cast<size_t>(16)` |
| netbase.cpp:229 | `(int)strDest.size()` | `static_cast<int>(strDest.size())` |
| netbase.cpp:234 | `(ssize_t)strSocks5.size()` | `static_cast<ssize_t>(strSocks5.size())` |
| netbase.cpp:319 | `(void*)&set` | `static_cast<void*>(&set)` |
| netbase.cpp:361 | `(char*)(&nRet)` | `reinterpret_cast<char*>(&nRet)` |

**Step 2: Build and verify**

---

## Task 17: Replace atoi in checkpoints.cpp

**Goal:** Replace unsafe `atoi()` with `std::stoi()`.

**Files:**
- Modify: `src/checkpoints.cpp` (line 61)

**Step 1: Replace atoi with stoi + error handling**

```cpp
// Before (line 61):
int nBlockNum = atoi(tempStr.c_str());

// After:
int nBlockNum;
try {
    nBlockNum = std::stoi(tempStr);
} catch (const std::exception&) {
    continue;  // skip malformed checkpoint entry
}
```

**Step 2: Build and verify**

---

## Task 18: Final build verification + hygiene checks

**Goal:** Verify both builds pass, all tests pass, and all hygiene targets are met.

**Files:** None — verification only.

**Step 1: Full build verification**
```bash
rm -rf build/linux-release && cmake --preset linux-release && cmake --build build/linux-release
cd build/linux-release && ctest --output-on-failure
```
Expected: 278 units, 1617 tests, zero failures.

```bash
cmake --build build/windows-mxe 2>&1 | tail -5
```
Expected: 223 units, zero errors.

**Step 2: Hygiene checks**

```bash
# Zero bignum.h references
grep -r "bignum" src/ --include="*.cpp" --include="*.h" | grep -v "test/" | grep -v ".md"

# Zero raw new in production (excluding test/, json/, asio/, lz4/, xxhash/, leveldb)
grep -rn "\bnew\b" src/ --include="*.cpp" --include="*.h" | grep -v "test/" | grep -v "json/" | grep -v "asio/" | grep -v "lz4/" | grep -v "xxhash/" | grep -v "leveldb"

# Zero C-style casts — check for pattern (type) or (type*)
# This is approximate — review results manually
grep -rn "(char\*)" src/ --include="*.cpp" --include="*.h" | grep -v "test/" | grep -v "json/" | grep -v "asio/"
grep -rn "(void\*)" src/ --include="*.cpp" --include="*.h" | grep -v "test/" | grep -v "json/" | grep -v "asio/"
grep -rn "(int)" src/ --include="*.cpp" --include="*.h" | grep -v "test/" | grep -v "json/" | grep -v "asio/" | grep -v "static_cast"

# Zero atoi in production
grep -rn "atoi\|atol\|atof" src/ --include="*.cpp" --include="*.h" | grep -v "test/"

# File line counts — no production file over 2000
wc -l src/smessage/smessage*.cpp src/wallet/wallet*.cpp src/main.cpp src/script.cpp src/net/net.cpp
```

**Step 3: Report results**

Report all hygiene check results. If any check fails, fix the issue before declaring Phase 6G complete.
