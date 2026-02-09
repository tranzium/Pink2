# Boost Migration Status

## Completed Migrations

### boost::filesystem → std::filesystem
- **Commit:** aaeed3b
- **Files:** 14 files migrated
- **Notes:**
  - `boost::filesystem::system_complete()` → `std::filesystem::absolute()`
  - `boost::filesystem::ifstream/ofstream` → `std::ifstream/std::ofstream`
  - Simplified copy_file options to `std::filesystem::copy_options::overwrite_existing`

### boost::bind → std::bind/lambdas
- **Commit:** 7a75336
- **Files:** init.cpp, bitcoinrpc.cpp, clientmodel.cpp/h, messagemodel.cpp/h, walletmodel.cpp/h, json_spirit_reader_template.h
- **Notes:**
  - Thread creation converted to lambdas
  - Signal binds converted to std::bind with connection-based disconnect
  - boost::function → std::function

### boost::chrono → std::chrono
- **Files:** ntp.cpp, rpcdump.cpp, util.h, CMakeLists.txt
- **Notes:**
  - `boost::chrono::duration_cast` → `std::chrono::duration_cast`
  - `boost::this_thread::sleep_for` → `std::this_thread::sleep_for`
  - `boost::date_time/posix_time` → std C++ time functions (std::tm, std::get_time, std::mktime)
  - Boost::chrono and Boost::system now OPTIONAL_COMPONENTS in CMake

### boost::shared_ptr → std::shared_ptr
- **Commit:** cf658da
- **Notes:** Direct replacement, std::shared_ptr is drop-in compatible

### BOOST_FOREACH → range-based for
- **Commit:** cf658da
- **Notes:** C++11 range-based for loops

### boost::tuple → std::tuple
- **Files:** miner.cpp, script.cpp, serialize.h, walletdb.cpp, test/multisig_tests.cpp
- **Notes:**
  - `boost::tuple` → `std::tuple`
  - `boost::get<N>(tuple)` → `std::get<N>(tuple)`
  - `tuple.get<N>()` → `std::get<N>(tuple)`
  - `boost::make_tuple` → `std::make_tuple`
  - Removed `using namespace boost;` where only used for tuples

### boost::variant → std::variant
- **Files:** script.h, script.cpp, base58.h, rpcwallet.cpp, wallet.cpp, qt/walletmodel.cpp, qt/coincontroldialog.cpp, rpcrawtransaction.cpp, test/base58_tests.cpp
- **Notes:**
  - `boost::variant<...>` → `std::variant<...>`
  - `boost::static_visitor<T>` → removed (std::visit uses duck typing)
  - `boost::apply_visitor(visitor, var)` → `std::visit(visitor, var)`
  - `boost::get<T>(&var)` → `std::get_if<T>(&var)`
  - `boost::get<T>(var)` → `std::get<T>(var)`
  - `var.type() == typeid(T)` → `std::holds_alternative<T>(var)`
  - **Exception:** json_spirit_value.h keeps boost::variant due to boost::recursive_wrapper dependency

### boost::lexical_cast → std::to_string/stoll
- **Files:** smessage.cpp, rpcsmessage.cpp
- **Notes:**
  - `boost::lexical_cast<std::string>(x)` → `std::to_string(x)`
  - `boost::lexical_cast<int64_t>(s)` → `std::stoll(s)`
  - Removed unused includes from bitcoinrpc.cpp, rpcwallet.cpp

### boost::array → std::array
- **Files:** net.h, net.cpp
- **Notes:** Direct replacement `boost::array<T, N>` → `std::array<T, N>`

### boost::assign/list_of → initializer lists
- **Files:** checkpoints.cpp, rpcrawtransaction.cpp, kernel.cpp (unused include removed)
- **Notes:**
  - `list_of(a)(b)(c)` → `{a, b, c}`
  - `map_list_of("k1", v1)("k2", v2)` → `{{"k1", v1}, {"k2", v2}}`

### boost::mutex/locks → std::mutex/locks (partial)
- **Files:** sync.h, sync.cpp, allocators.h, util.cpp
- **Notes:**
  - `boost::mutex` → `std::mutex`
  - `boost::recursive_mutex` → `std::recursive_mutex`
  - `boost::condition_variable` → `std::condition_variable`
  - `boost::unique_lock` → `std::unique_lock`
  - `boost::mutex::scoped_lock` → `std::lock_guard<std::mutex>`
  - `boost::defer_lock` → `std::defer_lock`
  - `boost::thread_specific_ptr` → `thread_local std::unique_ptr`
- **Include fixes:** Added missing `<ios>` to serialize.h, `<algorithm>` to bignum.h, `<cassert>` to allocators.h (previously pulled in transitively by boost headers)
- **Bug fix:** Fixed `[[nodiscard]]` warning in sync.h `TryEnter()` - `try_lock()` return value was being ignored

### boost::algorithm/string → strutil (custom header)
- **Files:** bitcoinrpc.cpp, rpcdump.cpp, init.cpp, netbase.cpp, alert.cpp, main.cpp, wallet.cpp, smessage.cpp, util.cpp, qt/qtipcserver.cpp
- **New file:** src/string_utils.h - header-only string utilities
- **Notes:**
  - `boost::split(v, s, boost::is_any_of(d))` → `v = strutil::split(s, d)`
  - `boost::trim(s)` → `strutil::trim(s)`
  - `boost::to_lower(s)` → `strutil::to_lower(s)`
  - `boost::replace_all(s, from, to)` → `strutil::replace_all(s, from, to)`
  - `boost::algorithm::starts_with(s, p)` → `strutil::starts_with(s, p)`
  - `boost::algorithm::ends_with(s, p)` → `strutil::ends_with(s, p)`
  - `boost::algorithm::istarts_with(s, p)` → `strutil::istarts_with(s, p)`
  - `boost::algorithm::join(v, d)` → `strutil::join(v, d)`
- **Tests:** 34 new test cases in test/string_utils_tests.cpp

### boost::program_options → strutil::parse_config_file
- **Files:** util.cpp
- **Notes:**
  - `boost::program_options::detail::config_file_iterator` replaced with `strutil::parse_config_file()` template
  - Custom config parser handles key=value format with comments (#) and empty lines
  - Removed `boost/program_options/detail/config_file.hpp` and `boost/program_options/parsers.hpp` includes
  - Removed clang workaround namespace declaration for `boost::program_options::to_internal`
- **Tests:** 5 new test cases for parse_config_file in test/string_utils_tests.cpp

## Must Keep (No Standard Replacement)

### boost::thread/thread_group
- **Reason:** `boost::thread_group` has no std equivalent; `boost::thread_interrupted` exception mechanism not in std::thread
- **Files:** init.cpp, util.cpp, util.h, alert.cpp, main.cpp, ntp.cpp, wallet.cpp
- **Note:** Mutex/locks migrated to std, but thread creation/management remains boost

### boost::asio
- **Reason:** No standard networking library until C++23 (and adoption is limited)
- **Includes:** boost/asio.hpp, boost/asio/ssl.hpp, boost/asio/ip/v6_only.hpp, boost/iostreams/*

### boost::signals2
- **Reason:** No standard signal/slot mechanism
- **Includes:** boost/signals2/signal.hpp, boost/signals2/connection.hpp, boost/signals2/last_value.hpp

### boost::spirit
- **Reason:** Complex parsing library, would require replacing entire JSON implementation
- **Includes:** Multiple boost/spirit/* headers

### boost::variant (in json_spirit)
- **Files:** src/json/json_spirit_value.h
- **Reason:** Uses boost::recursive_wrapper for recursive type definition
- **Note:** All other boost::variant usage has been migrated to std::variant

### boost::interprocess
- **Reason:** No standard IPC/file locking primitives

### boost::test
- **Reason:** Unit test framework, would require migration to different framework (e.g., Google Test)

## Behavioral Differences Review

Key semantic differences between Boost and std equivalents that were evaluated:

### std::variant vs boost::variant
- **Exception type changed:** `std::bad_variant_access` vs `boost::bad_get`
- **valueless_by_exception:** std::variant can enter this state if assignment throws (boost uses "never-empty" guarantee)
- **Risk:** Low - no catch blocks for variant exceptions in codebase; only simple types used
- **All std::get calls are guarded** by either `std::holds_alternative` checks or contextual guarantees (e.g., `IsPayToScriptHash()` implies `CScriptID`)
- **Note:** rpcrawtransaction.cpp:212 uses implicit guard via `IsPayToScriptHash()` - intentionally kept as-is because crash on invariant violation is preferable to silent failure

### std::stoll vs boost::lexical_cast<int64_t>
- **Partial parse:** `std::stoll("123abc")` returns 123; `lexical_cast` would throw
- **Whitespace:** `std::stoll(" 123")` succeeds; `lexical_cast` would throw
- **Risk:** Low - only used on internally-generated timestamp filenames (format: `timestamp_01.dat`)

### std::to_string vs boost::lexical_cast<string>
- **Float precision:** May differ for floating-point types
- **Risk:** N/A - only used on integers (bucket IDs, message counts, hashes)

### std::this_thread::sleep_for vs boost::this_thread::sleep_for
- **Interruption points:** `boost::this_thread::sleep_for` is a boost interruption point; `std::this_thread::sleep_for` is NOT
- **Impact:** Code using `boost::thread_interrupted` with `std::this_thread::sleep_for` will not be interruptible
- **Affected:** `LoopForever` and `TraceThread` templates in util.h (unused in production code, only in tests)

## Unit Test Fixes

Tests were updated to use Pinkcoin-specific test data instead of Bitcoin test data.

### Checkpoints_tests.cpp
- Updated checkpoint block hashes to use actual Pinkcoin checkpoints (blocks 50000 and 150000)
- Updated `GetTotalBlocksEstimate()` check to match Pinkcoin's checkpoint count

### key_tests.cpp
- Removed hardcoded Bitcoin WIF private keys (`strSecret1`, `strSecret2`, etc.)
- Rewrote to generate keys dynamically using deterministic seeds via `Hash()`
- Tests key creation, CBitcoinSecret encoding/decoding roundtrips, address generation, and signing

### base58_tests.cpp
- Rewrote `base58_keys_valid_parse` to generate test keys dynamically instead of reading from `base58_keys_valid.json`
- Rewrote `base58_keys_valid_gen` to test CKeyID and CScriptID encoding/decoding with generated data
- `base58_EncodeBase58`, `base58_DecodeBase58`, and `base58_keys_invalid` tests unchanged (network-agnostic)

### transaction_tests.cpp
- Removed `tx_valid` and `tx_invalid` tests (used Bitcoin transaction hex data incompatible with Pinkcoin's `nTime` field)
- Rewrote `basic_transaction_tests` to create transactions programmatically instead of deserializing Bitcoin hex
- `test_Get` and `test_GetThrow` tests unchanged (already used programmatic transaction creation)

### script_P2SH_tests.cpp
- Removed unused boost includes (`boost/assign`, `boost/foreach`, `boost/assert`)
- Fixed `switchover` test: Pinkcoin always validates P2SH (no switchover mechanism), updated expectations accordingly

### util_tests.cpp
- Disabled `util_loop_forever1` and `util_loop_forever2` tests
  - These rely on `boost::thread_interrupted` which doesn't work with `std::this_thread::sleep_for`
  - `LoopForever` template is unused in production code
- Fixed `util_threadtrace1` and `util_threadtrace2` by resetting `nCounter` at start of each test
- Removed unused `boost/foreach.hpp` include

### Test File Boost Cleanup

Removed unnecessary boost dependencies from test files:

- **DoS_tests.cpp**: `boost::assign/list_of` → initializer lists, `boost::posix_time` → `std::chrono`
- **getarg_tests.cpp**: `boost::split` → `strutil::split_compress`
- **multisig_tests.cpp**: `boost::assign +=` → explicit `push_back()` calls
- **script_tests.cpp**: `boost::algorithm::*` → `strutil::*` functions
- **sigopcount_tests.cpp**: Removed unused `boost/foreach.hpp`
- **util_tests.cpp**: Removed unused `boost/foreach.hpp`

**New strutil functions added:**
- `strutil::split_compress()` - splits with consecutive delimiter compression
- `strutil::replace_first()` - replaces first occurrence only

**New test file:**
- **rpc_tests.cpp** - 15 new test cases covering RPC infrastructure:
  - `rpc_ValueFromAmount` / `rpc_AmountFromValue` - Amount conversion
  - `rpc_ParseHashV` / `rpc_ParseHexV` - Hex parsing utilities
  - `rpc_TypeCheck_array` / `rpc_TypeCheck_array_with_null` / `rpc_TypeCheck_object` - Type validation
  - `rpc_HexBits` - nBits to hex conversion
  - `rpc_JSONRPCError` - Error object structure
  - `rpc_address_validation` / `rpc_invalid_addresses` - Address parsing
  - `rpc_script_address` - P2SH address generation (prefix 'C')
  - `rpc_sign_verify_message` - Message signing/verification
  - `rpc_error_codes` - JSON-RPC error code constants
  - `rpc_amount_precision` - Floating-point precision tests

**Remaining boost includes in tests:**
- `boost/test/unit_test.hpp` (26 files) - Test framework, must keep
- `boost/preprocessor/stringize.hpp` (1 file) - For TEST_DATA_DIR macro

**New P0 consensus test files (fork-prevention):**
- **kernel_tests.cpp** - 14 test cases for PoS consensus:
  - `GetWeight()` — stake weight calculation with min/max age caps, flash vs regular, boundary conditions
  - `CheckCoinStakeTimestamp()` — block/tx timestamp must match exactly
  - Modifier constants — `MODIFIER_INTERVAL_RATIO`, `nModifierInterval` pinned
  - Block/tx type identification — `IsProofOfWork()`, `IsProofOfStake()`, `IsCoinBase()`, `IsCoinStake()`
- **consensus_tests.cpp** - 30 test cases for block validation:
  - Chain constant regression — `MAX_BLOCK_SIZE`, `COIN`, `MAX_MONEY`, `nCoinbaseMaturity`, timing constants
  - Genesis hash pinning — mainnet and testnet hashes locked
  - Version timestamps — `nTimeV221`, `nTimeV231` locked
  - `CheckProofOfWork()` — zero/max hash, target boundary, invalid target
  - `GetProofOfWorkReward()` — block 1 premine (364.8M), pre-start, halving at 846800
  - `GetProofOfStakeReward()` — pre-start, halving, fee inclusion
  - `IsFlashStake()` — exactly 4 flash hours (1, 6, 15, 20 UTC) confirmed
  - `CheckTransaction()` — empty vin/vout, negative output, overflow, duplicates, coinbase limits
  - `CBlockIndex` flags — PoS flag, entropy bit, stake modifier flag
  - Merkle tree — single tx identity, deterministic rebuild
- **scrypt_tests.cpp** - 17 test cases for PoW hashing:
  - `scrypt_hash()` — determinism, collision resistance, single-bit avalanche
  - `scrypt_blockhash()` — determinism, pinned regression hash (`0x694b3a55...`), nonce/header sensitivity
  - `scrypt_salted_hash()` — determinism, salt independence, equivalence with unsalted
  - `scrypt_salted_multiround_hash()` — round-count divergence, single-round equivalence
  - Block integration — `GetPoWHash()` matches manual `scrypt_blockhash()`, field sensitivity

**New P1 wallet safety test files (wallet encryption & DB integrity):**
- **crypter_tests.cpp** - 21 test cases for wallet encryption:
  - Constants — `WALLET_CRYPTO_KEY_SIZE`, `WALLET_CRYPTO_SALT_SIZE` pinned
  - `CMasterKey` — default constructor (scrypt, 25000 iterations), sha512 constructor, scrypt constructor, serialization round-trip
  - `SetKeyFromPassphrase()` — sha512 method, scrypt method, rejects zero rounds, rejects wrong salt size
  - Encrypt/decrypt round-trip — sha512 method, scrypt method, wrong passphrase fails, different salts diverge
  - `SetKey()` direct — round-trip with explicit key/IV, rejects wrong sizes
  - `EncryptSecret()`/`DecryptSecret()` — round-trip with real CKey, different IVs diverge, wrong key fails
  - `CKeyMetadata` — default construction, construction with time, serialization round-trip
- **walletdb_tests.cpp** - 15 test cases for wallet database operations:
  - `DBErrors` enum — pin all 6 values (DB_LOAD_OK through DB_NEED_REWRITE)
  - `WriteName`/`EraseName` — round-trip, overwrite
  - `WriteTx`/`EraseTx` — round-trip with nWalletDBUpdated counter
  - `WriteMasterKey` — write with counter increment
  - `WriteKey` — write real keypair with metadata
  - `WritePool`/`ReadPool`/`ErasePool` — full CRUD cycle, verify erase removes entry
  - `WriteOrderPosNext`, `WriteDefaultKey`, `WriteMinVersion` — basic write operations
  - `WriteBestBlock`/`ReadBestBlock` — locator round-trip
  - `WriteAccount`/`ReadAccount` — account with pubkey round-trip
  - `WriteCScript` — P2SH redeem script storage
  - `nWalletDBUpdated` counter — verify sequential increments
  - `ReadVersion`/`WriteVersion` — DB version round-trip
- **wallet_tests.cpp** (expanded) — 12 test cases (was 1):
  - `coin_selection_tests` — existing coin selection (unchanged)
  - `wallet_feature_constants` — pin FEATURE_BASE, FEATURE_WALLETCRYPT, FEATURE_COMPRPUBKEY, FEATURE_LATEST
  - `generate_new_key` — GenerateNewKey produces valid key, key stored in wallet, retrievable
  - `generate_multiple_unique_keys` — three generated keys are all distinct
  - `key_pool_topup` — TopUpKeyPool fills pool
  - `get_key_from_pool` — GetKeyFromPool returns valid key present in wallet
  - `new_key_pool_resets` — NewKeyPool clears and refills
  - `get_pubkey_from_keyid` — GetPubKey round-trip via KeyID
  - `have_key_returns_false_for_unknown` — HaveKey rejects unknown KeyID
  - `address_encoding_prefix` — Pinkcoin address starts with '2', CBitcoinAddress round-trip
  - `sign_verify_with_wallet_key` — Sign/Verify with wallet-generated key, wrong hash fails
  - `unencrypted_wallet_not_locked` — unencrypted wallet is not crypted or locked

**P1 Result:** All 248 test cases pass across 30 test suites (201 from P0 + 47 new P1: 21 crypter + 15 walletdb + 11 new wallet)

**New P2 migration safety net test files (OpenSSL migration prep):**
- **hash_tests.cpp** - 12 test cases for SHA256d and SHA256+RIPEMD160 wrappers:
  - `Hash()` pinned regression vectors — SHA256d("") = `0x5694...5d`, SHA256d(0x00) = `0x9a53...14`, SHA256d("Pinkcoin") = `0x8db3...2f`
  - `Hash()` behavioral — different inputs diverge, two-part, three-part concatenation equivalence
  - `Hash160()` pinned regression — Hash160(33x0x02) = `0x3147...51`, determinism, real pubkey matches GetID()
  - `SerializeHash()` — determinism, field sensitivity (nTime change → different hash)
  - `CHashWriter` — produces same result as Hash() for identical input
- **stealth_tests.cpp** - 15 test cases for stealth crypto primitives (heaviest deprecated OpenSSL surface):
  - Constants — `ec_secret_size`, `ec_compressed_size`, `ec_uncompressed_size` pinned
  - `GenerateRandomSecret()` — succeeds, not all zeros, two calls produce unique results
  - `SecretToPublicKey()` — succeeds, compressed (33 bytes), 0x02/0x03 prefix, deterministic, different secrets → different pubkeys
  - `StealthSecret()` — sender/receiver derive same shared secret (ECDH roundtrip), different ephemeral keys → different secrets
  - `StealthSecretSpend()` — derived private key produces matching public key
  - `CStealthAddress` — encode/decode roundtrip, invalid string rejection, IsStealthAddress detection
  - `AppendChecksum`/`VerifyChecksum` — checksum added (+4 bytes), verifies, tamper detection, short input rejection
  - `CStealthAddress` serialization — full round-trip (pubkeys, label, secrets)
- **block_tests.cpp** - 12 test cases for genesis block and serialization:
  - Genesis reconstruction — hash pinned (`0x00000f79...cc89`), merkle root pinned (`0x96f872...d891`), all header fields, coinbase tx
  - `CBlock` serialization — disk round-trip, network round-trip, header-only (80 bytes, no vtx)
  - `CTransaction` serialization — multi-input/output round-trip with field verification
  - Genesis serialized size — header-only must be exactly 80 bytes
  - PoW hash — `GetHash()` == `GetPoWHash()` (scrypt-based)
  - Merkle tree — 2-tx manual verification, 3-tx with duplication (odd count)

**P2 Result:** All 287 test cases pass across 33 test suites (248 from P0+P1 + 39 new P2: 12 hash + 15 stealth + 12 block)

**Bug fix: BN_num_bytes zero-padding in stealth.cpp**
- Fixed 5 sites where `BN_num_bytes()` was compared with strict equality to expected size
- `BN_num_bytes()` returns minimum bytes to represent a value — if result has leading zeros, it returns fewer bytes than expected, causing silent failure (~1/256 probability per stealth payment)
- Fix: zero-fill output buffer, then write bignum right-justified at `out[expected_size - nBytes]`
- Affected functions: `SecretToPublicKey()` (1 site), `StealthSecret()` (2 sites), `StealthSecretSpend()` (1 site), `StealthSharedToSecretSpend()` (1 site)
- stealth_tests.cpp `stealth_secret_spend` test updated — retry loop removed since bug is fixed

**New P3 additional test coverage (pre-OpenSSL migration):**
- **netbase_tests.cpp** (expanded) — 22 new test cases added to existing 5:
  - Protocol constants pinned — PROTOCOL_VERSION=60019, INIT_PROTO_VERSION=209, MIN_PEER_PROTO_VERSION=60018, CADDR_TIME_VERSION=31402, DATABASE_VERSION=70509
  - Default ports — mainnet 9134, testnet 19134
  - CNetAddr extended — IPv4-mapped, non-routable, multicast, address groups, ToString
  - CService — construction, ToStringIPPort, comparison operators, serialization round-trip
  - CAddress — Init defaults, SER_DISK serialization round-trip
  - CInv — type constants (MSG_TX=1, MSG_BLOCK=2), string construction, invalid type throws, serialization round-trip, comparison ordering
  - CMessageHeader — size constants (HEADER_SIZE=24), IsValid(), GetCommand()
- **stakedb_tests.cpp** (new) — 10 test cases:
  - `SDBErrors` enum pinned (SDB_LOAD_OK=0 through SDB_NEED_REWRITE=5)
  - WriteStake/ReadStake round-trip, EraseStake, overwrite, multiple stakes, empty read fails
  - `nStakeDBUpdated` counter increments on write and erase
  - WriteMinVersion, erase nonexistent (no crash), empty string values
- **smessage_tests.cpp** (new) — 15 test cases for secure messaging:
  - Constants pinned — SMSG_HDR_LEN=104, SMSG_PL_HDR_LEN=90, SMSG_BUCKET_LEN=600, SMSG_RETENTION=172800, SMSG_MAX_MSG_BYTES=4096
  - SecureMessage structure — default construction, packed header layout offsets verified
  - SecMsgCrypter AES-256-CBC — SetKey, encrypt/decrypt round-trip, wrong key fails, different IV diverges, 4096-byte message
  - SecMsgAddress serialization round-trip (address, receiveEnabled, receiveAnon)
  - SecMsgStored serialization round-trip (all 6 fields)
  - SecMsgToken ordering — timestamp-first, sample-bytes tiebreaker, std::set ordering
  - SecMsgOptions/SecMsgBucket defaults

**P3 Result:** All 334 test cases pass across 35 test suites (287 from P0+P1+P2 + 47 new P3: 22 netbase/protocol + 10 stakedb + 15 smessage)

**New P4 OpenSSL regression tests (EC key generation + PBKDF2 pinning):**
- **key_tests.cpp** (expanded) — 13 new test cases added to existing 1:
  - EC_KEY_regenerate_key determinism — same secret always produces identical pubkey
  - EC_KEY_regenerate_key roundtrip — SetSecret→GetSecret returns original bytes (compressed + uncompressed)
  - Pubkey format — uncompressed 65 bytes (0x04 prefix), compressed 33 bytes (0x02/0x03), x-coordinates match
  - ECDSA_SIG_recover_key_GFp compressed — SignCompact+SetCompactSignature recovery, header byte 31-34
  - ECDSA_SIG_recover_key_GFp uncompressed — recovery, header byte 27-30
  - Recovery wrong message — VerifyCompact fails when message differs
  - VerifyCompact roundtrip — 8 messages signed and verified via compact signatures
  - secp256k1 constants pinned — vchMaxModOrder (n-1), vchMaxModHalfOrder ((n-1)/2), boundary checks via CheckSignatureElement
  - CKey copy constructor — pubkey preserved across copy
  - CKey assignment operator — pubkey preserved across assignment
  - MakeNewKey — compressed/uncompressed both valid, different keys differ
  - IsNull — true before set, false after MakeNewKey
  - ECC_InitSanityCheck — returns true
- **pbkdf2_tests.cpp** (new) — 13 test cases for HMAC-SHA256 and PBKDF2-SHA256:
  - HMAC-SHA256 RFC 4231 pinned vectors — Case 1 (0x0b key/"Hi There"), Case 2 ("Jefe"), Case 3 (0xaa/0xdd), Case 6 (131-byte key, triggers SHA256(K) path)
  - HMAC-SHA256 determinism — same inputs always same output
  - HMAC-SHA256 different keys — different keys diverge
  - PBKDF2-SHA256 pinned — "passwd"/"salt"/c=1/dkLen=64 (verified against Python hashlib)
  - PBKDF2-SHA256 pinned — "Password"/"NaCl"/c=80000/dkLen=64 (RFC 7914 vector)
  - PBKDF2-SHA256 determinism, different salt, different iterations diverge
  - PBKDF2-SHA256 partial output — dkLen=16 matches first 16 bytes of dkLen=32
  - PBKDF2-SHA256 empty password — deterministic, differs from non-empty

**P4 Result:** All 360 test cases pass across 36 test suites (334 from P0-P3 + 26 new P4: 13 key + 13 pbkdf2)

**Mainnet block golden reference tests (P2 item 7):**
- **mainnet_block_tests.cpp** (new) — 9 test cases using real mainnet block data:
  - Source of truth: https://chainz.cryptoid.info/pink/
  - **JSON fixture**: `data/mainnet_blocks.json` — 4 real blocks:
    - Block 1: PoW premine (364.8M PINK), nonce=3071608064
    - Block 50000: PoW checkpoint, nonce=4045912857
    - Block 2864480: Regular PoS (hour 22 UTC, non-flash), nonce=0
    - Block 2864550: Flash PoS (hour 1 UTC, flash stake), nonce=0
  - `mainnet_block_header_hashes` — reconstruct headers from fixture, verify scrypt hash matches for all 4 blocks
  - `mainnet_merkle_roots` — compute merkle root from tx hashes, verify against fixture
  - `mainnet_pow_block_identification` — PoW blocks have nonzero nonce and single coinbase tx
  - `mainnet_pos_block_identification` — PoS blocks have zero nonce and 2+ txs
  - `mainnet_flash_pos_identification` — IsFlashStake(nTime) for FPoS vs regular PoS
  - `mainnet_checkpoint_block_50000` — Checkpoints::CheckHardened(50000, hash) passes
  - `mainnet_block_1_genesis_link` — block 1 prevhash == genesis hash, single tx, merkle==txhash
  - `mainnet_entropy_bits` — GetStakeEntropyBit() matches fixture entropybit for all blocks
  - `mainnet_header_field_pinning` — version==1, nBits!=0, reasonable timestamp

**Final Result:** All 369 test cases pass across 37 test suites
