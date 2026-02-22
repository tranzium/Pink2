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
#include <boost/preprocessor/stringize.hpp>

#include "json/nlohmann/json.hpp"

#include "main.h"
#include "kernel.h"
#include "checkpoints.h"

#include <string>
#include <vector>
#include <cstdlib>
#include <fstream>
#include <filesystem>

using json = nlohmann::json;

// Local read_json that returns nlohmann::json (independent of json_spirit version in script_tests.cpp)
static json read_json(const std::string& filename)
{
    namespace fs = std::filesystem;
    fs::path testFile = fs::current_path() / "test" / "data" / filename;

#ifdef TEST_DATA_DIR
    if (!fs::exists(testFile))
    {
        testFile = fs::path(BOOST_PP_STRINGIZE(TEST_DATA_DIR)) / filename;
    }
#endif

    std::ifstream ifs(testFile.string().c_str(), std::ifstream::in);
    BOOST_REQUIRE_MESSAGE(ifs.good(), "Could not find/open " << filename);

    json v = json::parse(ifs);
    BOOST_REQUIRE_MESSAGE(v.is_array(), filename << " does not contain a json array");
    return v;
}

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
static CBlock BlockFromJson(const json& obj)
{
    CBlock blk;
    blk.nVersion       = obj["version"].get<int>();
    blk.hashPrevBlock  = uint256("0x" + obj["previousblockhash"].get<std::string>());
    blk.hashMerkleRoot = uint256("0x" + obj["merkleroot"].get<std::string>());
    blk.nTime          = static_cast<unsigned int>(obj["time"].get<int64_t>());

    std::string bitsHex = obj["bits"].get<std::string>();
    blk.nBits = static_cast<unsigned int>(strtoul(bitsHex.c_str(), nullptr, 16));

    blk.nNonce = static_cast<unsigned int>(obj["nonce"].get<int64_t>());
    return blk;
}

BOOST_AUTO_TEST_SUITE(mainnet_block_tests)

// ============================================================================
// Block header hash verification — pins scrypt hash of real mainnet headers
// ============================================================================

BOOST_AUTO_TEST_CASE(mainnet_block_header_hashes)
{
    json blocks = read_json("mainnet_blocks.json");
    BOOST_REQUIRE(blocks.size() >= 4);

    for (const auto& obj : blocks)
    {
        std::string desc = obj["description"].get<std::string>();
        uint256 expectedHash("0x" + obj["hash"].get<std::string>());

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
    json blocks = read_json("mainnet_blocks.json");

    for (const auto& obj : blocks)
    {
        std::string desc = obj["description"].get<std::string>();
        uint256 expectedMerkle("0x" + obj["merkleroot"].get<std::string>());

        const json& txArr = obj["tx"];
        std::vector<uint256> txHashes;
        for (const auto& tv : txArr)
            txHashes.push_back(uint256("0x" + tv.get<std::string>()));

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
    json blocks = read_json("mainnet_blocks.json");

    for (const auto& obj : blocks)
    {
        std::string flags = obj["flags"].get<std::string>();
        unsigned int nonce = static_cast<unsigned int>(obj["nonce"].get<int64_t>());
        const json& txArr = obj["tx"];

        if (flags.find("proof-of-work") != std::string::npos)
        {
            // PoW blocks have nonzero nonce and single coinbase tx
            BOOST_CHECK_MESSAGE(nonce > 0,
                "PoW block should have nonzero nonce: " +
                obj["description"].get<std::string>());
            BOOST_CHECK_EQUAL(txArr.size(), 1u);
        }
    }
}

BOOST_AUTO_TEST_CASE(mainnet_pos_block_identification)
{
    json blocks = read_json("mainnet_blocks.json");

    for (const auto& obj : blocks)
    {
        std::string flags = obj["flags"].get<std::string>();
        unsigned int nonce = static_cast<unsigned int>(obj["nonce"].get<int64_t>());
        const json& txArr = obj["tx"];

        if (flags.find("proof-of-stake") != std::string::npos)
        {
            // PoS blocks have zero nonce and 2+ transactions (coinbase + coinstake)
            BOOST_CHECK_MESSAGE(nonce == 0,
                "PoS block should have zero nonce: " +
                obj["description"].get<std::string>());
            BOOST_CHECK_MESSAGE(txArr.size() >= 2,
                "PoS block should have 2+ txs: " +
                obj["description"].get<std::string>());
        }
    }
}

// ============================================================================
// Flash PoS identification — timestamp at flash hours (1, 6, 15, 20 UTC)
// ============================================================================

BOOST_AUTO_TEST_CASE(mainnet_flash_pos_identification)
{
    json blocks = read_json("mainnet_blocks.json");

    for (const auto& obj : blocks)
    {
        std::string desc = obj["description"].get<std::string>();
        unsigned int nTime = static_cast<unsigned int>(obj["time"].get<int64_t>());

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
    json blocks = read_json("mainnet_blocks.json");

    for (const auto& obj : blocks)
    {
        int64_t height = obj["height"].get<int64_t>();

        if (height == 50000)
        {
            uint256 hash("0x" + obj["hash"].get<std::string>());
            BOOST_CHECK(Checkpoints::CheckHardened(50000, hash));
        }
    }
}

// ============================================================================
// Block 1 — genesis successor verification
// ============================================================================

BOOST_AUTO_TEST_CASE(mainnet_block_1_genesis_link)
{
    json blocks = read_json("mainnet_blocks.json");

    for (const auto& obj : blocks)
    {
        int64_t height = obj["height"].get<int64_t>();

        if (height == 1)
        {
            // Block 1's previous hash must be the genesis hash
            uint256 prevHash("0x" + obj["previousblockhash"].get<std::string>());
            uint256 genesisHash("0x00000f79b700e6444665c4d090c9b8833664c4e2597c7087a6ba6391b956cc89");
            BOOST_CHECK(prevHash == genesisHash);

            // PoW premine block — single tx
            const json& txArr = obj["tx"];
            BOOST_CHECK_EQUAL(txArr.size(), 1u);

            // Merkle root equals the single tx hash (identity)
            uint256 merkle("0x" + obj["merkleroot"].get<std::string>());
            uint256 txHash("0x" + txArr[0].get<std::string>());
            BOOST_CHECK(merkle == txHash);
        }
    }
}

// ============================================================================
// Entropy bit verification
// ============================================================================

BOOST_AUTO_TEST_CASE(mainnet_entropy_bits)
{
    json blocks = read_json("mainnet_blocks.json");

    for (const auto& obj : blocks)
    {
        std::string desc = obj["description"].get<std::string>();
        int expectedBit = obj["entropybit"].get<int>();

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
    json blocks = read_json("mainnet_blocks.json");

    for (const auto& obj : blocks)
    {
        std::string desc = obj["description"].get<std::string>();

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
