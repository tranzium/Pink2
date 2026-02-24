// Copyright (c) 2024 The Pinkcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

//
// Golden Reference Tests — Backward Compatibility Safety Net
//
// These tests pin the exact byte-level serialization of every critical type.
// Any future code change that alters serialized output will fail immediately.
//
// Pattern: construct deterministic object → serialize → compare to hardcoded hex.
// Reverse: deserialize hardcoded hex → verify all fields match.
//
// CLIENT_VERSION note: Several types write nVersion = CLIENT_VERSION (2040000 =
// 0x001F20C0, LE: c0201f00) at serialization start. If CLIENT_VERSION changes
// in a future release, those golden values must be updated. This is intentional
// — it forces conscious acknowledgment that the version bump affects formats.
//

#include <boost/test/unit_test.hpp>

#include "main.h"
#include "wallet.h"
#include "walletdb.h"
#include "key.h"
#include "base58.h"
#include "script.h"
#include "stealth.h"
#include "crypter.h"
#include "protocol.h"
#include "version.h"
#include "arith_uint256.h"
#include "scriptnum.h"
#include "util.h"
#include "alert.h"
#include "checkpoints.h"

// Helper: serialize any object to hex string
template<typename T>
static std::string SerializeToHex(const T& obj, int nType = SER_DISK, int nVersion = CLIENT_VERSION)
{
    CDataStream ss(nType, nVersion);
    ss << obj;
    return HexStr(ss.begin(), ss.end());
}

// Helper: deserialize from hex string
template<typename T>
static T DeserializeFromHex(const std::string& hex, int nType = SER_DISK, int nVersion = CLIENT_VERSION)
{
    std::vector<unsigned char> data = ParseHex(hex);
    CDataStream ss(data, nType, nVersion);
    T obj;
    ss >> obj;
    return obj;
}

// Helper: genesis block (reuse from block_tests.cpp pattern)
static CBlock CreateGenesisBlock()
{
    const char* pszTimestamp = "A black hole is a stable energy construct that occupies greater than three dimensions of physical space.";
    CTransaction txNew;
    txNew.nTime = 1486329989;
    txNew.vin.resize(1);
    txNew.vout.resize(1);
    txNew.vin[0].scriptSig = CScript() << 0 << CScriptNum(42).getvch()
        << std::vector<unsigned char>((const unsigned char*)pszTimestamp,
                                      (const unsigned char*)pszTimestamp + strlen(pszTimestamp));
    txNew.vout[0].SetEmpty();

    CBlock block;
    block.vtx.push_back(txNew);
    block.hashPrevBlock = 0;
    block.hashMerkleRoot = block.BuildMerkleTree();
    block.nVersion = 1;
    block.nTime    = 1486329989;
    block.nBits    = UintToArith256(~uint256(0) >> 20).GetCompact();
    block.nNonce   = 6777712;

    return block;
}

// Deterministic secret key: Hash("golden test secret N")
static CSecret MakeTestSecret(int n)
{
    std::string seed = strprintf("golden test secret %d", n);
    uint256 hash = Hash(seed.begin(), seed.end());
    CSecret secret(hash.begin(), hash.begin() + 32);
    return secret;
}

BOOST_AUTO_TEST_SUITE(golden_tests)

// ============================================================================
// Section 1: Transaction Format (7 tests)
// Pin the Pinkcoin-specific nTime field at byte offset 4.
// Layout: nVersion(4) + nTime(4) + vin + vout + nLockTime(4)
// ============================================================================

BOOST_AUTO_TEST_CASE(golden_outpoint_serialization)
{
    // COutPoint: FLATDATA hash(32 LE) + n(4 LE) = 36 bytes
    COutPoint op;
    op.hash = uint256("0x0102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f20");
    op.n = 7;

    std::string hex = SerializeToHex(op);
    BOOST_CHECK_EQUAL(hex, "201f1e1d1c1b1a191817161514131211100f0e0d0c0b0a09080706050403020107000000");

    // Reverse: deserialize and verify
    COutPoint op2 = DeserializeFromHex<COutPoint>(hex);
    BOOST_CHECK(op2.hash == op.hash);
    BOOST_CHECK_EQUAL(op2.n, 7u);
}

BOOST_AUTO_TEST_CASE(golden_txin_serialization)
{
    // CTxIn: prevout(36) + varint(scriptSig len) + scriptSig + nSequence(4)
    CTxIn txin;
    txin.prevout.hash = uint256("0xaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    txin.prevout.n = 0;
    txin.scriptSig = CScript() << std::vector<unsigned char>(4, 0xff);
    txin.nSequence = 0xffffffff;

    std::string hex = SerializeToHex(txin);
    BOOST_CHECK_EQUAL(hex,
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
        "00000000"
        "05"
        "04ffffffff"
        "ffffffff"
    );

    CTxIn txin2 = DeserializeFromHex<CTxIn>(hex);
    BOOST_CHECK(txin2.prevout.hash == txin.prevout.hash);
    BOOST_CHECK_EQUAL(txin2.prevout.n, 0u);
    BOOST_CHECK_EQUAL(txin2.nSequence, 0xffffffffu);
}

BOOST_AUTO_TEST_CASE(golden_txout_serialization)
{
    // CTxOut: nValue(8 LE) + varint(scriptPubKey len) + scriptPubKey
    CTxOut txout;
    txout.nValue = 100 * COIN;
    txout.scriptPubKey = CScript() << OP_DUP << OP_HASH160
        << std::vector<unsigned char>(20, 0x42) << OP_EQUALVERIFY << OP_CHECKSIG;

    std::string hex = SerializeToHex(txout);
    BOOST_CHECK_EQUAL(hex,
        "00e40b5402000000"
        "19"
        "76a914" "4242424242424242424242424242424242424242" "88ac"
    );

    CTxOut txout2 = DeserializeFromHex<CTxOut>(hex);
    BOOST_CHECK_EQUAL(txout2.nValue, 100 * COIN);
    BOOST_CHECK(txout2.scriptPubKey == txout.scriptPubKey);
}

BOOST_AUTO_TEST_CASE(golden_coinbase_tx)
{
    // Full coinbase tx — verifies nTime at byte offset 4
    CTransaction tx;
    tx.nVersion = 1;
    tx.nTime = 1700000000;
    tx.vin.resize(1);
    tx.vin[0].prevout.SetNull();
    tx.vin[0].scriptSig = CScript() << 1;
    tx.vout.resize(1);
    tx.vout[0].nValue = 100 * COIN;
    tx.vout[0].scriptPubKey = CScript() << OP_TRUE;
    tx.nLockTime = 0;

    std::string hex = SerializeToHex(tx);
    BOOST_CHECK_EQUAL(hex,
        "01000000"
        "00f15365"
        "01"
        "0000000000000000000000000000000000000000000000000000000000000000"
        "ffffffff"
        "0151"
        "ffffffff"
        "01"
        "00e40b5402000000"
        "0151"
        "00000000"
    );

    // Verify nTime is at offset 4
    BOOST_CHECK(hex.substr(0, 8) == "01000000");
    BOOST_CHECK(hex.substr(8, 8) == "00f15365");
    BOOST_CHECK(tx.IsCoinBase());
}

BOOST_AUTO_TEST_CASE(golden_regular_tx)
{
    CTransaction tx;
    tx.nVersion = 1;
    tx.nTime = 1700000000;
    tx.vin.resize(2);
    tx.vin[0].prevout.hash = uint256("0x1111111111111111111111111111111111111111111111111111111111111111");
    tx.vin[0].prevout.n = 0;
    tx.vin[0].scriptSig = CScript() << std::vector<unsigned char>(8, 0xab);
    tx.vin[1].prevout.hash = uint256("0x2222222222222222222222222222222222222222222222222222222222222222");
    tx.vin[1].prevout.n = 1;
    tx.vin[1].scriptSig = CScript() << std::vector<unsigned char>(8, 0xcd);
    tx.vout.resize(2);
    tx.vout[0].nValue = 50 * COIN;
    tx.vout[0].scriptPubKey = CScript() << OP_DUP << OP_HASH160
        << std::vector<unsigned char>(20, 0xaa) << OP_EQUALVERIFY << OP_CHECKSIG;
    tx.vout[1].nValue = 30 * COIN;
    tx.vout[1].scriptPubKey = CScript() << OP_DUP << OP_HASH160
        << std::vector<unsigned char>(20, 0xbb) << OP_EQUALVERIFY << OP_CHECKSIG;
    tx.nLockTime = 0;

    std::string hex = SerializeToHex(tx);
    BOOST_CHECK_EQUAL(hex,
        "01000000"
        "00f15365"
        "02"
        "1111111111111111111111111111111111111111111111111111111111111111"
        "00000000"
        "09" "08abababababababab"
        "ffffffff"
        "2222222222222222222222222222222222222222222222222222222222222222"
        "01000000"
        "09" "08cdcdcdcdcdcdcdcd"
        "ffffffff"
        "02"
        "00f2052a01000000"
        "19" "76a914" "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa" "88ac"
        "005ed0b200000000"
        "19" "76a914" "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb" "88ac"
        "00000000"
    );
}

BOOST_AUTO_TEST_CASE(golden_tx_hash_pinned)
{
    // Deserialize a known tx → GetHash() → pin SHA256d result
    const std::string txhex =
        "01000000"
        "00f15365"
        "01"
        "0000000000000000000000000000000000000000000000000000000000000000"
        "ffffffff"
        "0151"
        "ffffffff"
        "01"
        "00e40b5402000000"
        "0151"
        "00000000";

    CTransaction tx = DeserializeFromHex<CTransaction>(txhex);
    BOOST_CHECK_EQUAL(tx.GetHash().GetHex(),
        "c787bc970dd7768666c97da160087678c124a22437026df0b71fe7184cff2a24");

    // Round-trip: re-serialize matches original hex
    BOOST_CHECK_EQUAL(SerializeToHex(tx), txhex);
}

BOOST_AUTO_TEST_CASE(golden_coinstake_tx)
{
    // PoS coinstake: empty vout[0], IsCoinStake()=true
    CTransaction tx;
    tx.nVersion = 1;
    tx.nTime = 1700000000;
    tx.vin.resize(1);
    tx.vin[0].prevout.hash = uint256("0x3333333333333333333333333333333333333333333333333333333333333333");
    tx.vin[0].prevout.n = 0;
    tx.vin[0].scriptSig = CScript();
    tx.vout.resize(2);
    tx.vout[0].SetEmpty();
    tx.vout[1].nValue = 100 * COIN;
    tx.vout[1].scriptPubKey = CScript() << OP_DUP << OP_HASH160
        << std::vector<unsigned char>(20, 0xdd) << OP_EQUALVERIFY << OP_CHECKSIG;
    tx.nLockTime = 0;

    BOOST_CHECK(tx.IsCoinStake());
    BOOST_CHECK(!tx.IsCoinBase());

    std::string hex = SerializeToHex(tx);
    BOOST_CHECK_EQUAL(hex,
        "01000000"
        "00f15365"
        "01"
        "3333333333333333333333333333333333333333333333333333333333333333"
        "00000000"
        "00"
        "ffffffff"
        "02"
        "0000000000000000"
        "00"
        "00e40b5402000000"
        "19" "76a914" "dddddddddddddddddddddddddddddddddddddddd" "88ac"
        "00000000"
    );

    CTransaction tx2 = DeserializeFromHex<CTransaction>(hex);
    BOOST_CHECK(tx2.IsCoinStake());
    BOOST_CHECK(tx2.vout[0].IsEmpty());
}

// ============================================================================
// Section 2: Block Format (5 tests)
// Pin Pinkcoin-specific vchBlockSig and conditional serialization.
// ============================================================================

BOOST_AUTO_TEST_CASE(golden_block_header_80bytes)
{
    CBlock block;
    block.nVersion = 1;
    block.hashPrevBlock = uint256("0xaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    block.hashMerkleRoot = uint256("0xbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    block.nTime = 1700000000;
    block.nBits = 0x1e0fffff;
    block.nNonce = 12345;

    CDataStream ss(SER_BLOCKHEADERONLY, CLIENT_VERSION);
    ss << block;
    BOOST_CHECK_EQUAL(ss.size(), 80u);

    std::string hex = HexStr(ss.begin(), ss.end());
    BOOST_CHECK_EQUAL(hex,
        "01000000"
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
        "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"
        "00f15365"
        "ffff0f1e"
        "39300000"
    );
}

BOOST_AUTO_TEST_CASE(golden_genesis_block_full_hex)
{
    // Complete genesis block — the ultimate canary
    CBlock genesis = CreateGenesisBlock();
    std::string hex = SerializeToHex(genesis);

    BOOST_CHECK_EQUAL(hex,
        "01000000"
        "0000000000000000000000000000000000000000000000000000000000000000"
        "91d8fa263b4f000ae281fc1c8046b06a5c307ae24385c1bdad0a339c3172f896"
        "85989758"
        "ffff0f1e"
        "706b6700"
        "01"
        "0100000085989758010000000000000000000000000000000000000000000000"
        "000000000000000000ffffffff6d00012a4c684120626c61636b20686f6c6520"
        "6973206120737461626c6520656e6572677920636f6e737472756374207468"
        "6174206f636375706965732067726561746572207468616e20746872656520"
        "64696d656e73696f6e73206f6620706879736963616c2073706163652effff"
        "ffff0100000000000000000000000000"
        "00"
    );

    // Verify round-trip
    CBlock genesis2 = DeserializeFromHex<CBlock>(hex);
    BOOST_CHECK(genesis2.GetHash() == genesis.GetHash());
    BOOST_CHECK(genesis2.hashMerkleRoot == genesis.hashMerkleRoot);
    BOOST_CHECK_EQUAL(genesis2.vtx.size(), 1u);
}

BOOST_AUTO_TEST_CASE(golden_genesis_hash_from_hex)
{
    CBlock genesis = CreateGenesisBlock();
    std::string hex = SerializeToHex(genesis);
    CBlock block = DeserializeFromHex<CBlock>(hex);
    BOOST_CHECK(block.GetHash() == hashGenesisBlock);
}

BOOST_AUTO_TEST_CASE(golden_block_with_blocksig)
{
    CBlock block;
    block.nVersion = 1;
    block.hashPrevBlock = uint256("0x1111111111111111111111111111111111111111111111111111111111111111");
    block.hashMerkleRoot = uint256("0x2222222222222222222222222222222222222222222222222222222222222222");
    block.nTime = 1700000000;
    block.nBits = 0x1e0fffff;
    block.nNonce = 0;

    CTransaction cb;
    cb.nVersion = 1;
    cb.nTime = 1700000000;
    cb.vin.resize(1);
    cb.vin[0].prevout.SetNull();
    cb.vin[0].scriptSig = CScript() << 1;
    cb.vout.resize(1);
    cb.vout[0].SetEmpty();
    block.vtx.push_back(cb);
    block.vchBlockSig = std::vector<unsigned char>(64, 0xfe);

    std::string hex = SerializeToHex(block);

    CBlock block2 = DeserializeFromHex<CBlock>(hex);
    BOOST_CHECK_EQUAL(block2.vchBlockSig.size(), 64u);
    BOOST_CHECK_EQUAL(block2.vtx.size(), 1u);
    BOOST_CHECK(block2.vchBlockSig == block.vchBlockSig);

    // Size = header(80) + varint(1) + tx + varint(64) + sig(64)
    CDataStream ssTx(SER_DISK, CLIENT_VERSION);
    ssTx << cb;
    BOOST_CHECK_EQUAL(hex.size() / 2, 80u + 1u + ssTx.size() + 1u + 64u);
}

BOOST_AUTO_TEST_CASE(golden_block_header_vs_full)
{
    CBlock genesis = CreateGenesisBlock();

    CDataStream ssHeader(SER_BLOCKHEADERONLY, CLIENT_VERSION);
    ssHeader << genesis;
    BOOST_CHECK_EQUAL(ssHeader.size(), 80u);

    CDataStream ssFull(SER_DISK, CLIENT_VERSION);
    ssFull << genesis;

    CDataStream ssTx(SER_DISK, CLIENT_VERSION);
    ssTx << genesis.vtx[0];

    // full = header(80) + varint(1) + tx + varint(0)
    BOOST_CHECK_EQUAL(ssFull.size(), 80u + 1u + ssTx.size() + 1u);
}

// ============================================================================
// Section 3: Key & Address Encoding (6 tests)
// Pin Pinkcoin-specific version bytes.
// ============================================================================

BOOST_AUTO_TEST_CASE(golden_wif_compressed)
{
    CSecret secret = MakeTestSecret(1);
    CBitcoinSecret wif;
    wif.SetSecret(secret, true);

    BOOST_CHECK_EQUAL(wif.ToString(), "LTVv4v6ehwufb7Y46n31uYH7zSoRmFqs8zaK9gogBbq5p3snWgoc");

    CBitcoinSecret wif2;
    BOOST_CHECK(wif2.SetString("LTVv4v6ehwufb7Y46n31uYH7zSoRmFqs8zaK9gogBbq5p3snWgoc"));
    bool fCompressed = false;
    CSecret secret2 = wif2.GetSecret(fCompressed);
    BOOST_CHECK(fCompressed);
    BOOST_CHECK(secret2 == secret);
}

BOOST_AUTO_TEST_CASE(golden_wif_uncompressed)
{
    CSecret secret = MakeTestSecret(1);
    CBitcoinSecret wif;
    wif.SetSecret(secret, false);

    BOOST_CHECK_EQUAL(wif.ToString(), "5QgBDDV1qhXxsKhZshZdTQQ8voB4oH3v3ibHM6PowWeB9r8XGE5");

    CBitcoinSecret wif2;
    BOOST_CHECK(wif2.SetString("5QgBDDV1qhXxsKhZshZdTQQ8voB4oH3v3ibHM6PowWeB9r8XGE5"));
    bool fCompressed = false;
    CSecret secret2 = wif2.GetSecret(fCompressed);
    BOOST_CHECK(!fCompressed);
    BOOST_CHECK(secret2 == secret);
}

BOOST_AUTO_TEST_CASE(golden_p2pkh_address)
{
    CSecret secret = MakeTestSecret(1);
    CKey key;
    key.SetSecret(secret, true);
    CPubKey pubkey = key.GetPubKey();
    BOOST_CHECK(pubkey.IsValid());
    BOOST_CHECK(pubkey.IsCompressed());

    CBitcoinAddress addr(pubkey.GetID());
    BOOST_CHECK_EQUAL(addr.ToString(), "2NF8NAfTuMks1Pfg3W4YccFx7pxSPmhap7");
    BOOST_CHECK_EQUAL(addr.ToString()[0], '2');

    CBitcoinAddress addr2("2NF8NAfTuMks1Pfg3W4YccFx7pxSPmhap7");
    BOOST_CHECK(addr2.IsValid());
    CKeyID keyID;
    BOOST_CHECK(addr2.GetKeyID(keyID));
    BOOST_CHECK(keyID == pubkey.GetID());
}

BOOST_AUTO_TEST_CASE(golden_p2sh_address)
{
    std::vector<unsigned char> hashBytes(20, 0x42);
    uint160 scriptHash(hashBytes);
    CScriptID sid(scriptHash);
    CBitcoinAddress addr(sid);

    BOOST_CHECK_EQUAL(addr.ToString(), "CNWEYSp4Rs35UZqNtiSHjqZcGYr2ta1Y4C");
    BOOST_CHECK_EQUAL(addr.ToString()[0], 'C');

    CBitcoinAddress addr2("CNWEYSp4Rs35UZqNtiSHjqZcGYr2ta1Y4C");
    BOOST_CHECK(addr2.IsValid());
    BOOST_CHECK(addr2.IsScript());
}

BOOST_AUTO_TEST_CASE(golden_cpubkey_serialization)
{
    CSecret secret = MakeTestSecret(1);
    CKey key;
    key.SetSecret(secret, true);

    std::string hex = SerializeToHex(key.GetPubKey());
    BOOST_CHECK_EQUAL(hex, "21024b40edcd94ffcb805f55ddb552c4fe7e22fd4c274e25e0b953085708a947cefb");
    BOOST_CHECK_EQUAL(hex.size(), 68u);  // (1 + 33) * 2
    BOOST_CHECK(hex.substr(0, 2) == "21");  // varint(33)
}

BOOST_AUTO_TEST_CASE(golden_address_version_bytes)
{
    BOOST_CHECK_EQUAL(CBitcoinAddress::PUBKEY_ADDRESS, 3);
    BOOST_CHECK_EQUAL(CBitcoinAddress::SCRIPT_ADDRESS, 28);
    BOOST_CHECK_EQUAL(CBitcoinAddress::PUBKEY_ADDRESS_TEST, 55);
    BOOST_CHECK_EQUAL(CBitcoinAddress::SCRIPT_ADDRESS_TEST, 196);
    BOOST_CHECK_EQUAL(128 + CBitcoinAddress::PUBKEY_ADDRESS, 131);
}

// ============================================================================
// Section 4: Script Byte Format (4 tests)
// ============================================================================

BOOST_AUTO_TEST_CASE(golden_p2pkh_script)
{
    std::vector<unsigned char> hash160(20, 0x42);
    CScript script;
    script << OP_DUP << OP_HASH160 << hash160 << OP_EQUALVERIFY << OP_CHECKSIG;

    BOOST_CHECK_EQUAL(script.size(), 25u);
    BOOST_CHECK_EQUAL(HexStr(script.begin(), script.end()),
        "76a914" "4242424242424242424242424242424242424242" "88ac");
}

BOOST_AUTO_TEST_CASE(golden_p2sh_script)
{
    std::vector<unsigned char> hash160(20, 0x42);
    CScript script;
    script << OP_HASH160 << hash160 << OP_EQUAL;

    BOOST_CHECK_EQUAL(script.size(), 23u);
    BOOST_CHECK_EQUAL(HexStr(script.begin(), script.end()),
        "a914" "4242424242424242424242424242424242424242" "87");
}

BOOST_AUTO_TEST_CASE(golden_op_return_script)
{
    std::vector<unsigned char> data = {0xde, 0xad, 0xbe, 0xef};
    CScript script;
    script << OP_RETURN << data;

    BOOST_CHECK_EQUAL(HexStr(script.begin(), script.end()), "6a04deadbeef");
}

BOOST_AUTO_TEST_CASE(golden_coinbase_scriptsig)
{
    CBlock genesis = CreateGenesisBlock();
    const CScript& scriptSig = genesis.vtx[0].vin[0].scriptSig;
    std::string hex = HexStr(scriptSig.begin(), scriptSig.end());

    // Structure: 00 (OP_0) + 012a (push 1 byte 0x2a=42) + 4c68 (OP_PUSHDATA1 + len 104) + <timestamp>
    BOOST_CHECK(hex.substr(0, 2) == "00");
    BOOST_CHECK(hex.substr(2, 4) == "012a");
    BOOST_CHECK_EQUAL(hex.substr(6, 4), "4c68");

    const char* pszTimestamp = "A black hole is a stable energy construct that occupies greater than three dimensions of physical space.";
    BOOST_CHECK_EQUAL(strlen(pszTimestamp), 104u);
    std::string tsHex = HexStr((const unsigned char*)pszTimestamp,
                                (const unsigned char*)pszTimestamp + 104);
    BOOST_CHECK(hex.substr(10) == tsHex);
}

// ============================================================================
// Section 5: Stealth Address (3 tests)
// ============================================================================

BOOST_AUTO_TEST_CASE(golden_stealth_address_encoded)
{
    CSecret scanSecret = MakeTestSecret(2);
    CSecret spendSecret = MakeTestSecret(3);
    CKey scanKey, spendKey;
    scanKey.SetSecret(scanSecret, true);
    spendKey.SetSecret(spendSecret, true);

    CStealthAddress sa;
    sa.options = 0;
    sa.scan_pubkey.resize(33);
    memcpy(&sa.scan_pubkey[0], &scanKey.GetPubKey().Raw()[0], 33);
    sa.spend_pubkey.resize(33);
    memcpy(&sa.spend_pubkey[0], &spendKey.GetPubKey().Raw()[0], 33);
    sa.scan_secret.resize(32);
    memcpy(&sa.scan_secret[0], scanSecret.data(), 32);
    sa.spend_secret.resize(32);
    memcpy(&sa.spend_secret[0], spendSecret.data(), 32);
    sa.label = "";
    sa.number_signatures = 0;
    sa.prefix.number_bits = 0;
    sa.prefix.bitfield = 0;

    BOOST_CHECK_EQUAL(sa.Encoded(),
        "smYnCBaa9GocCx4Rn9h4TSUEV9rvq3WniYRQswhh1egjGYc4nbzWhM38qjuotwbQhdDpxeNYVS6CUpEUYWJHj7JJZmCrNqeWu5mG2p");

    CStealthAddress sa2;
    BOOST_CHECK(sa2.SetEncoded(sa.Encoded()));
    BOOST_CHECK(sa2.scan_pubkey == sa.scan_pubkey);
    BOOST_CHECK(sa2.spend_pubkey == sa.spend_pubkey);
}

BOOST_AUTO_TEST_CASE(golden_stealth_serialization)
{
    CSecret scanSecret = MakeTestSecret(2);
    CSecret spendSecret = MakeTestSecret(3);
    CKey scanKey, spendKey;
    scanKey.SetSecret(scanSecret, true);
    spendKey.SetSecret(spendSecret, true);

    CStealthAddress sa;
    sa.options = 0;
    sa.scan_pubkey.resize(33);
    memcpy(&sa.scan_pubkey[0], &scanKey.GetPubKey().Raw()[0], 33);
    sa.spend_pubkey.resize(33);
    memcpy(&sa.spend_pubkey[0], &spendKey.GetPubKey().Raw()[0], 33);
    sa.label = "";
    sa.scan_secret.resize(32);
    memcpy(&sa.scan_secret[0], scanSecret.data(), 32);
    sa.spend_secret.resize(32);
    memcpy(&sa.spend_secret[0], spendSecret.data(), 32);

    std::string hex = SerializeToHex(sa);
    BOOST_CHECK_EQUAL(hex,
        "00"
        "21035e2351d8ef6779118df01fea79c87efa7767ad96accb581d604f97d230ff1775"
        "2103c5d242ce131681d7e85d9facbf814e0451252f2d92de785a3c2ce728cf2bc0bd"
        "00"
        "20b3ce02de479930f16941dcc5c31e49adb262fe84d47de8c04a0695f11a1e26a4"
        "20246db29fc749da38dc0b106f83bce81491e443d203f5e39a5f4e70b267b8f236"
    );

    // 1 + 1+33 + 1+33 + 1 + 1+32 + 1+32 = 136 bytes
    BOOST_CHECK_EQUAL(hex.size() / 2, 136u);

    CStealthAddress sa2 = DeserializeFromHex<CStealthAddress>(hex);
    BOOST_CHECK_EQUAL(sa2.options, 0);
    BOOST_CHECK(sa2.scan_pubkey == sa.scan_pubkey);
    BOOST_CHECK(sa2.spend_pubkey == sa.spend_pubkey);
    BOOST_CHECK(sa2.scan_secret == sa.scan_secret);
    BOOST_CHECK(sa2.spend_secret == sa.spend_secret);
}

BOOST_AUTO_TEST_CASE(golden_stealth_version_byte)
{
    // stealth_version_byte == 0x28 — verify via Base58 encoding
    CSecret scanSecret = MakeTestSecret(2);
    CSecret spendSecret = MakeTestSecret(3);
    CKey scanKey, spendKey;
    scanKey.SetSecret(scanSecret, true);
    spendKey.SetSecret(spendSecret, true);

    CStealthAddress sa;
    sa.options = 0;
    sa.scan_pubkey.resize(33);
    memcpy(&sa.scan_pubkey[0], &scanKey.GetPubKey().Raw()[0], 33);
    sa.spend_pubkey.resize(33);
    memcpy(&sa.spend_pubkey[0], &spendKey.GetPubKey().Raw()[0], 33);
    sa.number_signatures = 0;
    sa.prefix.number_bits = 0;
    sa.prefix.bitfield = 0;

    std::vector<unsigned char> raw;
    BOOST_CHECK(DecodeBase58(sa.Encoded(), raw));
    BOOST_CHECK_EQUAL(raw[0], 0x28);
}

// ============================================================================
// Section 6: Wallet Entry Serialization (7 tests)
// Highest-risk area for wallet corruption.
// ============================================================================

BOOST_AUTO_TEST_CASE(golden_ckeypool)
{
    CSecret secret = MakeTestSecret(4);
    CKey key;
    key.SetSecret(secret, true);

    CKeyPool pool;
    pool.nTime = 1700000000;
    pool.vchPubKey = key.GetPubKey();

    std::string hex = SerializeToHex(pool);
    BOOST_CHECK_EQUAL(hex,
        "c0201f00"
        "00f1536500000000"
        "210290178a9a6f85af30a16600d27c030b5286b3a138a15f0cd34421fb2b45ee984d"
    );
    BOOST_CHECK_EQUAL(hex.size() / 2, 46u);

    CKeyPool pool2 = DeserializeFromHex<CKeyPool>(hex);
    BOOST_CHECK_EQUAL(pool2.nTime, 1700000000);
    BOOST_CHECK(pool2.vchPubKey == pool.vchPubKey);
}

BOOST_AUTO_TEST_CASE(golden_ckeymetadata)
{
    CKeyMetadata meta(1700000000);

    std::string hex = SerializeToHex(meta);
    BOOST_CHECK_EQUAL(hex, "0100000000f1536500000000");
    BOOST_CHECK_EQUAL(hex.size() / 2, 12u);

    CKeyMetadata meta2 = DeserializeFromHex<CKeyMetadata>(hex);
    BOOST_CHECK_EQUAL(meta2.nVersion, 1);
    BOOST_CHECK_EQUAL(meta2.nCreateTime, 1700000000);
}

BOOST_AUTO_TEST_CASE(golden_cmasterkey_sha512)
{
    CMasterKey mk(0);
    mk.vchCryptedKey.assign(32, 0xaa);
    mk.vchSalt.assign(8, 0xbb);

    std::string hex = SerializeToHex(mk);
    BOOST_CHECK_EQUAL(hex,
        "20"
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
        "08"
        "bbbbbbbbbbbbbbbb"
        "00000000"
        "a8610000"
        "00"
    );
    BOOST_CHECK_EQUAL(hex.size() / 2, 51u);

    CMasterKey mk2 = DeserializeFromHex<CMasterKey>(hex);
    BOOST_CHECK_EQUAL(mk2.nDerivationMethod, 0u);
    BOOST_CHECK_EQUAL(mk2.nDeriveIterations, 25000u);
    BOOST_CHECK(mk2.vchCryptedKey == mk.vchCryptedKey);
    BOOST_CHECK(mk2.vchSalt == mk.vchSalt);
}

BOOST_AUTO_TEST_CASE(golden_cmasterkey_scrypt)
{
    CMasterKey mk(1);
    mk.vchCryptedKey.assign(32, 0xcc);
    mk.vchSalt.assign(8, 0xdd);

    std::string hex = SerializeToHex(mk);
    BOOST_CHECK_EQUAL(hex,
        "20"
        "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc"
        "08"
        "dddddddddddddddd"
        "01000000"
        "10270000"
        "00"
    );
    BOOST_CHECK_EQUAL(hex.size() / 2, 51u);

    CMasterKey mk2 = DeserializeFromHex<CMasterKey>(hex);
    BOOST_CHECK_EQUAL(mk2.nDerivationMethod, 1u);
    BOOST_CHECK_EQUAL(mk2.nDeriveIterations, 10000u);
}

BOOST_AUTO_TEST_CASE(golden_caccount)
{
    CSecret secret = MakeTestSecret(5);
    CKey key;
    key.SetSecret(secret, true);
    CAccount acct;
    acct.vchPubKey = key.GetPubKey();

    std::string hex = SerializeToHex(acct);
    BOOST_CHECK_EQUAL(hex,
        "c0201f00"
        "21038a59447097783a86db4c955d3f45a658ac115096bf5bfe5fd0de4b485522bc8c"
    );
    BOOST_CHECK_EQUAL(hex.size() / 2, 38u);

    CAccount acct2 = DeserializeFromHex<CAccount>(hex);
    BOOST_CHECK(acct2.vchPubKey == acct.vchPubKey);
}

BOOST_AUTO_TEST_CASE(golden_cwallettx)
{
    CWalletTx wtx;
    wtx.nVersion = 1;
    wtx.nTime = 1700000000;
    wtx.vin.resize(1);
    wtx.vin[0].prevout.SetNull();
    wtx.vin[0].scriptSig = CScript() << 1;
    wtx.vout.resize(1);
    wtx.vout[0].nValue = 50 * COIN;
    wtx.vout[0].scriptPubKey = CScript() << OP_TRUE;
    wtx.nLockTime = 0;
    wtx.hashBlock = uint256("0x4444444444444444444444444444444444444444444444444444444444444444");
    wtx.vMerkleBranch.clear();
    wtx.nIndex = 0;
    wtx.fTimeReceivedIsTxTime = 1;
    wtx.nTimeReceived = 1700000001;
    wtx.fFromMe = 1;

    std::string hex = SerializeToHex(wtx);
    BOOST_CHECK_EQUAL(hex,
        "0100000000f15365010000000000000000000000000000000000000000000000"
        "000000000000000000ffffffff0151ffffffff0100f2052a01000000015100000000"
        "4444444444444444444444444444444444444444444444444444444444444444"
        "0000000000"
        "0002"
        "0b" "66726f6d6163636f756e74" "00"
        "05" "7370656e74" "00"
        "0001000000"
        "01f15365"
        "01"
        "00"
    );

    CWalletTx wtx2 = DeserializeFromHex<CWalletTx>(hex);
    BOOST_CHECK_EQUAL(wtx2.nVersion, 1);
    BOOST_CHECK_EQUAL(wtx2.nTime, 1700000000u);
    BOOST_CHECK(wtx2.hashBlock == wtx.hashBlock);
    BOOST_CHECK_EQUAL(wtx2.nIndex, 0);
    BOOST_CHECK_EQUAL(wtx2.fTimeReceivedIsTxTime, 1u);
    BOOST_CHECK_EQUAL(wtx2.nTimeReceived, 1700000001u);
    BOOST_CHECK_EQUAL(wtx2.fFromMe, 1);
    BOOST_CHECK_EQUAL(wtx2.vout[0].nValue, 50 * COIN);
}

BOOST_AUTO_TEST_CASE(golden_cstealthkeymeta)
{
    CSecret ephemSecret = MakeTestSecret(6);
    CSecret scanSecret = MakeTestSecret(7);
    CKey ephemKey, scanKey;
    ephemKey.SetSecret(ephemSecret, true);
    scanKey.SetSecret(scanSecret, true);

    CStealthKeyMetadata skm(ephemKey.GetPubKey(), scanKey.GetPubKey());

    std::string hex = SerializeToHex(skm);
    BOOST_CHECK_EQUAL(hex,
        "2102c7393391a4b49683df62f20de0a2369ad0fca000933a4e23fe7790c13c475075"
        "2103b04992c0cf21074a692655c555c41a215af34504036120e7aebd99a7c76fcd37"
    );
    BOOST_CHECK_EQUAL(hex.size() / 2, 68u);

    CStealthKeyMetadata skm2 = DeserializeFromHex<CStealthKeyMetadata>(hex);
    BOOST_CHECK(skm2.pkEphem == skm.pkEphem);
    BOOST_CHECK(skm2.pkScan == skm.pkScan);
}

// ============================================================================
// Section 7: Network Protocol (5 tests)
// ============================================================================

BOOST_AUTO_TEST_CASE(golden_magic_bytes)
{
    unsigned char mainnetMagic[4] = {0xf2, 0xf4, 0xf9, 0xfb};
    unsigned char testnetMagic[4] = {0x02, 0x04, 0x05, 0x0d};

    BOOST_CHECK_EQUAL(HexStr(mainnetMagic, mainnetMagic + 4), "f2f4f9fb");
    BOOST_CHECK_EQUAL(HexStr(testnetMagic, testnetMagic + 4), "0204050d");
}

BOOST_AUTO_TEST_CASE(golden_cmessageheader)
{
    CMessageHeader hdr;
    memcpy(hdr.pchMessageStart, "\xf2\xf4\xf9\xfb", 4);
    memset(hdr.pchCommand, 0, 12);
    memcpy(hdr.pchCommand, "version", 7);
    hdr.nMessageSize = 100;
    hdr.nChecksum = 0xdeadbeef;

    std::string hex = SerializeToHex(hdr);
    BOOST_CHECK_EQUAL(hex,
        "f2f4f9fb"
        "76657273696f6e0000000000"
        "64000000"
        "efbeadde"
    );
    BOOST_CHECK_EQUAL(hex.size() / 2, 24u);
}

BOOST_AUTO_TEST_CASE(golden_caddress)
{
    CAddress addr;
    addr.nServices = 1;
    addr.nTime = 1700000000;
    struct in_addr ipv4;
    ipv4.s_addr = htonl(0xC0A80101);
    *(CService*)&addr = CService(ipv4, 9134);

    CDataStream ss(SER_DISK, CLIENT_VERSION);
    ss << addr;

    // SER_DISK: nVersion(4) + nTime(4) + nServices(8) + ip(16) + port(2) = 34
    BOOST_CHECK_EQUAL(ss.size(), 34u);

    std::string hex = HexStr(ss.begin(), ss.end());
    CDataStream ss2(ParseHex(hex), SER_DISK, CLIENT_VERSION);
    CAddress addr2;
    ss2 >> addr2;
    BOOST_CHECK_EQUAL(addr2.nTime, 1700000000u);
    BOOST_CHECK_EQUAL(addr2.nServices, 1u);
}

BOOST_AUTO_TEST_CASE(golden_cinv)
{
    CInv inv(1, uint256("0x5555555555555555555555555555555555555555555555555555555555555555"));

    std::string hex = SerializeToHex(inv);
    BOOST_CHECK_EQUAL(hex,
        "01000000"
        "5555555555555555555555555555555555555555555555555555555555555555"
    );
    BOOST_CHECK_EQUAL(hex.size() / 2, 36u);

    CInv inv2 = DeserializeFromHex<CInv>(hex);
    BOOST_CHECK_EQUAL(inv2.type, 1);
    BOOST_CHECK(inv2.hash == inv.hash);
}

BOOST_AUTO_TEST_CASE(golden_cblocklocator)
{
    std::vector<uint256> vHave;
    vHave.push_back(uint256("0xaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"));
    vHave.push_back(hashGenesisBlock);
    CBlockLocator locator(vHave);

    std::string hex = SerializeToHex(locator);
    BOOST_CHECK_EQUAL(hex,
        "c0201f00"
        "02"
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
        "89cc56b99163baa687707c59e2c4643683b8c990d0c4654644e600b7790f0000"
    );
    BOOST_CHECK_EQUAL(hex.size() / 2, 69u);
}

// ============================================================================
// Section 8: Disk Index Formats (5 tests)
// ============================================================================

BOOST_AUTO_TEST_CASE(golden_cdisktxpos)
{
    CDiskTxPos pos(1, 1000, 2000);

    std::string hex = SerializeToHex(pos);
    BOOST_CHECK_EQUAL(hex, "01000000" "e8030000" "d0070000");
    BOOST_CHECK_EQUAL(hex.size() / 2, 12u);

    CDiskTxPos pos2 = DeserializeFromHex<CDiskTxPos>(hex);
    BOOST_CHECK_EQUAL(pos2.nFile, 1u);
    BOOST_CHECK_EQUAL(pos2.nBlockPos, 1000u);
    BOOST_CHECK_EQUAL(pos2.nTxPos, 2000u);
}

BOOST_AUTO_TEST_CASE(golden_ctxindex)
{
    CDiskTxPos pos(1, 500, 600);
    CTxIndex txidx(pos, 2);

    std::string hex = SerializeToHex(txidx);
    BOOST_CHECK_EQUAL(hex,
        "c0201f00"
        "01000000" "f4010000" "58020000"
        "02"
        "ffffffff" "00000000" "00000000"
        "ffffffff" "00000000" "00000000"
    );
    BOOST_CHECK_EQUAL(hex.size() / 2, 41u);

    CTxIndex txidx2 = DeserializeFromHex<CTxIndex>(hex);
    BOOST_CHECK(txidx2.pos == pos);
    BOOST_CHECK_EQUAL(txidx2.vSpent.size(), 2u);
}

BOOST_AUTO_TEST_CASE(golden_cdiskblockindex_pow)
{
    CDiskBlockIndex dbi;
    dbi.hashNext = uint256("0x1111111111111111111111111111111111111111111111111111111111111111");
    dbi.nFile = 1;
    dbi.nBlockPos = 1000;
    dbi.nHeight = 100;
    dbi.nMint = 100 * COIN;
    dbi.nMoneySupply = 50000 * COIN;
    dbi.nFlags = 0;
    dbi.nStakeModifier = 0;
    dbi.hashProof = uint256("0x3333333333333333333333333333333333333333333333333333333333333333");
    dbi.nVersion = 1;
    dbi.hashPrev = uint256("0x4444444444444444444444444444444444444444444444444444444444444444");
    dbi.hashMerkleRoot = uint256("0x5555555555555555555555555555555555555555555555555555555555555555");
    dbi.nTime = 1700000000;
    dbi.nBits = 0x1e0fffff;
    dbi.nNonce = 42;

    std::string hex = SerializeToHex(dbi);
    BOOST_CHECK_EQUAL(hex,
        "c0201f00"
        "1111111111111111111111111111111111111111111111111111111111111111"
        "01000000" "e8030000" "64000000"
        "00e40b5402000000"
        "005039278c04000000000000"
        "0000000000000000"
        "3333333333333333333333333333333333333333333333333333333333333333"
        "01000000"
        "4444444444444444444444444444444444444444444444444444444444444444"
        "5555555555555555555555555555555555555555555555555555555555555555"
        "00f15365" "ffff0f1e" "2a000000"
        "0000000000000000000000000000000000000000000000000000000000000000"
    );

    CDiskBlockIndex dbi2 = DeserializeFromHex<CDiskBlockIndex>(hex);
    BOOST_CHECK_EQUAL(dbi2.nHeight, 100);
    BOOST_CHECK_EQUAL(dbi2.nFlags, 0u);
    BOOST_CHECK(dbi2.IsProofOfWork());
    BOOST_CHECK(dbi2.hashNext == dbi.hashNext);
    BOOST_CHECK(dbi2.hashProof == dbi.hashProof);
    BOOST_CHECK_EQUAL(dbi2.nTime, 1700000000u);
}

BOOST_AUTO_TEST_CASE(golden_cdiskblockindex_pos)
{
    CDiskBlockIndex dbi;
    dbi.hashNext = uint256("0x1111111111111111111111111111111111111111111111111111111111111111");
    dbi.nFile = 1;
    dbi.nBlockPos = 1000;
    dbi.nHeight = 200;
    dbi.nMint = 5 * COIN;
    dbi.nMoneySupply = 100000 * COIN;
    dbi.nFlags = 1;
    dbi.nStakeModifier = 0x123456789abcdef0ULL;
    dbi.prevoutStake = COutPoint(uint256("0x6666666666666666666666666666666666666666666666666666666666666666"), 0);
    dbi.nStakeTime = 1700000000;
    dbi.hashProof = uint256("0x7777777777777777777777777777777777777777777777777777777777777777");
    dbi.nVersion = 1;
    dbi.hashPrev = uint256("0x8888888888888888888888888888888888888888888888888888888888888888");
    dbi.hashMerkleRoot = uint256("0x9999999999999999999999999999999999999999999999999999999999999999");
    dbi.nTime = 1700000000;
    dbi.nBits = 0x1e0fffff;
    dbi.nNonce = 0;

    std::string hex = SerializeToHex(dbi);
    BOOST_CHECK_EQUAL(hex,
        "c0201f00"
        "1111111111111111111111111111111111111111111111111111111111111111"
        "01000000" "e8030000" "c8000000"
        "0065cd1d00000000"
        "00a0724e18090000"
        "01000000"
        "f0debc9a78563412"
        "6666666666666666666666666666666666666666666666666666666666666666"
        "00000000"
        "00f15365"
        "7777777777777777777777777777777777777777777777777777777777777777"
        "01000000"
        "8888888888888888888888888888888888888888888888888888888888888888"
        "9999999999999999999999999999999999999999999999999999999999999999"
        "00f15365" "ffff0f1e" "00000000"
        "0000000000000000000000000000000000000000000000000000000000000000"
    );

    CDiskBlockIndex dbi2 = DeserializeFromHex<CDiskBlockIndex>(hex);
    BOOST_CHECK_EQUAL(dbi2.nHeight, 200);
    BOOST_CHECK(dbi2.IsProofOfStake());
    BOOST_CHECK(dbi2.prevoutStake.hash == dbi.prevoutStake.hash);
    BOOST_CHECK_EQUAL(dbi2.nStakeTime, 1700000000u);
    BOOST_CHECK_EQUAL(dbi2.nStakeModifier, 0x123456789abcdef0ULL);
}

BOOST_AUTO_TEST_CASE(golden_cdiskblockindex_size_diff)
{
    CDiskBlockIndex powBlock;
    powBlock.hashNext = uint256("0x1111111111111111111111111111111111111111111111111111111111111111");
    powBlock.nFile = 1; powBlock.nBlockPos = 1000; powBlock.nHeight = 100;
    powBlock.nMint = 100 * COIN; powBlock.nMoneySupply = 50000 * COIN;
    powBlock.nFlags = 0; powBlock.nStakeModifier = 0;
    powBlock.hashProof = uint256("0x3333333333333333333333333333333333333333333333333333333333333333");
    powBlock.nVersion = 1;
    powBlock.hashPrev = uint256("0x4444444444444444444444444444444444444444444444444444444444444444");
    powBlock.hashMerkleRoot = uint256("0x5555555555555555555555555555555555555555555555555555555555555555");
    powBlock.nTime = 1700000000; powBlock.nBits = 0x1e0fffff; powBlock.nNonce = 42;

    CDiskBlockIndex posBlock;
    posBlock.hashNext = uint256("0x1111111111111111111111111111111111111111111111111111111111111111");
    posBlock.nFile = 1; posBlock.nBlockPos = 1000; posBlock.nHeight = 100;
    posBlock.nMint = 100 * COIN; posBlock.nMoneySupply = 50000 * COIN;
    posBlock.nFlags = 1; posBlock.nStakeModifier = 0;
    posBlock.prevoutStake = COutPoint(uint256("0x6666666666666666666666666666666666666666666666666666666666666666"), 0);
    posBlock.nStakeTime = 1700000000;
    posBlock.hashProof = uint256("0x3333333333333333333333333333333333333333333333333333333333333333");
    posBlock.nVersion = 1;
    posBlock.hashPrev = uint256("0x4444444444444444444444444444444444444444444444444444444444444444");
    posBlock.hashMerkleRoot = uint256("0x5555555555555555555555555555555555555555555555555555555555555555");
    posBlock.nTime = 1700000000; posBlock.nBits = 0x1e0fffff; posBlock.nNonce = 42;

    CDataStream ssPow(SER_DISK, CLIENT_VERSION);
    ssPow << powBlock;
    CDataStream ssPos(SER_DISK, CLIENT_VERSION);
    ssPos << posBlock;

    // PoS is exactly 40 bytes larger: COutPoint(36) + nStakeTime(4)
    BOOST_CHECK_EQUAL(ssPos.size() - ssPow.size(), 40u);
}

// ============================================================================
// Section 9: Genesis Block Pin (4 tests)
// The ultimate backward-compatibility canary.
// ============================================================================

BOOST_AUTO_TEST_CASE(golden_genesis_coinbase_hex)
{
    CBlock genesis = CreateGenesisBlock();
    std::string hex = SerializeToHex(genesis.vtx[0]);

    BOOST_CHECK_EQUAL(hex,
        "0100000085989758010000000000000000000000000000000000000000000000"
        "000000000000000000ffffffff6d00012a4c684120626c61636b20686f6c6520"
        "6973206120737461626c6520656e6572677920636f6e737472756374207468"
        "6174206f636375706965732067726561746572207468616e20746872656520"
        "64696d656e73696f6e73206f6620706879736963616c2073706163652effff"
        "ffff0100000000000000000000000000"
    );

    CTransaction cb2 = DeserializeFromHex<CTransaction>(hex);
    BOOST_CHECK(cb2.GetHash() == genesis.vtx[0].GetHash());
    BOOST_CHECK_EQUAL(cb2.nTime, 1486329989u);
    BOOST_CHECK(cb2.IsCoinBase());
    BOOST_CHECK(cb2.vout[0].IsEmpty());
}

BOOST_AUTO_TEST_CASE(golden_genesis_block_hex)
{
    CBlock genesis = CreateGenesisBlock();
    std::string hex = SerializeToHex(genesis);

    // Verify immutability: re-serialization matches
    CBlock genesis2 = DeserializeFromHex<CBlock>(hex);
    BOOST_CHECK_EQUAL(SerializeToHex(genesis2), hex);

    // Pin size: header(80) + varint(1) + coinbase + varint(0)
    CDataStream ssTx(SER_DISK, CLIENT_VERSION);
    ssTx << genesis.vtx[0];
    BOOST_CHECK_EQUAL(hex.size() / 2, 80u + 1u + ssTx.size() + 1u);
}

BOOST_AUTO_TEST_CASE(golden_genesis_hash_verified)
{
    CBlock genesis = CreateGenesisBlock();
    BOOST_CHECK(genesis.GetHash() == hashGenesisBlock);

    std::string hex = SerializeToHex(genesis);
    CBlock block = DeserializeFromHex<CBlock>(hex);
    BOOST_CHECK(block.GetHash() == hashGenesisBlock);
    BOOST_CHECK_EQUAL(block.GetHash().GetHex(),
        "00000f79b700e6444665c4d090c9b8833664c4e2597c7087a6ba6391b956cc89");
}

BOOST_AUTO_TEST_CASE(golden_genesis_merkle_verified)
{
    CBlock genesis = CreateGenesisBlock();
    std::string hex = SerializeToHex(genesis);
    CBlock block = DeserializeFromHex<CBlock>(hex);
    uint256 merkle = block.BuildMerkleTree();

    BOOST_CHECK_EQUAL(merkle.GetHex(),
        "96f872319c330aadbdc18543e27a305c6ab046801cfc81e20a004f3b26fad891");
    BOOST_CHECK(merkle == genesis.hashMerkleRoot);
}

// ============================================================================
// Section 10: Pre-Serialization-Migration Pins
// ============================================================================

BOOST_AUTO_TEST_CASE(golden_cmerkletx)
{
    // CMerkleTx = CTransaction + hashBlock + vMerkleBranch + nIndex
    CMerkleTx mtx;
    mtx.nVersion = 1;
    mtx.nTime = 1000;
    mtx.nLockTime = 0;
    mtx.hashBlock = uint256("0x00000000000000001");
    mtx.nIndex = 5;
    // Leave vMerkleBranch empty, vin/vout empty

    std::string hex = SerializeToHex(mtx);
    CMerkleTx mtx2 = DeserializeFromHex<CMerkleTx>(hex);
    BOOST_CHECK_EQUAL(mtx2.hashBlock.GetHex(), mtx.hashBlock.GetHex());
    BOOST_CHECK_EQUAL(mtx2.nIndex, 5);
    BOOST_CHECK_EQUAL(mtx2.nTime, 1000u);

    // Pin the size: CTransaction(14) + hashBlock(32) + vMerkleBranch count(1) + nIndex(4) = 51
    BOOST_CHECK_EQUAL(hex.size() / 2, 51u);
}

BOOST_AUTO_TEST_CASE(golden_cunsignedalert)
{
    CUnsignedAlert alert;
    alert.nVersion = 1;
    alert.nRelayUntil = 1000000;
    alert.nExpiration = 2000000;
    alert.nID = 42;
    alert.nCancel = 0;
    alert.nMinVer = 60016;
    alert.nMaxVer = 60019;
    alert.nPriority = 100;
    alert.strComment = "test";
    alert.strStatusBar = "alert!";
    alert.strReserved = "";

    std::string hex = SerializeToHex(alert);
    CUnsignedAlert alert2 = DeserializeFromHex<CUnsignedAlert>(hex);
    BOOST_CHECK_EQUAL(alert2.nID, 42);
    BOOST_CHECK_EQUAL(alert2.nMinVer, 60016);
    BOOST_CHECK_EQUAL(alert2.nMaxVer, 60019);
    BOOST_CHECK_EQUAL(alert2.strStatusBar, "alert!");

    // Round-trip integrity
    std::string hex2 = SerializeToHex(alert2);
    BOOST_CHECK_EQUAL(hex, hex2);
}

BOOST_AUTO_TEST_CASE(golden_calert)
{
    CAlert alert;
    alert.vchMsg = {0x01, 0x02, 0x03};
    alert.vchSig = {0xAA, 0xBB};

    std::string hex = SerializeToHex(alert);
    CAlert alert2 = DeserializeFromHex<CAlert>(hex);
    BOOST_CHECK(alert2.vchMsg == alert.vchMsg);
    BOOST_CHECK(alert2.vchSig == alert.vchSig);
}

BOOST_AUTO_TEST_CASE(golden_cunsignedsynccheckpoint)
{
    CUnsignedSyncCheckpoint cp;
    cp.nVersion = 1;
    cp.hashCheckpoint = uint256("0x00000000000000abc");

    std::string hex = SerializeToHex(cp);
    CUnsignedSyncCheckpoint cp2 = DeserializeFromHex<CUnsignedSyncCheckpoint>(hex);
    BOOST_CHECK_EQUAL(cp2.nVersion, 1);
    BOOST_CHECK_EQUAL(cp2.hashCheckpoint.GetHex(), cp.hashCheckpoint.GetHex());
}

BOOST_AUTO_TEST_CASE(golden_csynccheckpoint)
{
    CSyncCheckpoint cp;
    cp.vchMsg = {0xDE, 0xAD};
    cp.vchSig = {0xBE, 0xEF};

    std::string hex = SerializeToHex(cp);
    CSyncCheckpoint cp2 = DeserializeFromHex<CSyncCheckpoint>(hex);
    BOOST_CHECK(cp2.vchMsg == cp.vchMsg);
    BOOST_CHECK(cp2.vchSig == cp.vchSig);
}

BOOST_AUTO_TEST_SUITE_END()
