# Phase 6G Design: Structural Decomposition & Code Cleanup

**Date**: 2026-02-24
**Branch**: feature/twenty_six
**Status**: Approved
**Predecessor**: Phase 6F (protocol hardening) — all 15 tasks complete

## Goals

1. **Decompose the two largest production files** — smessage.cpp (4031 lines) and wallet.cpp (3560 lines) — into focused modules with clear boundaries
2. **Eliminate remaining code debt** — dead files, raw new/delete, C-style casts, unsafe C functions, unguarded counters
3. **Maintain 100% behavioral compatibility** — zero consensus impact, zero serialization change, verified by 1617 existing tests + both builds

## Consensus Safety Invariants

All rules from the master design document apply:
- Never modify consensus constants, checkpoint hashes, or serialization format
- Every change verified by: Linux build + Windows cross-compile + full test suite
- File splits preserve identical function signatures and behavior

---

## Stream 1: smessage.cpp Decomposition (4031 → 5 files)

### Directory: `src/smessage/`

Move smessage into its own module directory (consistent with consensus/, rpc/, wallet/, net/).

| New File | Contents | Est. Lines |
|----------|----------|------------|
| `smessage_crypto.cpp` | SecMsgCrypter (SetKey/Encrypt/Decrypt), SecureMspinkcrypt, SecureMsgDecrypt (both overloads), SecureMsgValidate, SecureMsgSetHash | ~764 |
| `smessage_db.cpp` | SecMsgDB class (Open, ReadPK/WritePK/ExistsPK, ReadSmesg/WriteSmesg/ExistsSmesg/EraseSmesg, NextSmesg/NextSmesgKey, TxnBegin/TxnCommit/TxnAbort), SecMsgBatchScanner | ~477 |
| `smessage_net.cpp` | SecureMsgReceiveData, SecureMsgSendData, ThreadSecureMsg, ThreadSecureMsgPow, SecMsgBucket::hashBucket | ~822 |
| `smessage_store.cpp` | SecureMsgSend, SecureMsgReceive, SecureMsgStore (both overloads), SecureMsgStoreUnscanned, SecureMsgRetrieve, SecureMsgScanMessage | ~542 |
| `smessage.cpp` (slimmed) | Lifecycle (Start/Shutdown/Enable/Disable), config I/O (ReadIni/WriteIni), address management (InsertAddress/GetLocalKey/GetStoredKey/GetLocalPublicKey/AddAddress/AddWalletAddresses), blockchain scanning (ScanBlock/ScanBlockChain/ScanChainForPublicKeys/ScanBuckets), wallet events (WalletUnlocked/WalletKeyChanged), utilities (getTimeString/fsReadable) | ~529 |

### Header strategy

- `smessage.h` moves to `src/smessage/smessage.h` — retains all struct/class/signal definitions
- Global state definitions (`smsgBuckets`, `smsgAddresses`, `fSecMsgenabled`, `smsgDB`, etc.) live in slimmed `smessage.cpp`
- Each split file includes `"smessage.h"` — no new internal headers needed
- External callers update from `#include "smessage.h"` to `#include "smessage/smessage.h"`

### Lock discipline (unchanged)

- `cs_smsg` — protects `smsgBuckets`, `smsgAddresses`, `fSecMsgenabled`
- `cs_smsgDB` — protects `SecMsgDB` operations
- No new locks introduced

### Bug fix: nPeerIdCounter

`nPeerIdCounter` (line 84) currently has no synchronization. Change to `std::atomic<uint32_t>`.

---

## Stream 2: wallet.cpp Decomposition (3560 → 5 files)

### Directory: `src/wallet/` (existing)

| New File | Contents | Est. Lines |
|----------|----------|------------|
| `wallet_keys.cpp` | GenerateNewKey, AddKey, AddCryptedKey, LoadKeyMetadata, LoadCryptedKey, AddCScript, LoadCScript, Lock, Unlock, ChangeWalletPassphrase, EncryptWallet, SetMinVersion, SetMaxVersion, SetBestChain, NewKeyPool, TopUpKeyPool, ReserveKeyFromKeyPool, AddReserveKey, KeepKey, ReturnKey, GetKeyFromPool, GetOldestKeyPoolTime, CReserveKey methods | ~440 |
| `wallet_tx.cpp` | AddToWallet, AddToWalletIfInvolvingMe, EraseFromWallet, WalletUpdateSpent, ScanForWalletTransactions, ReacceptWalletTransactions, ResendWalletTransactions, MarkDirty, UpdatedTransaction, IsMine(CTxIn), GetDebit(CTxIn), IsChange, GetTxTime, GetRequestCount, GetAmounts, GetAccountAmounts, AddSupportingTransactions, WriteToDisk, RelayWalletTransaction (both), balance queries (GetBalance, GetTotalMinted, GetUnconfirmedBalance, GetConfirmingBalance, GetImmatureBalance, GetStake, GetNewMint), IncOrderPosNext, OrderedTxItems, address book (SetAddressBookName, DelAddressBookName, SetAddressBookStake, DelAddressBookStake), GetTransaction, SetDefaultKey, FixSpentCoins, DisableTransaction | ~1020 |
| `wallet_send.cpp` | ApproximateBestSubset, SelectCoinsMinConf, SelectCoins, SelectCoinsForStaking, AvailableCoins, AvailableCoinsForStaking, CreateTransaction (both), CommitTransaction, SendMoney, SendMoneyToDestination, CreateStealthTransaction, SendStealthMoney, SendStealthMoneyToDestination | ~700 |
| `wallet_staking.cpp` | GetStakeWeight, CreateCoinStake, AggregateStakeOut, CountStakeOut, NewStealthAddress, AddStealthAddress, UnlockStealthAddresses, UpdateStealthAddress, FindStealthTransactions | ~830 |
| `wallet.cpp` (slimmed) | LoadWallet, LoadStakeDB, PrintWallet, GetWalletFile, GetAddressBalances, GetAddressGroupings, GetAllReserveKeys, GetKeyBirthTimes | ~570 |

### Header strategy

- `wallet.h` remains unchanged — single CWallet class definition
- All split files include `"wallet.h"` — methods are CWallet members distributed across translation units
- No new headers needed — this is purely a source file split

### Lock discipline (unchanged)

- `cs_wallet` — protects all CWallet state
- No new locks introduced

---

## Stream 3: Code Cleanup

### 3a. Delete dead file

- `src/bignum.h` — CBigNum eliminated in Phase 6F, zero references remain

### 3b. Smart pointer conversions

| File | Pattern | Target Type | Notes |
|------|---------|-------------|-------|
| `init.cpp` | `pwalletMain = new CWallet(...)` + `delete` | `std::unique_ptr<CWallet>` | Update extern in wallet.h and ~4 test files |
| `init.cpp` | `pstakeDB = new CWallet(...)` + `delete` | `std::unique_ptr<CWallet>` | Same pattern as pwalletMain |
| `main.cpp` | `mapOrphanBlocks` (CBlock*) | `std::unique_ptr<CBlock>` in map | Clear ownership, erase auto-deletes |
| `net.cpp` | `delete pnode` in disconnect | `std::unique_ptr<CNode>` in vNodesDisconnected | Check container types first |
| `bitcoinrpc.cpp` | `delete conn` (3 paths) | `std::unique_ptr<AcceptedConnection>` | Thread handoff via release()/move |
| `wallet.cpp` | `new/delete` in GetAddressGroupings | `std::unique_ptr<std::set<>>` | Local scope only |
| `util.cpp` | `new char[]`/`delete[]` in vstrprintf | `std::vector<char>` | Simpler than unique_ptr for buffers |

### 3c. C-style cast elimination

| File:Line | Current | Fix |
|-----------|---------|-----|
| `base58.h:219` | `(void*)pbegin` | `static_cast<const void*>(pbegin)` or fix callee signature |
| `crypter.cpp:30` | `(const void*)strKeyData.c_str()` | `static_cast<const void*>(...)` |
| `crypter.cpp:33` | `(unsigned char*)&scryptHash` | `reinterpret_cast<unsigned char*>(...)` |
| `db.cpp:574` | `(char*)&vchData[0]` | `reinterpret_cast<char*>(...)` |
| `db.h:138,259,262` | `(char*)datValue.get_data()` | `reinterpret_cast<char*>(...)` |
| `key.cpp:369` | `(size_t)16` | `static_cast<size_t>(16)` |
| `netbase.cpp:319` | `(void*)&set` | `static_cast<void*>(&set)` |
| `netbase.cpp:361` | `(char*)(&nRet)` | `reinterpret_cast<char*>(&nRet)` |

### 3d. Unsafe function replacement

- `checkpoints.cpp:61`: `atoi(tempStr.c_str())` → `std::stoi(tempStr)` with try-catch

### 3e. Thread safety fix

- `smessage.cpp:84`: `nPeerIdCounter` (uint32_t) → `std::atomic<uint32_t>` with `fetch_add`

---

## Verification Strategy

Each task verified by:
1. Linux build (278 units) compiles clean
2. Windows MXE cross-compile (223 units) compiles clean
3. All 1617 tests pass
4. `nm` symbol comparison where applicable (function splits)

### Hygiene checks (end of phase)
- Zero `bignum.h` references
- Zero raw `new`/`delete` in production code (outside BDB/LevelDB third-party patterns)
- Zero C-style casts in production code (outside third-party headers)
- Zero `atoi`/`atol` in production code
- No production file exceeds ~2000 lines (target: all under 1100)

---

## Task Sequencing

Three parallel streams converging at final verification:

### Stream 1: smessage decomposition (Tasks 1-6)
1. Create `src/smessage/` directory, git mv smessage.h and smessage.cpp
2. Extract smessage_db.cpp (SecMsgDB class + batch scanner)
3. Extract smessage_crypto.cpp (SecMsgCrypter + encrypt/decrypt/validate/hash)
4. Extract smessage_net.cpp (receive/send data + threads + buckets)
5. Extract smessage_store.cpp (send/receive/store/retrieve/scan message)
6. Update all include paths + CMakeLists.txt, fix nPeerIdCounter

### Stream 2: wallet decomposition (Tasks 7-11)
7. Extract wallet_keys.cpp (key management + encryption + key pool)
8. Extract wallet_tx.cpp (transaction lifecycle + balance + address book)
9. Extract wallet_send.cpp (coin selection + creation + broadcasting)
10. Extract wallet_staking.cpp (PoS + stealth addresses)
11. Update CMakeLists.txt, verify all methods accounted for

### Stream 3: code cleanup (Tasks 12-17)
12. Delete bignum.h
13. Smart pointer conversion — init.cpp (pwalletMain, pstakeDB)
14. Smart pointer conversion — main.cpp (orphan blocks), wallet.cpp (groupings), util.cpp (buffer)
15. Smart pointer conversion — net.cpp (nodes), bitcoinrpc.cpp (connections)
16. C-style cast elimination (all 10 sites)
17. Replace atoi in checkpoints.cpp

### Final (Task 18)
18. Final build verification + hygiene checks
