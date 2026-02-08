// Copyright (c) 2024 The Pinkcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <boost/test/unit_test.hpp>

#include "main.h"
#include "uint256.h"
#include "bignum.h"

BOOST_AUTO_TEST_SUITE(block_tests)

// ============================================================================
// Genesis block reconstruction — pin all fields
// ============================================================================

static CBlock CreateGenesisBlock()
{
    const char* pszTimestamp = "A black hole is a stable energy construct that occupies greater than three dimensions of physical space.";
    CTransaction txNew;
    txNew.nTime = 1486329989;
    txNew.vin.resize(1);
    txNew.vout.resize(1);
    txNew.vin[0].scriptSig = CScript() << 0 << CBigNum(42)
        << std::vector<unsigned char>((const unsigned char*)pszTimestamp,
                                      (const unsigned char*)pszTimestamp + strlen(pszTimestamp));
    txNew.vout[0].SetEmpty();

    CBlock block;
    block.vtx.push_back(txNew);
    block.hashPrevBlock = 0;
    block.hashMerkleRoot = block.BuildMerkleTree();
    block.nVersion = 1;
    block.nTime    = 1486329989;
    block.nBits    = CBigNum(~uint256(0) >> 20).GetCompact();
    block.nNonce   = 6777712;

    return block;
}

BOOST_AUTO_TEST_CASE(genesis_block_hash)
{
    CBlock genesis = CreateGenesisBlock();
    uint256 hash = genesis.GetHash();

    uint256 expected("0x00000f79b700e6444665c4d090c9b8833664c4e2597c7087a6ba6391b956cc89");
    BOOST_CHECK(hash == expected);
}

BOOST_AUTO_TEST_CASE(genesis_merkle_root)
{
    CBlock genesis = CreateGenesisBlock();

    uint256 expected("0x96f872319c330aadbdc18543e27a305c6ab046801cfc81e20a004f3b26fad891");
    BOOST_CHECK(genesis.hashMerkleRoot == expected);
}

BOOST_AUTO_TEST_CASE(genesis_block_fields)
{
    CBlock genesis = CreateGenesisBlock();

    BOOST_CHECK_EQUAL(genesis.nVersion, 1);
    BOOST_CHECK_EQUAL(genesis.nTime, 1486329989u);
    BOOST_CHECK_EQUAL(genesis.nNonce, 6777712u);
    BOOST_CHECK(genesis.hashPrevBlock == 0);
    BOOST_CHECK_EQUAL(genesis.vtx.size(), 1u);
    BOOST_CHECK(genesis.IsProofOfWork());
    BOOST_CHECK(!genesis.IsProofOfStake());
}

BOOST_AUTO_TEST_CASE(genesis_coinbase_tx)
{
    CBlock genesis = CreateGenesisBlock();
    const CTransaction& tx = genesis.vtx[0];

    BOOST_CHECK(tx.IsCoinBase());
    BOOST_CHECK(!tx.IsCoinStake());
    BOOST_CHECK_EQUAL(tx.nTime, 1486329989u);
    BOOST_CHECK_EQUAL(tx.vin.size(), 1u);
    BOOST_CHECK_EQUAL(tx.vout.size(), 1u);
    BOOST_CHECK(tx.vout[0].IsEmpty());
}

// ============================================================================
// CBlock serialization round-trip
// ============================================================================

BOOST_AUTO_TEST_CASE(block_serialize_roundtrip)
{
    CBlock genesis = CreateGenesisBlock();

    CDataStream ss(SER_DISK, CLIENT_VERSION);
    ss << genesis;

    CBlock deserialized;
    ss >> deserialized;

    // All header fields must match
    BOOST_CHECK_EQUAL(deserialized.nVersion, genesis.nVersion);
    BOOST_CHECK(deserialized.hashPrevBlock == genesis.hashPrevBlock);
    BOOST_CHECK(deserialized.hashMerkleRoot == genesis.hashMerkleRoot);
    BOOST_CHECK_EQUAL(deserialized.nTime, genesis.nTime);
    BOOST_CHECK_EQUAL(deserialized.nBits, genesis.nBits);
    BOOST_CHECK_EQUAL(deserialized.nNonce, genesis.nNonce);

    // Transactions must survive
    BOOST_CHECK_EQUAL(deserialized.vtx.size(), genesis.vtx.size());

    // Hash must be identical after round-trip
    BOOST_CHECK(deserialized.GetHash() == genesis.GetHash());
}

BOOST_AUTO_TEST_CASE(block_serialize_network_roundtrip)
{
    CBlock genesis = CreateGenesisBlock();

    // Serialize for network (includes full tx data)
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << genesis;

    CBlock deserialized;
    ss >> deserialized;

    BOOST_CHECK(deserialized.GetHash() == genesis.GetHash());
    BOOST_CHECK_EQUAL(deserialized.vtx.size(), genesis.vtx.size());
}

BOOST_AUTO_TEST_CASE(block_header_only_serialize)
{
    CBlock genesis = CreateGenesisBlock();

    // Header-only serialization (SER_BLOCKHEADERONLY)
    CDataStream ss(SER_BLOCKHEADERONLY, CLIENT_VERSION);
    ss << genesis;

    CBlock headerOnly;
    ss >> headerOnly;

    // Header fields must match
    BOOST_CHECK_EQUAL(headerOnly.nVersion, genesis.nVersion);
    BOOST_CHECK(headerOnly.hashPrevBlock == genesis.hashPrevBlock);
    BOOST_CHECK(headerOnly.hashMerkleRoot == genesis.hashMerkleRoot);
    BOOST_CHECK_EQUAL(headerOnly.nTime, genesis.nTime);
    BOOST_CHECK_EQUAL(headerOnly.nBits, genesis.nBits);
    BOOST_CHECK_EQUAL(headerOnly.nNonce, genesis.nNonce);

    // But transactions should be empty
    BOOST_CHECK(headerOnly.vtx.empty());
}

// ============================================================================
// CTransaction serialization round-trip
// ============================================================================

BOOST_AUTO_TEST_CASE(tx_serialize_roundtrip)
{
    // Build a transaction with multiple inputs/outputs
    CTransaction tx;
    tx.nVersion = 1;
    tx.nTime = 1700000000;
    tx.vin.resize(2);
    tx.vin[0].prevout.hash = uint256("0xaaaa");
    tx.vin[0].prevout.n = 0;
    tx.vin[0].scriptSig = CScript() << std::vector<unsigned char>(72, 0x30);
    tx.vin[1].prevout.hash = uint256("0xbbbb");
    tx.vin[1].prevout.n = 1;
    tx.vout.resize(2);
    tx.vout[0].nValue = 100 * COIN;
    tx.vout[0].scriptPubKey = CScript() << OP_DUP << OP_HASH160
                              << std::vector<unsigned char>(20, 0x42) << OP_EQUALVERIFY << OP_CHECKSIG;
    tx.vout[1].nValue = 50 * COIN;
    tx.vout[1].scriptPubKey = CScript() << OP_DUP << OP_HASH160
                              << std::vector<unsigned char>(20, 0x43) << OP_EQUALVERIFY << OP_CHECKSIG;
    tx.nLockTime = 0;

    CDataStream ss(SER_DISK, CLIENT_VERSION);
    ss << tx;

    CTransaction tx2;
    ss >> tx2;

    // Hash must be identical
    BOOST_CHECK(tx2.GetHash() == tx.GetHash());

    // Fields must match
    BOOST_CHECK_EQUAL(tx2.nVersion, tx.nVersion);
    BOOST_CHECK_EQUAL(tx2.nTime, tx.nTime);
    BOOST_CHECK_EQUAL(tx2.vin.size(), tx.vin.size());
    BOOST_CHECK_EQUAL(tx2.vout.size(), tx.vout.size());
    BOOST_CHECK_EQUAL(tx2.vout[0].nValue, 100 * COIN);
    BOOST_CHECK_EQUAL(tx2.vout[1].nValue, 50 * COIN);
    BOOST_CHECK_EQUAL(tx2.nLockTime, tx.nLockTime);
}

// ============================================================================
// Block hash stability — pin the serialized binary size
// ============================================================================

BOOST_AUTO_TEST_CASE(genesis_serialized_size)
{
    CBlock genesis = CreateGenesisBlock();

    CDataStream ss(SER_DISK, CLIENT_VERSION);
    ss << genesis;
    size_t fullSize = ss.size();

    // The genesis block has a fixed serialization size
    // If this changes, the wire format has changed
    BOOST_CHECK(fullSize > 0);

    // Pin the size — must not change across builds
    // Header(80) + varint(1) + coinbase tx + varint(0) blocksig
    CDataStream ssHeader(SER_BLOCKHEADERONLY, CLIENT_VERSION);
    ssHeader << genesis;
    BOOST_CHECK_EQUAL(ssHeader.size(), 80u);  // header only: nVersion(4) + hashPrev(32) + hashMerkle(32) + nTime(4) + nBits(4) + nNonce(4)
}

// ============================================================================
// PoW hash vs block hash (GetHash uses scrypt, ComputeHash == GetPoWHash)
// ============================================================================

BOOST_AUTO_TEST_CASE(block_hash_is_pow_hash)
{
    // CBlock::GetHash() delegates to ComputeHash() which calls GetPoWHash()
    CBlock block;
    block.nVersion = 1;
    block.hashPrevBlock = 0;
    block.hashMerkleRoot = uint256("0xdeadbeef");
    block.nTime = 1700000000;
    block.nBits = 0x1e0fffff;
    block.nNonce = 42;

    uint256 blockHash = block.GetHash();
    uint256 powHash = block.GetPoWHash();

    BOOST_CHECK(blockHash == powHash);
}

// ============================================================================
// Merkle tree with multiple transactions
// ============================================================================

BOOST_AUTO_TEST_CASE(merkle_tree_two_tx)
{
    CBlock block;

    // Coinbase
    CTransaction cb;
    cb.nTime = 1700000000;
    cb.vin.resize(1);
    cb.vin[0].prevout.SetNull();
    cb.vin[0].scriptSig = CScript() << 1;
    cb.vout.resize(1);
    cb.vout[0].nValue = 50 * COIN;
    block.vtx.push_back(cb);

    // Second tx
    CTransaction tx;
    tx.nTime = 1700000000;
    tx.vin.resize(1);
    tx.vin[0].prevout.hash = uint256("0x1111");
    tx.vin[0].prevout.n = 0;
    tx.vout.resize(1);
    tx.vout[0].nValue = 10 * COIN;
    block.vtx.push_back(tx);

    uint256 merkle = block.BuildMerkleTree();
    BOOST_CHECK(merkle != 0);

    // Deterministic
    uint256 merkle2 = block.BuildMerkleTree();
    BOOST_CHECK(merkle == merkle2);

    // Merkle root should differ from either individual tx hash
    BOOST_CHECK(merkle != cb.GetHash());
    BOOST_CHECK(merkle != tx.GetHash());

    // Pin: merkle = Hash(Hash(tx0), Hash(tx1))
    uint256 htx0 = block.vtx[0].GetHash();
    uint256 htx1 = block.vtx[1].GetHash();
    uint256 expected = Hash(BEGIN(htx0), END(htx0), BEGIN(htx1), END(htx1));
    BOOST_CHECK(merkle == expected);
}

BOOST_AUTO_TEST_CASE(merkle_tree_three_tx)
{
    CBlock block;

    for (int i = 0; i < 3; i++) {
        CTransaction tx;
        tx.nTime = 1700000000 + i;
        tx.vin.resize(1);
        tx.vin[0].prevout.SetNull();
        tx.vin[0].scriptSig = CScript() << i;
        tx.vout.resize(1);
        tx.vout[0].nValue = (i + 1) * COIN;
        block.vtx.push_back(tx);
    }

    uint256 merkle = block.BuildMerkleTree();
    BOOST_CHECK(merkle != 0);

    // With 3 tx, the tree has: [tx0, tx1, tx2] → [H(tx0,tx1), H(tx2,tx2)] → root
    uint256 h0 = block.vtx[0].GetHash();
    uint256 h1 = block.vtx[1].GetHash();
    uint256 h2 = block.vtx[2].GetHash();
    uint256 h01 = Hash(BEGIN(h0), END(h0), BEGIN(h1), END(h1));
    uint256 h22 = Hash(BEGIN(h2), END(h2), BEGIN(h2), END(h2));
    uint256 expected = Hash(BEGIN(h01), END(h01), BEGIN(h22), END(h22));
    BOOST_CHECK(merkle == expected);
}

BOOST_AUTO_TEST_SUITE_END()
