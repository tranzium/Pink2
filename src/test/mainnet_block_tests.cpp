// Copyright (c) 2024 The Pinkcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
//
// Golden reference tests using real Pinkcoin mainnet blocks.
// Source of truth: https://chainz.cryptoid.info/pink/
//
// These fixtures pin the exact behavior of block header hashing (scrypt),
// merkle tree construction (SHA256d), and block type identification.
// Any migration (OpenSSL, Boost, etc.) that changes these outputs will
// be caught immediately.

#include <boost/test/unit_test.hpp>

#include "json/json_spirit_reader_template.h"
#include "json/json_spirit_utils.h"

#include "main.h"
#include "kernel.h"
#include "checkpoints.h"

#include <string>
#include <vector>
#include <cstdlib>

using namespace json_spirit;
extern Array read_json(const std::string& filename);

// Compute merkle root from a vector of tx hashes using the same algorithm
// as CBlock::BuildMerkleTree(), without needing full CTransaction objects.
static uint256 ComputeMerkleRootFromHashes(const std::vector<uint256>& leaves)
{
    if (leaves.empty()) return uint256(0);

    std::vector<uint256> tree = leaves;
    while (tree.size() > 1)
    {
        std::vector<uint256> next;
        for (size_t i = 0; i < tree.size(); i += 2)
        {
            size_t i2 = std::min(i + 1, tree.size() - 1);
            next.push_back(Hash(CharCast(tree[i]),  CharEnd(tree[i]),
                                CharCast(tree[i2]), CharEnd(tree[i2])));
        }
        tree = next;
    }
    return tree[0];
}

// Helper: construct a CBlock header from JSON object fields
static CBlock BlockFromJson(const Object& obj)
{
    CBlock blk;
    blk.nVersion       = find_value(obj, "version").get_int();
    blk.hashPrevBlock  = uint256("0x" + find_value(obj, "previousblockhash").get_str());
    blk.hashMerkleRoot = uint256("0x" + find_value(obj, "merkleroot").get_str());
    blk.nTime          = (unsigned int)find_value(obj, "time").get_int64();

    std::string bitsHex = find_value(obj, "bits").get_str();
    blk.nBits = (unsigned int)strtoul(bitsHex.c_str(), nullptr, 16);

    blk.nNonce = (unsigned int)find_value(obj, "nonce").get_int64();
    return blk;
}

BOOST_AUTO_TEST_SUITE(mainnet_block_tests)

// ============================================================================
// Block header hash verification — pins scrypt hash of real mainnet headers
// ============================================================================

BOOST_AUTO_TEST_CASE(mainnet_block_header_hashes)
{
    Array blocks = read_json("mainnet_blocks.json");
    BOOST_REQUIRE(blocks.size() >= 4);

    for (const Value& bv : blocks)
    {
        Object obj = bv.get_obj();
        std::string desc = find_value(obj, "description").get_str();
        uint256 expectedHash("0x" + find_value(obj, "hash").get_str());

        CBlock blk = BlockFromJson(obj);
        uint256 computedHash = blk.GetHash();

        BOOST_CHECK_MESSAGE(computedHash == expectedHash,
            "Block hash mismatch for " << desc
            << ": expected " << expectedHash.ToString()
            << " got " << computedHash.ToString());
    }
}

// ============================================================================
// Merkle root verification — pins SHA256d merkle tree computation
// ============================================================================

BOOST_AUTO_TEST_CASE(mainnet_merkle_roots)
{
    Array blocks = read_json("mainnet_blocks.json");

    for (const Value& bv : blocks)
    {
        Object obj = bv.get_obj();
        std::string desc = find_value(obj, "description").get_str();
        uint256 expectedMerkle("0x" + find_value(obj, "merkleroot").get_str());

        Array txArr = find_value(obj, "tx").get_array();
        std::vector<uint256> txHashes;
        for (const Value& tv : txArr)
            txHashes.push_back(uint256("0x" + tv.get_str()));

        uint256 computedMerkle = ComputeMerkleRootFromHashes(txHashes);

        BOOST_CHECK_MESSAGE(computedMerkle == expectedMerkle,
            "Merkle root mismatch for " << desc
            << ": expected " << expectedMerkle.ToString()
            << " got " << computedMerkle.ToString());
    }
}

// ============================================================================
// Block type identification — PoW vs PoS from header fields
// ============================================================================

BOOST_AUTO_TEST_CASE(mainnet_pow_block_identification)
{
    Array blocks = read_json("mainnet_blocks.json");

    for (const Value& bv : blocks)
    {
        Object obj = bv.get_obj();
        std::string flags = find_value(obj, "flags").get_str();
        unsigned int nonce = (unsigned int)find_value(obj, "nonce").get_int64();
        Array txArr = find_value(obj, "tx").get_array();

        if (flags.find("proof-of-work") != std::string::npos)
        {
            // PoW blocks have nonzero nonce and single coinbase tx
            BOOST_CHECK_MESSAGE(nonce > 0,
                "PoW block should have nonzero nonce: " +
                find_value(obj, "description").get_str());
            BOOST_CHECK_EQUAL(txArr.size(), 1u);
        }
    }
}

BOOST_AUTO_TEST_CASE(mainnet_pos_block_identification)
{
    Array blocks = read_json("mainnet_blocks.json");

    for (const Value& bv : blocks)
    {
        Object obj = bv.get_obj();
        std::string flags = find_value(obj, "flags").get_str();
        unsigned int nonce = (unsigned int)find_value(obj, "nonce").get_int64();
        Array txArr = find_value(obj, "tx").get_array();

        if (flags.find("proof-of-stake") != std::string::npos)
        {
            // PoS blocks have zero nonce and 2+ transactions (coinbase + coinstake)
            BOOST_CHECK_MESSAGE(nonce == 0,
                "PoS block should have zero nonce: " +
                find_value(obj, "description").get_str());
            BOOST_CHECK_MESSAGE(txArr.size() >= 2,
                "PoS block should have 2+ txs: " +
                find_value(obj, "description").get_str());
        }
    }
}

// ============================================================================
// Flash PoS identification — timestamp at flash hours (1, 6, 15, 20 UTC)
// ============================================================================

BOOST_AUTO_TEST_CASE(mainnet_flash_pos_identification)
{
    Array blocks = read_json("mainnet_blocks.json");

    for (const Value& bv : blocks)
    {
        Object obj = bv.get_obj();
        std::string desc = find_value(obj, "description").get_str();
        unsigned int nTime = (unsigned int)find_value(obj, "time").get_int64();

        // The FPoS block (hour 1 UTC) should be identified as flash stake
        if (desc.find("Flash PoS") != std::string::npos)
        {
            BOOST_CHECK_MESSAGE(IsFlashStake(nTime),
                "FPoS block should be at flash hour: " << desc);
        }

        // The regular PoS block (hour 22 UTC) should NOT be flash stake
        if (desc.find("Regular PoS") != std::string::npos)
        {
            BOOST_CHECK_MESSAGE(!IsFlashStake(nTime),
                "Regular PoS block should not be at flash hour: " << desc);
        }
    }
}

// ============================================================================
// Checkpoint block hash pinning
// ============================================================================

BOOST_AUTO_TEST_CASE(mainnet_checkpoint_block_50000)
{
    // Block 50000 is a hardened checkpoint — verify from fixture
    Array blocks = read_json("mainnet_blocks.json");

    for (const Value& bv : blocks)
    {
        Object obj = bv.get_obj();
        int64_t height = find_value(obj, "height").get_int64();

        if (height == 50000)
        {
            uint256 hash("0x" + find_value(obj, "hash").get_str());
            BOOST_CHECK(Checkpoints::CheckHardened(50000, hash));
        }
    }
}

// ============================================================================
// Block 1 — genesis successor verification
// ============================================================================

BOOST_AUTO_TEST_CASE(mainnet_block_1_genesis_link)
{
    Array blocks = read_json("mainnet_blocks.json");

    for (const Value& bv : blocks)
    {
        Object obj = bv.get_obj();
        int64_t height = find_value(obj, "height").get_int64();

        if (height == 1)
        {
            // Block 1's previous hash must be the genesis hash
            uint256 prevHash("0x" + find_value(obj, "previousblockhash").get_str());
            uint256 genesisHash("0x00000f79b700e6444665c4d090c9b8833664c4e2597c7087a6ba6391b956cc89");
            BOOST_CHECK(prevHash == genesisHash);

            // PoW premine block — single tx
            Array txArr = find_value(obj, "tx").get_array();
            BOOST_CHECK_EQUAL(txArr.size(), 1u);

            // Merkle root equals the single tx hash (identity)
            uint256 merkle("0x" + find_value(obj, "merkleroot").get_str());
            uint256 txHash("0x" + txArr[0].get_str());
            BOOST_CHECK(merkle == txHash);
        }
    }
}

// ============================================================================
// Entropy bit verification
// ============================================================================

BOOST_AUTO_TEST_CASE(mainnet_entropy_bits)
{
    Array blocks = read_json("mainnet_blocks.json");

    for (const Value& bv : blocks)
    {
        Object obj = bv.get_obj();
        std::string desc = find_value(obj, "description").get_str();
        int expectedBit = find_value(obj, "entropybit").get_int();

        CBlock blk = BlockFromJson(obj);
        unsigned int computedBit = blk.GetStakeEntropyBit();

        BOOST_CHECK_MESSAGE((int)computedBit == expectedBit,
            "Entropy bit mismatch for " << desc
            << ": expected " << expectedBit
            << " got " << computedBit);
    }
}

// ============================================================================
// Header field pinning — verify individual fields match fixture data
// ============================================================================

BOOST_AUTO_TEST_CASE(mainnet_header_field_pinning)
{
    Array blocks = read_json("mainnet_blocks.json");

    for (const Value& bv : blocks)
    {
        Object obj = bv.get_obj();
        std::string desc = find_value(obj, "description").get_str();

        CBlock blk = BlockFromJson(obj);

        // Version must be 1 for all current Pinkcoin blocks
        BOOST_CHECK_MESSAGE(blk.nVersion == 1,
            "Unexpected version for " << desc);

        // nBits must be nonzero
        BOOST_CHECK_MESSAGE(blk.nBits != 0,
            "nBits should be nonzero for " << desc);

        // Timestamp must be reasonable (after genesis, 2017+)
        BOOST_CHECK_MESSAGE(blk.nTime > 1488326400,
            "Timestamp too old for " << desc);
    }
}

BOOST_AUTO_TEST_SUITE_END()
