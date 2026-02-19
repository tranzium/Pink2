// Copyright (c) 2024-2026 The Pinkcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Consensus pinning tests — verify consensus-critical code paths and exact
// values that must never change accidentally.  Covers:
//   - CheckBlock sigops enforcement at MAX_BLOCK_SIGOPS boundary
//   - IsFinal time-based locktime edge cases
//   - Version constant pinning (CBlock/CTransaction::CURRENT_VERSION)
//   - Orphan transaction map operations
//   - Orphan block map and helper functions
//   - Reward function exact boundary values
//   - ProcessBlock/AcceptBlock dispatch (fDisablePOW, version, coinbase height)

#include <boost/test/unit_test.hpp>

#include "main.h"
#include "kernel.h"
#include "miner.h"
#include "wallet.h"
#include "test_framework.h"

extern CWallet* pwalletMain;
extern CBigNum bnProofOfWorkLimit;

// Globals from main.cpp not declared in main.h
extern std::multimap<uint256, CBlock*> mapOrphanBlocksByPrev;
extern std::set<std::pair<COutPoint, unsigned int> > setStakeSeenOrphan;
extern std::map<uint256, CTransaction> mapOrphanTransactions;
extern std::map<uint256, std::set<uint256> > mapOrphanTransactionsByPrev;

// Functions from main.cpp not declared in main.h
extern bool AddOrphanTx(const CTransaction& tx);
extern unsigned int LimitOrphanTxSize(unsigned int nMaxOrphans);
extern uint256 WantedByOrphan(const CBlock* pblockOrphan);

// ---------------------------------------------------------------------------
// Helpers — build minimal blocks for CheckBlock sigops tests
// ---------------------------------------------------------------------------
namespace {

// Build a minimal PoW block (same as checkblock_tests helper).
CBlock MakeMinimalPoW()
{
    CBlock block;
    block.nVersion = CBlock::CURRENT_VERSION;
    block.nTime = GetAdjustedTime();
    block.nBits = bnProofOfWorkLimit.GetCompact();
    block.nNonce = 0;

    CTransaction coinbase;
    coinbase.nTime = block.nTime;
    coinbase.vin.resize(1);
    coinbase.vin[0].prevout.SetNull();
    coinbase.vin[0].scriptSig = CScript() << 0 << 0;
    coinbase.vout.resize(1);
    coinbase.vout[0].nValue = 50 * COIN;
    coinbase.vout[0].scriptPubKey = CScript() << OP_TRUE;

    block.vtx.push_back(coinbase);
    block.hashMerkleRoot = block.BuildMerkleTree();

    return block;
}

// RAII guard for temporarily lowering PoW difficulty.
struct TestEasyPoW {
    CBigNum bnOrig;
    TestEasyPoW() : bnOrig(bnProofOfWorkLimit) {
        bnProofOfWorkLimit = CBigNum(~uint256(0) >> 2);
    }
    ~TestEasyPoW() { bnProofOfWorkLimit = bnOrig; }
};

// Mine a valid nonce for a block (assumes easy difficulty).
bool MineNonce(CBlock& block) {
    uint256 hashTarget = CBigNum().SetCompact(block.nBits).getuint256();
    block.nNonce = 0;
    while (block.GetPoWHash() > hashTarget) {
        block.nNonce++;
        if (block.nNonce > 10000) return false;
    }
    return true;
}

// Create a properly constructed block template at current tip.
// Requires EasyPoW guard to be active.
CBlock MakeBlockAtTip(int nVersion = CBlock::CURRENT_VERSION)
{
    pwalletMain->NewKeyPool();
    CBlock* ptemplate = CreateNewBlock(pwalletMain);
    if (!ptemplate) return CBlock();

    CBlock block = *ptemplate;
    delete ptemplate;

    block.nVersion = nVersion;
    block.nTime = pindexBest->GetMedianTimePast() + 1;

    int nHeight = pindexBest->nHeight + 1;
    CScript scriptSig = CScript() << nHeight;
    while (scriptSig.size() < 2)
        scriptSig.push_back(0xFF);
    block.vtx[0].vin[0].scriptSig = scriptSig;
    block.vtx[0].vout[0].scriptPubKey = CScript();
    block.vtx[0].nTime = static_cast<unsigned int>(block.nTime);

    block.hashMerkleRoot = block.BuildMerkleTree();

    return block;
}

// Clean up an orphan block entry from global maps.
void CleanupOrphanBlock(const uint256& hash)
{
    auto it = mapOrphanBlocks.find(hash);
    if (it != mapOrphanBlocks.end()) {
        CBlock* pblock = it->second;
        // Remove from mapOrphanBlocksByPrev
        for (auto it2 = mapOrphanBlocksByPrev.begin();
             it2 != mapOrphanBlocksByPrev.end(); ) {
            if (it2->second == pblock)
                it2 = mapOrphanBlocksByPrev.erase(it2);
            else
                ++it2;
        }
        // Remove from setStakeSeenOrphan if present
        setStakeSeenOrphan.erase(pblock->GetProofOfStake());
        delete pblock;
        mapOrphanBlocks.erase(it);
    }
}

} // anonymous namespace


// ===========================================================================
// Suite 1: CheckBlock sigops enforcement at boundaries
// ===========================================================================
BOOST_AUTO_TEST_SUITE(consensus_sigops_pinning)

BOOST_AUTO_TEST_CASE(max_block_sigops_is_20000)
{
    BOOST_CHECK_EQUAL(MAX_BLOCK_SIGOPS, 20000u);
    BOOST_CHECK_EQUAL(MAX_BLOCK_SIGOPS, MAX_BLOCK_SIZE / 50);
}

BOOST_AUTO_TEST_CASE(checkblock_sigops_at_exact_limit_passes)
{
    CBlock block = MakeMinimalPoW();

    // Create script with exactly MAX_BLOCK_SIGOPS OP_CHECKSIG opcodes
    CScript sigopScript;
    sigopScript.resize(MAX_BLOCK_SIGOPS, static_cast<unsigned char>(OP_CHECKSIG));

    block.vtx[0].vout[0].scriptPubKey = sigopScript;
    block.hashMerkleRoot = block.BuildMerkleTree();

    // Should pass — exactly at the limit
    BOOST_CHECK(block.CheckBlock(false, true, false));
}

BOOST_AUTO_TEST_CASE(checkblock_sigops_one_over_limit_fails)
{
    CBlock block = MakeMinimalPoW();

    // Create script with MAX_BLOCK_SIGOPS + 1 OP_CHECKSIG opcodes
    CScript sigopScript;
    sigopScript.resize(MAX_BLOCK_SIGOPS + 1, static_cast<unsigned char>(OP_CHECKSIG));

    block.vtx[0].vout[0].scriptPubKey = sigopScript;
    block.hashMerkleRoot = block.BuildMerkleTree();

    // Should fail — one over the limit
    BOOST_CHECK(!block.CheckBlock(false, true, false));
}

BOOST_AUTO_TEST_CASE(sigops_accumulate_across_transactions)
{
    CBlock block = MakeMinimalPoW();

    // Coinbase has 10,000 sigops
    CScript halfScript;
    halfScript.resize(10000, static_cast<unsigned char>(OP_CHECKSIG));
    block.vtx[0].vout[0].scriptPubKey = halfScript;

    // Add a non-coinbase tx with 10,001 sigops (total = 20,001 > limit)
    CTransaction tx2;
    tx2.nTime = block.nTime;
    tx2.vin.resize(1);
    tx2.vin[0].prevout.hash = uint256("0xfeed");
    tx2.vin[0].prevout.n = 0;
    tx2.vout.resize(1);
    tx2.vout[0].nValue = COIN;
    CScript halfScript2;
    halfScript2.resize(10001, static_cast<unsigned char>(OP_CHECKSIG));
    tx2.vout[0].scriptPubKey = halfScript2;

    block.vtx.push_back(tx2);
    block.hashMerkleRoot = block.BuildMerkleTree();

    // Total sigops = 10,000 + 10,001 = 20,001 > MAX_BLOCK_SIGOPS
    BOOST_CHECK(!block.CheckBlock(false, true, false));
}

BOOST_AUTO_TEST_CASE(sigops_across_transactions_at_limit_passes)
{
    CBlock block = MakeMinimalPoW();

    // Coinbase has 10,000 sigops
    CScript halfScript;
    halfScript.resize(10000, static_cast<unsigned char>(OP_CHECKSIG));
    block.vtx[0].vout[0].scriptPubKey = halfScript;

    // Second tx with exactly 10,000 sigops (total = 20,000 = limit)
    CTransaction tx2;
    tx2.nTime = block.nTime;
    tx2.vin.resize(1);
    tx2.vin[0].prevout.hash = uint256("0xfeed");
    tx2.vin[0].prevout.n = 0;
    tx2.vout.resize(1);
    tx2.vout[0].nValue = COIN;
    CScript halfScript2;
    halfScript2.resize(10000, static_cast<unsigned char>(OP_CHECKSIG));
    tx2.vout[0].scriptPubKey = halfScript2;

    block.vtx.push_back(tx2);
    block.hashMerkleRoot = block.BuildMerkleTree();

    // Total sigops = 10,000 + 10,000 = 20,000 = MAX_BLOCK_SIGOPS
    BOOST_CHECK(block.CheckBlock(false, true, false));
}

BOOST_AUTO_TEST_CASE(sigops_checkmultisig_counts_twenty)
{
    // OP_CHECKMULTISIG counts as 20 in GetLegacySigOpCount (fAccurate=false)
    CBlock block = MakeMinimalPoW();

    // 1001 OP_CHECKMULTISIG → 1001*20 = 20,020 sigops > 20,000 limit
    CScript multiScript;
    multiScript.resize(1001, static_cast<unsigned char>(OP_CHECKMULTISIG));
    block.vtx[0].vout[0].scriptPubKey = multiScript;
    block.hashMerkleRoot = block.BuildMerkleTree();

    BOOST_CHECK(!block.CheckBlock(false, true, false));

    // 1000 OP_CHECKMULTISIG → 1000*20 = 20,000 sigops = limit
    CScript multiScript2;
    multiScript2.resize(1000, static_cast<unsigned char>(OP_CHECKMULTISIG));
    block.vtx[0].vout[0].scriptPubKey = multiScript2;
    block.hashMerkleRoot = block.BuildMerkleTree();

    BOOST_CHECK(block.CheckBlock(false, true, false));
}

BOOST_AUTO_TEST_CASE(sigops_in_scriptsig_counted)
{
    // GetLegacySigOpCount counts sigops in BOTH scriptSig and scriptPubKey
    CTransaction tx;
    tx.nTime = GetAdjustedTime();
    tx.vin.resize(1);
    tx.vin[0].prevout.hash = uint256("0xbeef");
    tx.vin[0].prevout.n = 0;
    // scriptSig with 3 OP_CHECKSIG
    tx.vin[0].scriptSig = CScript() << OP_CHECKSIG << OP_CHECKSIG << OP_CHECKSIG;
    tx.vout.resize(1);
    tx.vout[0].nValue = COIN;
    // scriptPubKey with 2 OP_CHECKSIG
    tx.vout[0].scriptPubKey = CScript() << OP_CHECKSIG << OP_CHECKSIG;

    // Total: 3 (input) + 2 (output) = 5
    BOOST_CHECK_EQUAL(tx.GetLegacySigOpCount(), 5u);
}

BOOST_AUTO_TEST_SUITE_END()


// ===========================================================================
// Suite 2: IsFinal time-based locktime edge cases
// ===========================================================================
BOOST_AUTO_TEST_SUITE(consensus_finality_pinning)

BOOST_AUTO_TEST_CASE(locktime_threshold_is_500000000)
{
    BOOST_CHECK_EQUAL(LOCKTIME_THRESHOLD, 500000000u);
}

BOOST_AUTO_TEST_CASE(is_final_time_locktime_passed)
{
    // nLockTime > LOCKTIME_THRESHOLD → time-based comparison.
    // If nLockTime < current time → final.
    CTransaction tx;
    tx.nLockTime = LOCKTIME_THRESHOLD + 1; // time-based, well in the past
    tx.vin.resize(1);
    tx.vin[0].prevout.SetNull();
    tx.vin[0].nSequence = std::numeric_limits<unsigned int>::max();

    // With nBlockTime far in the future, this should be final
    int64_t futureTime = static_cast<int64_t>(LOCKTIME_THRESHOLD) + 1000000;
    BOOST_CHECK(tx.IsFinal(0, futureTime));
}

BOOST_AUTO_TEST_CASE(is_final_time_locktime_not_passed)
{
    // nLockTime > LOCKTIME_THRESHOLD and > provided time → not final
    CTransaction tx;
    int64_t farFuture = GetAdjustedTime() + 365 * 24 * 60 * 60; // 1 year ahead
    tx.nLockTime = static_cast<unsigned int>(farFuture);
    tx.vin.resize(1);
    tx.vin[0].prevout.SetNull();
    tx.vin[0].nSequence = 0; // non-final sequence

    // At current time, nLockTime is in the future → not final
    BOOST_CHECK(!tx.IsFinal(0, GetAdjustedTime()));
}

BOOST_AUTO_TEST_CASE(is_final_at_locktime_threshold_boundary)
{
    // nLockTime = LOCKTIME_THRESHOLD exactly is treated as block height
    // (since the comparison is < LOCKTIME_THRESHOLD for height-based)
    CTransaction tx;
    tx.nLockTime = LOCKTIME_THRESHOLD;
    tx.vin.resize(1);
    tx.vin[0].prevout.SetNull();
    tx.vin[0].nSequence = 0;

    // LOCKTIME_THRESHOLD itself: the comparison
    // static_cast<int64_t>(nLockTime) < (static_cast<int64_t>(nLockTime) < LOCKTIME_THRESHOLD ? ...)
    // LOCKTIME_THRESHOLD < LOCKTIME_THRESHOLD is FALSE → time branch
    // So it compares nLockTime against nBlockTime
    int64_t futureTime = static_cast<int64_t>(LOCKTIME_THRESHOLD) + 1;
    BOOST_CHECK(tx.IsFinal(0, futureTime));

    // At exact locktime, not final (need strictly past)
    BOOST_CHECK(!tx.IsFinal(0, static_cast<int64_t>(LOCKTIME_THRESHOLD)));
}

BOOST_AUTO_TEST_CASE(is_final_nonfinal_sequence_blocks_finality)
{
    // When locktime is NOT yet passed AND any input has non-max nSequence,
    // the transaction is not final.
    CTransaction tx;
    tx.nLockTime = 999999; // height-based, well above any test block height
    tx.vin.resize(2);
    tx.vin[0].prevout.SetNull();
    tx.vin[0].nSequence = std::numeric_limits<unsigned int>::max(); // final
    tx.vin[1].prevout.SetNull();
    tx.vin[1].nSequence = 0; // non-final!

    // nLockTime > nBlockHeight (100), so locktime not passed.
    // Then checks sequences: vin[1] is non-final → tx is not final
    BOOST_CHECK(!tx.IsFinal(100, GetAdjustedTime()));
}

BOOST_AUTO_TEST_CASE(is_final_all_max_sequence_with_future_locktime)
{
    // All inputs with max sequence → IsFinal returns true even with
    // future locktime, because IsFinal only checks sequences AFTER the
    // locktime comparison fails.
    CTransaction tx;
    tx.nLockTime = 999999999; // far future height
    tx.vin.resize(1);
    tx.vin[0].prevout.SetNull();
    tx.vin[0].nSequence = std::numeric_limits<unsigned int>::max();

    // nLockTime 999999999 < LOCKTIME_THRESHOLD (500000000)? YES → height branch
    // 999999999 >= nBestHeight → locktime check fails → fall through to sequence check
    // All sequences are max → final!
    // Wait, no: IsFinal returns true only if nLockTime < (height or time).
    // 999999999 < 100 is false, so it doesn't return true early.
    // Then it checks all sequences. All max → returns true.
    BOOST_CHECK(tx.IsFinal(100, GetAdjustedTime()));
}

BOOST_AUTO_TEST_SUITE_END()


// ===========================================================================
// Suite 3: Version constant pinning
// ===========================================================================
BOOST_AUTO_TEST_SUITE(consensus_version_pinning)

BOOST_AUTO_TEST_CASE(block_current_version_is_one)
{
    BOOST_CHECK(CBlock::CURRENT_VERSION == 1);
}

BOOST_AUTO_TEST_CASE(transaction_current_version_is_one)
{
    BOOST_CHECK(CTransaction::CURRENT_VERSION == 1);
}

BOOST_AUTO_TEST_CASE(block_default_version_matches_constant)
{
    CBlock block;
    BOOST_CHECK(block.nVersion == CBlock::CURRENT_VERSION);
}

BOOST_AUTO_TEST_CASE(transaction_default_version_matches_constant)
{
    CTransaction tx;
    BOOST_CHECK(tx.nVersion == CTransaction::CURRENT_VERSION);
}

BOOST_AUTO_TEST_CASE(block_max_size_is_1mb)
{
    BOOST_CHECK_EQUAL(MAX_BLOCK_SIZE, 1000000u);
}

BOOST_AUTO_TEST_CASE(block_max_orphan_transactions)
{
    BOOST_CHECK_EQUAL(MAX_ORPHAN_TRANSACTIONS, 10000u);
    BOOST_CHECK_EQUAL(MAX_ORPHAN_TRANSACTIONS, MAX_BLOCK_SIZE / 100);
}

BOOST_AUTO_TEST_SUITE_END()


// ===========================================================================
// Suite 4: Orphan transaction map operations
// ===========================================================================
BOOST_AUTO_TEST_SUITE(consensus_orphan_tx_pinning)

BOOST_AUTO_TEST_CASE(add_orphan_tx_inserts_into_maps)
{
    // Clear any prior orphans
    mapOrphanTransactions.clear();
    mapOrphanTransactionsByPrev.clear();

    CTransaction tx;
    tx.nTime = GetAdjustedTime();
    tx.vin.resize(1);
    tx.vin[0].prevout.hash = uint256("0xaaaa");
    tx.vin[0].prevout.n = 0;
    tx.vout.resize(1);
    tx.vout[0].nValue = COIN;
    tx.vout[0].scriptPubKey = CScript() << OP_TRUE;

    BOOST_CHECK(AddOrphanTx(tx));
    BOOST_CHECK_EQUAL(mapOrphanTransactions.size(), 1u);
    BOOST_CHECK_EQUAL(mapOrphanTransactionsByPrev.size(), 1u);

    // Verify the prev-map entry points to the right parent hash
    BOOST_CHECK(mapOrphanTransactionsByPrev.count(uint256("0xaaaa")));
    BOOST_CHECK(mapOrphanTransactionsByPrev[uint256("0xaaaa")].count(tx.GetHash()));

    // Cleanup
    mapOrphanTransactions.clear();
    mapOrphanTransactionsByPrev.clear();
}

BOOST_AUTO_TEST_CASE(add_orphan_tx_duplicate_returns_false)
{
    mapOrphanTransactions.clear();
    mapOrphanTransactionsByPrev.clear();

    CTransaction tx;
    tx.nTime = GetAdjustedTime();
    tx.vin.resize(1);
    tx.vin[0].prevout.hash = uint256("0xbbbb");
    tx.vin[0].prevout.n = 0;
    tx.vout.resize(1);
    tx.vout[0].nValue = COIN;
    tx.vout[0].scriptPubKey = CScript() << OP_TRUE;

    BOOST_CHECK(AddOrphanTx(tx));
    BOOST_CHECK(!AddOrphanTx(tx)); // duplicate
    BOOST_CHECK_EQUAL(mapOrphanTransactions.size(), 1u);

    mapOrphanTransactions.clear();
    mapOrphanTransactionsByPrev.clear();
}

BOOST_AUTO_TEST_CASE(add_orphan_tx_oversize_rejected)
{
    mapOrphanTransactions.clear();
    mapOrphanTransactionsByPrev.clear();

    CTransaction tx;
    tx.nTime = GetAdjustedTime();
    tx.vin.resize(1);
    tx.vin[0].prevout.hash = uint256("0xcccc");
    tx.vin[0].prevout.n = 0;
    // Create output with large scriptPubKey to exceed 5000 byte limit
    tx.vout.resize(1);
    tx.vout[0].nValue = COIN;
    CScript bigScript;
    bigScript.resize(5000, 0x00);
    tx.vout[0].scriptPubKey = bigScript;

    // The serialized tx should be > 5000 bytes
    size_t txSize = tx.GetSerializeSize(SER_NETWORK, CTransaction::CURRENT_VERSION);
    BOOST_CHECK_GT(txSize, 5000u);

    BOOST_CHECK(!AddOrphanTx(tx));
    BOOST_CHECK_EQUAL(mapOrphanTransactions.size(), 0u);

    mapOrphanTransactions.clear();
    mapOrphanTransactionsByPrev.clear();
}

BOOST_AUTO_TEST_CASE(orphan_tx_prev_map_multiple_children)
{
    mapOrphanTransactions.clear();
    mapOrphanTransactionsByPrev.clear();

    uint256 parentHash("0xdddd");

    // Two orphan txs referencing the same parent
    CTransaction tx1;
    tx1.nTime = GetAdjustedTime();
    tx1.vin.resize(1);
    tx1.vin[0].prevout.hash = parentHash;
    tx1.vin[0].prevout.n = 0;
    tx1.vout.resize(1);
    tx1.vout[0].nValue = COIN;
    tx1.vout[0].scriptPubKey = CScript() << OP_TRUE;

    CTransaction tx2;
    tx2.nTime = GetAdjustedTime();
    tx2.vin.resize(1);
    tx2.vin[0].prevout.hash = parentHash;
    tx2.vin[0].prevout.n = 1; // different output index → different tx hash
    tx2.vout.resize(1);
    tx2.vout[0].nValue = 2 * COIN;
    tx2.vout[0].scriptPubKey = CScript() << OP_TRUE;

    BOOST_CHECK(AddOrphanTx(tx1));
    BOOST_CHECK(AddOrphanTx(tx2));
    BOOST_CHECK_EQUAL(mapOrphanTransactions.size(), 2u);

    // Both should be in the prev-map under the same parent
    BOOST_CHECK_EQUAL(mapOrphanTransactionsByPrev[parentHash].size(), 2u);

    mapOrphanTransactions.clear();
    mapOrphanTransactionsByPrev.clear();
}

BOOST_AUTO_TEST_CASE(limit_orphan_size_evicts_excess)
{
    mapOrphanTransactions.clear();
    mapOrphanTransactionsByPrev.clear();

    // Add 5 orphan transactions
    for (unsigned int i = 0; i < 5; ++i) {
        CTransaction tx;
        tx.nTime = GetAdjustedTime();
        tx.vin.resize(1);
        tx.vin[0].prevout.hash = GetRandHash();
        tx.vin[0].prevout.n = 0;
        tx.vout.resize(1);
        tx.vout[0].nValue = COIN;
        tx.vout[0].scriptPubKey = CScript() << OP_TRUE;
        AddOrphanTx(tx);
    }
    BOOST_CHECK_EQUAL(mapOrphanTransactions.size(), 5u);

    // Limit to 3 → should evict 2
    unsigned int nEvicted = LimitOrphanTxSize(3);
    BOOST_CHECK_EQUAL(nEvicted, 2u);
    BOOST_CHECK_EQUAL(mapOrphanTransactions.size(), 3u);

    mapOrphanTransactions.clear();
    mapOrphanTransactionsByPrev.clear();
}

BOOST_AUTO_TEST_CASE(limit_orphan_size_zero_clears_all)
{
    mapOrphanTransactions.clear();
    mapOrphanTransactionsByPrev.clear();

    for (unsigned int i = 0; i < 3; ++i) {
        CTransaction tx;
        tx.nTime = GetAdjustedTime();
        tx.vin.resize(1);
        tx.vin[0].prevout.hash = GetRandHash();
        tx.vin[0].prevout.n = 0;
        tx.vout.resize(1);
        tx.vout[0].nValue = COIN;
        tx.vout[0].scriptPubKey = CScript() << OP_TRUE;
        AddOrphanTx(tx);
    }
    BOOST_CHECK_EQUAL(mapOrphanTransactions.size(), 3u);

    unsigned int nEvicted = LimitOrphanTxSize(0);
    BOOST_CHECK_EQUAL(nEvicted, 3u);
    BOOST_CHECK_EQUAL(mapOrphanTransactions.size(), 0u);

    mapOrphanTransactionsByPrev.clear();
}

BOOST_AUTO_TEST_SUITE_END()


// ===========================================================================
// Suite 5: Orphan block map and helper operations
// ===========================================================================
BOOST_AUTO_TEST_SUITE(consensus_orphan_block_pinning)

BOOST_AUTO_TEST_CASE(orphan_block_map_insert_and_find)
{
    // Verify basic map operations on mapOrphanBlocks
    size_t origSize = mapOrphanBlocks.size();

    uint256 hash("0x1111111111111111111111111111111111111111111111111111111111111111");
    CBlock* pblock = new CBlock();
    pblock->nTime = 12345;
    pblock->hashPrevBlock = uint256("0x2222");
    mapOrphanBlocks[hash] = pblock;

    BOOST_CHECK_EQUAL(mapOrphanBlocks.size(), origSize + 1);
    BOOST_CHECK(mapOrphanBlocks.count(hash));
    BOOST_CHECK_EQUAL(mapOrphanBlocks[hash]->nTime, 12345u);

    // Cleanup
    delete pblock;
    mapOrphanBlocks.erase(hash);
    BOOST_CHECK_EQUAL(mapOrphanBlocks.size(), origSize);
}

BOOST_AUTO_TEST_CASE(orphan_block_by_prev_multimap)
{
    size_t origSize = mapOrphanBlocksByPrev.size();
    uint256 prevHash("0x3333");

    CBlock* pblock1 = new CBlock();
    pblock1->hashPrevBlock = prevHash;
    CBlock* pblock2 = new CBlock();
    pblock2->hashPrevBlock = prevHash;

    mapOrphanBlocksByPrev.insert(std::make_pair(prevHash, pblock1));
    mapOrphanBlocksByPrev.insert(std::make_pair(prevHash, pblock2));

    BOOST_CHECK_EQUAL(mapOrphanBlocksByPrev.count(prevHash), 2u);

    // Cleanup
    for (auto it = mapOrphanBlocksByPrev.lower_bound(prevHash);
         it != mapOrphanBlocksByPrev.upper_bound(prevHash); ++it) {
        delete it->second;
    }
    mapOrphanBlocksByPrev.erase(prevHash);
    BOOST_CHECK_EQUAL(mapOrphanBlocksByPrev.size(), origSize);
}

BOOST_AUTO_TEST_CASE(wanted_by_orphan_single_block)
{
    // WantedByOrphan with a single orphan (no chain) returns its hashPrevBlock
    uint256 hash("0x4444444444444444444444444444444444444444444444444444444444444444");
    uint256 prevHash("0x5555555555555555555555555555555555555555555555555555555555555555");

    CBlock* pblock = new CBlock();
    pblock->hashPrevBlock = prevHash;

    // prevHash is NOT in mapOrphanBlocks, so WantedByOrphan stops immediately
    BOOST_CHECK(!mapOrphanBlocks.count(prevHash));

    mapOrphanBlocks[hash] = pblock;

    uint256 wanted = WantedByOrphan(pblock);
    BOOST_CHECK(wanted == prevHash);

    delete pblock;
    mapOrphanBlocks.erase(hash);
}

BOOST_AUTO_TEST_CASE(wanted_by_orphan_follows_chain)
{
    // Chain of 3 orphans: C → B → A → (unknown parent)
    // WantedByOrphan(C) should return A's hashPrevBlock
    uint256 unknownParent("0x6666666666666666666666666666666666666666666666666666666666666666");
    uint256 hashA("0x7777777777777777777777777777777777777777777777777777777777777777");
    uint256 hashB("0x8888888888888888888888888888888888888888888888888888888888888888");
    uint256 hashC("0x9999999999999999999999999999999999999999999999999999999999999999");

    CBlock* pblockA = new CBlock();
    pblockA->hashPrevBlock = unknownParent;

    CBlock* pblockB = new CBlock();
    pblockB->hashPrevBlock = hashA;

    CBlock* pblockC = new CBlock();
    pblockC->hashPrevBlock = hashB;

    mapOrphanBlocks[hashA] = pblockA;
    mapOrphanBlocks[hashB] = pblockB;
    mapOrphanBlocks[hashC] = pblockC;

    // WantedByOrphan(C) follows: C.prev=B (in map) → B.prev=A (in map) → A.prev=unknown (not in map)
    // Returns A.hashPrevBlock = unknownParent
    uint256 wanted = WantedByOrphan(pblockC);
    BOOST_CHECK(wanted == unknownParent);

    // Cleanup
    delete pblockA;
    delete pblockB;
    delete pblockC;
    mapOrphanBlocks.erase(hashA);
    mapOrphanBlocks.erase(hashB);
    mapOrphanBlocks.erase(hashC);
}

BOOST_AUTO_TEST_CASE(set_stake_seen_basic_operations)
{
    COutPoint outpoint(uint256("0xaaaa"), 0);
    unsigned int nStakeTime = 1234567890;
    auto stakeProof = std::make_pair(outpoint, nStakeTime);

    // Should not be present initially
    BOOST_CHECK(!setStakeSeen.count(stakeProof));

    setStakeSeen.insert(stakeProof);
    BOOST_CHECK(setStakeSeen.count(stakeProof));

    setStakeSeen.erase(stakeProof);
    BOOST_CHECK(!setStakeSeen.count(stakeProof));
}

BOOST_AUTO_TEST_CASE(set_stake_seen_orphan_is_separate)
{
    // setStakeSeenOrphan is separate from setStakeSeen
    COutPoint outpoint(uint256("0xbbbb"), 1);
    unsigned int nStakeTime = 987654321u;
    auto stakeProof = std::make_pair(outpoint, nStakeTime);

    setStakeSeenOrphan.insert(stakeProof);
    BOOST_CHECK(setStakeSeenOrphan.count(stakeProof));
    BOOST_CHECK(!setStakeSeen.count(stakeProof));

    setStakeSeenOrphan.erase(stakeProof);
}

BOOST_AUTO_TEST_SUITE_END()


// ===========================================================================
// Suite 6: Reward function boundary values (golden pinning)
// ===========================================================================
BOOST_AUTO_TEST_SUITE(consensus_reward_boundary_pinning)

BOOST_AUTO_TEST_CASE(pow_reward_height_0_is_zero)
{
    // Height 0 (genesis): no subsidy block 1 check fails, nHeight >= 17000 fails
    BOOST_CHECK_EQUAL(GetProofOfWorkReward(0, 0), 0);
}

BOOST_AUTO_TEST_CASE(pow_reward_height_1_is_premine)
{
    BOOST_CHECK_EQUAL(GetProofOfWorkReward(1, 0), 364800000LL * COIN);
}

BOOST_AUTO_TEST_CASE(pow_reward_height_2_is_zero)
{
    // Heights 2 through 16999: nHeight != 1 and nHeight < 17000
    BOOST_CHECK_EQUAL(GetProofOfWorkReward(2, 0), 0);
}

BOOST_AUTO_TEST_CASE(pow_reward_height_16999_is_zero)
{
    // Last height before subsidy starts
    BOOST_CHECK_EQUAL(GetProofOfWorkReward(16999, 0), 0);
}

BOOST_AUTO_TEST_CASE(pow_reward_height_17000_is_50_coins)
{
    // First height with PoW subsidy: nHalving = 17000 / 2 / 423400 = 0
    BOOST_CHECK_EQUAL(GetProofOfWorkReward(17000, 0), 50 * COIN);
}

BOOST_AUTO_TEST_CASE(pow_reward_height_17001_is_50_coins)
{
    // nHalving = 17001 / 2 / 423400 = 0 → 50 COIN
    BOOST_CHECK_EQUAL(GetProofOfWorkReward(17001, 0), 50 * COIN);
}

BOOST_AUTO_TEST_CASE(pow_reward_first_halving_exact)
{
    // First halving at nHeight / 2 / YEARLY_BLOCKCOUNT = 1
    // nHeight = 2 * YEARLY_BLOCKCOUNT = 846800
    int64_t nHalving = 846800 / 2 / YEARLY_BLOCKCOUNT;
    BOOST_CHECK_EQUAL(nHalving, 1);
    BOOST_CHECK_EQUAL(GetProofOfWorkReward(846800, 0), 25 * COIN);
}

BOOST_AUTO_TEST_CASE(pow_reward_preserves_fees)
{
    // Fees are passed through at all heights
    BOOST_CHECK_EQUAL(GetProofOfWorkReward(0, 1000), 1000);
    BOOST_CHECK_EQUAL(GetProofOfWorkReward(1, 1000), 364800000LL * COIN + 1000);
    BOOST_CHECK_EQUAL(GetProofOfWorkReward(17000, 1000), 50 * COIN + 1000);
}

BOOST_AUTO_TEST_CASE(pos_reward_height_16239_is_zero)
{
    // Just before PoS subsidy starts at 16240
    // pindexBest->nTime doesn't matter when nHeight < 16240
    int64_t reward = GetProofOfStakeReward(100, 0, 16239, GetAdjustedTime());
    BOOST_CHECK_EQUAL(reward, 0);
}

BOOST_AUTO_TEST_CASE(pos_reward_height_16240_regular)
{
    // First height with PoS subsidy
    // Pre-v231: nHalving = 16240 / 2 / 423400 = 0 → 100 COIN
    // Use fixed timestamp at 10:00 UTC (NOT a flash-stake hour: 1,6,15,20)
    // so IsFlashStake() returns false and we test the regular reward path.
    unsigned int nNonFlashTime = 1700128800; // 2023-11-16 10:00:00 UTC
    unsigned int savedBestTime = pindexBest ? pindexBest->nTime : 0;
    // Ensure pindexBest->nTime < nTimeV231 for pre-v231 path
    if (pindexBest) pindexBest->nTime = nTimeV231 - 1;

    int64_t reward = GetProofOfStakeReward(100, 0, 16240, nNonFlashTime);
    BOOST_CHECK_EQUAL(reward, 100 * COIN);

    if (pindexBest) pindexBest->nTime = savedBestTime;
}

BOOST_AUTO_TEST_CASE(pos_reward_integer_division_bug_golden)
{
    // Post-v231 regular stake: nSubsidy = (100 * COIN) / 3
    // Then nSubsidy *= 16 / 10 → 16/10 = 1 in integer division → no-op
    // This is THE consensus bug that must never be fixed.
    // Use fixed timestamp at 10:00 UTC (NOT a flash-stake hour: 1,6,15,20)
    unsigned int nNonFlashTime = 1700128800; // 2023-11-16 10:00:00 UTC
    unsigned int savedBestTime = pindexBest ? pindexBest->nTime : 0;
    if (pindexBest) pindexBest->nTime = nTimeV231 + 1;

    int64_t reward = GetProofOfStakeReward(100, 0, 16240, nNonFlashTime);
    // 100 COIN / 3 = 3333333333 satoshis (integer division)
    // Then *= 1 (16/10 in integer math) = 3333333333
    BOOST_CHECK_EQUAL(reward, 3333333333LL);

    if (pindexBest) pindexBest->nTime = savedBestTime;
}

BOOST_AUTO_TEST_CASE(flash_stake_constants_are_accessible)
{
    // Verify flash-stake hour constants from main.h are the expected values
    BOOST_CHECK_EQUAL(nFlashStakeHour1, 15u);
    BOOST_CHECK_EQUAL(nFlashStakeHour2, 20u);
    BOOST_CHECK_EQUAL(nFlashStakeHour3, 1u);
    BOOST_CHECK_EQUAL(nFlashStakeHour4, 6u);

    // Verify retargeting timespan constants
    BOOST_CHECK_EQUAL(nTargetTimespan, 3600);           // 60 minutes
    BOOST_CHECK_EQUAL(nStakeTargetTimespan, 7200);       // 2 hours
    BOOST_CHECK_EQUAL(nFlashStakeTargetTimespan, 600);   // 10 minutes
}

BOOST_AUTO_TEST_CASE(is_flash_stake_hour_boundaries)
{
    // Construct timestamps at specific UTC hours using a known epoch base
    // 2023-11-16 00:00:00 UTC = 1700092800
    unsigned int base = 1700092800;

    // Flash-stake hours: 1, 6, 15, 20
    BOOST_CHECK_EQUAL(IsFlashStake(base + 1 * 3600), true);   // 01:00 UTC
    BOOST_CHECK_EQUAL(IsFlashStake(base + 6 * 3600), true);   // 06:00 UTC
    BOOST_CHECK_EQUAL(IsFlashStake(base + 15 * 3600), true);  // 15:00 UTC
    BOOST_CHECK_EQUAL(IsFlashStake(base + 20 * 3600), true);  // 20:00 UTC

    // Non-flash-stake hours: 0, 2, 5, 7, 10, 14, 16, 19, 21, 23
    BOOST_CHECK_EQUAL(IsFlashStake(base + 0 * 3600), false);  // 00:00 UTC
    BOOST_CHECK_EQUAL(IsFlashStake(base + 2 * 3600), false);  // 02:00 UTC
    BOOST_CHECK_EQUAL(IsFlashStake(base + 5 * 3600), false);  // 05:00 UTC
    BOOST_CHECK_EQUAL(IsFlashStake(base + 7 * 3600), false);  // 07:00 UTC
    BOOST_CHECK_EQUAL(IsFlashStake(base + 10 * 3600), false); // 10:00 UTC
    BOOST_CHECK_EQUAL(IsFlashStake(base + 14 * 3600), false); // 14:00 UTC
    BOOST_CHECK_EQUAL(IsFlashStake(base + 16 * 3600), false); // 16:00 UTC
    BOOST_CHECK_EQUAL(IsFlashStake(base + 19 * 3600), false); // 19:00 UTC
    BOOST_CHECK_EQUAL(IsFlashStake(base + 21 * 3600), false); // 21:00 UTC
    BOOST_CHECK_EQUAL(IsFlashStake(base + 23 * 3600), false); // 23:00 UTC
}

BOOST_AUTO_TEST_CASE(is_flash_stake_mid_hour)
{
    // Flash-stake check is based on the hour, not exact boundary
    // 2023-11-16 15:30:00 UTC = base + 15*3600 + 1800
    unsigned int base = 1700092800;
    BOOST_CHECK_EQUAL(IsFlashStake(base + 15 * 3600 + 1800), true);   // 15:30
    BOOST_CHECK_EQUAL(IsFlashStake(base + 15 * 3600 + 3599), true);   // 15:59:59
    BOOST_CHECK_EQUAL(IsFlashStake(base + 10 * 3600 + 1800), false);  // 10:30
}

BOOST_AUTO_TEST_SUITE_END()


// ===========================================================================
// Suite 7: ProcessBlock/AcceptBlock dispatch (TestChain fixture)
// ===========================================================================
BOOST_FIXTURE_TEST_SUITE(consensus_processblock_pinning, TestChain)

BOOST_AUTO_TEST_CASE(test_chain_has_old_timestamps)
{
    // Verify our TestChain blocks have timestamps before nTimeV231.
    // This is important: PoW blocks are allowed because pindexBest->nTime < nTimeV231.
    BOOST_REQUIRE(pindexBest != nullptr);
    BOOST_CHECK_LT(pindexBest->nTime, nTimeV231);
}

BOOST_AUTO_TEST_CASE(pow_disabled_after_v231_rejects_pow)
{
    TestEasyPoW guard;

    // Temporarily set nTimeV231 = 0 so pindexBest->nTime > nTimeV231
    unsigned int nOrigV231 = nTimeV231;
    nTimeV231 = 0;

    CBlock block = MakeBlockAtTip();
    BOOST_REQUIRE(!block.IsNull());
    BOOST_REQUIRE(MineNonce(block));
    BOOST_REQUIRE(block.IsProofOfWork());

    // ProcessBlock should reject: "Received POW Block. Proof-of-work was disabled..."
    bool result = ProcessBlock(NULL, &block);
    BOOST_CHECK(!result);

    nTimeV231 = nOrigV231;
}

BOOST_AUTO_TEST_CASE(pow_allowed_before_v231)
{
    TestEasyPoW guard;

    // With real nTimeV231, PoW is allowed (pindexBest->nTime < nTimeV231)
    BOOST_CHECK_LT(pindexBest->nTime, nTimeV231);

    int heightBefore = chainHeight();
    CBlock block = MakeBlockAtTip();
    BOOST_REQUIRE(!block.IsNull());
    BOOST_REQUIRE(MineNonce(block));

    bool result = ProcessBlock(NULL, &block);
    BOOST_CHECK(result);
    BOOST_CHECK_EQUAL(chainHeight(), heightBefore + 1);
}

BOOST_AUTO_TEST_CASE(process_block_already_known_rejected)
{
    // A block whose hash is already in mapBlockIndex is rejected.
    // This covers the first check in ProcessBlock.
    TestEasyPoW guard;

    CBlock block = MakeBlockAtTip();
    BOOST_REQUIRE(!block.IsNull());
    BOOST_REQUIRE(MineNonce(block));

    // Submit once — should succeed
    int heightBefore = chainHeight();
    BOOST_CHECK(ProcessBlock(NULL, &block));
    BOOST_CHECK_EQUAL(chainHeight(), heightBefore + 1);

    // Submit again — should fail (already in mapBlockIndex)
    BOOST_CHECK(!ProcessBlock(NULL, &block));
}

BOOST_AUTO_TEST_CASE(orphan_block_via_direct_map_operations)
{
    // Test orphan block map behavior directly (ProcessBlock's checkpoint
    // difficulty check prevents easy orphan creation in test mode).
    size_t origSize = mapOrphanBlocks.size();

    // Simulate what ProcessBlock does at line 2543-2545
    uint256 hash("0xdeadbeef0000000000000000000000000000000000000000000000000000dead");
    uint256 prevHash("0xfeedface0000000000000000000000000000000000000000000000000000feed");

    CBlock* pblock = new CBlock();
    pblock->hashPrevBlock = prevHash;
    pblock->nTime = GetAdjustedTime();

    mapOrphanBlocks.insert(std::make_pair(hash, pblock));
    mapOrphanBlocksByPrev.insert(std::make_pair(pblock->hashPrevBlock, pblock));

    BOOST_CHECK_EQUAL(mapOrphanBlocks.size(), origSize + 1);
    BOOST_CHECK(mapOrphanBlocks.count(hash));
    BOOST_CHECK(mapOrphanBlocksByPrev.count(prevHash));

    // Duplicate check (same as ProcessBlock line 2482)
    BOOST_CHECK(mapOrphanBlocks.count(hash));

    // Cleanup (same as ProcessBlock lines 2611-2613)
    CleanupOrphanBlock(hash);
    BOOST_CHECK_EQUAL(mapOrphanBlocks.size(), origSize);
}

BOOST_AUTO_TEST_CASE(accept_block_version_too_high_rejected)
{
    TestEasyPoW guard;

    // Test blocks have nTime < 1538265600 (Sep 30 2018).
    // AcceptBlock rejects nVersion > CURRENT_VERSION when nTime < cutoff.
    CBlock block = MakeBlockAtTip(CBlock::CURRENT_VERSION + 1);
    BOOST_REQUIRE(!block.IsNull());
    BOOST_REQUIRE(MineNonce(block));

    // Version 2 should be rejected (nTime < 1538265600)
    BOOST_CHECK(!ProcessBlock(NULL, &block));
}

BOOST_AUTO_TEST_CASE(accept_block_wrong_coinbase_height_rejected)
{
    TestEasyPoW guard;

    CBlock block = MakeBlockAtTip();
    BOOST_REQUIRE(!block.IsNull());

    // Put wrong height in coinbase scriptSig
    int wrongHeight = pindexBest->nHeight + 999;
    CScript badScript = CScript() << wrongHeight;
    while (badScript.size() < 2) badScript.push_back(0xFF);
    block.vtx[0].vin[0].scriptSig = badScript;

    // Rebuild merkle root and re-mine
    block.hashMerkleRoot = block.BuildMerkleTree();
    BOOST_REQUIRE(MineNonce(block));

    // AcceptBlock enforces: coinbase scriptSig must start with serialized nHeight
    BOOST_CHECK(!ProcessBlock(NULL, &block));
}

BOOST_AUTO_TEST_CASE(accept_block_correct_coinbase_height_accepted)
{
    TestEasyPoW guard;

    int heightBefore = chainHeight();
    CBlock block = MakeBlockAtTip();
    BOOST_REQUIRE(!block.IsNull());
    BOOST_REQUIRE(MineNonce(block));

    // Correct height is already set by MakeBlockAtTip
    BOOST_CHECK(ProcessBlock(NULL, &block));
    BOOST_CHECK_EQUAL(chainHeight(), heightBefore + 1);
}

BOOST_AUTO_TEST_CASE(accept_block_nonfinal_tx_rejected)
{
    TestEasyPoW guard;

    CBlock block = MakeBlockAtTip();
    BOOST_REQUIRE(!block.IsNull());

    // Add a non-final transaction (nLockTime far in the future)
    CTransaction nonFinalTx;
    nonFinalTx.nTime = block.nTime;
    nonFinalTx.nLockTime = pindexBest->nHeight + 10000; // far future height
    nonFinalTx.vin.resize(1);
    nonFinalTx.vin[0].prevout.hash = uint256("0xbeef");
    nonFinalTx.vin[0].prevout.n = 0;
    nonFinalTx.vin[0].nSequence = 0; // non-final sequence
    nonFinalTx.vout.resize(1);
    nonFinalTx.vout[0].nValue = COIN;
    nonFinalTx.vout[0].scriptPubKey = CScript() << OP_TRUE;

    block.vtx.push_back(nonFinalTx);
    block.hashMerkleRoot = block.BuildMerkleTree();
    BOOST_REQUIRE(MineNonce(block));

    // AcceptBlock checks IsFinal for all txs — this should fail
    BOOST_CHECK(!ProcessBlock(NULL, &block));
}

BOOST_AUTO_TEST_CASE(connect_block_pow_reward_not_exceeded)
{
    // Verify that blocks mined by TestChain have coinbase reward
    // within the allowed limit.
    BOOST_REQUIRE(!coinbaseTxns.empty());

    // Block 1: premine
    BOOST_CHECK_EQUAL(coinbaseTxns[0].GetValueOut(),
                      GetProofOfWorkReward(1, 0));

    // Blocks 2+: heights 2-16999 have 0 subsidy, so coinbase should be 0
    if (coinbaseTxns.size() > 1) {
        int64_t coinbaseValue = coinbaseTxns[1].GetValueOut();
        int64_t expectedReward = GetProofOfWorkReward(2, 0);
        BOOST_CHECK_EQUAL(coinbaseValue, expectedReward);
        BOOST_CHECK_EQUAL(coinbaseValue, 0);
    }
}

BOOST_AUTO_TEST_CASE(connect_block_money_supply_chain)
{
    // Money supply at each height = prev supply + nMint
    CBlockIndex* pGenesis = blockIndexAt(0);
    BOOST_REQUIRE(pGenesis != nullptr);
    BOOST_CHECK_EQUAL(pGenesis->nMoneySupply, 0);

    // Walk the chain and verify accumulation
    for (int h = 1; h <= std::min(chainHeight(), 5); ++h) {
        CBlockIndex* pindex = blockIndexAt(h);
        CBlockIndex* pprev = blockIndexAt(h - 1);
        BOOST_REQUIRE(pindex != nullptr);
        BOOST_REQUIRE(pprev != nullptr);

        BOOST_CHECK_EQUAL(pindex->nMoneySupply,
                          pprev->nMoneySupply + pindex->nMint);
    }
}

BOOST_AUTO_TEST_CASE(accept_block_duplicate_hash_rejected)
{
    // A block whose hash is already in mapBlockIndex should be rejected
    BOOST_REQUIRE(pindexGenesisBlock != nullptr);

    CBlock genesisBlock;
    BOOST_REQUIRE(genesisBlock.ReadFromDisk(pindexGenesisBlock));

    // ProcessBlock checks: mapBlockIndex.count(hash) → returns error
    BOOST_CHECK(!ProcessBlock(NULL, &genesisBlock));
}

BOOST_AUTO_TEST_CASE(block_index_all_pow_in_test_chain)
{
    // All blocks in the TestChain should be PoW
    for (int h = 1; h <= std::min(chainHeight(), 10); ++h) {
        CBlockIndex* pindex = blockIndexAt(h);
        BOOST_REQUIRE(pindex != nullptr);
        BOOST_CHECK(!pindex->IsProofOfStake());
    }
}

BOOST_AUTO_TEST_SUITE_END()


// ===========================================================================
// Suite 8: CheckBlock additional edge cases
// ===========================================================================
BOOST_AUTO_TEST_SUITE(consensus_checkblock_pinning)

BOOST_AUTO_TEST_CASE(checkblock_oversize_block_rejected)
{
    CBlock block = MakeMinimalPoW();

    // Make block oversized by adding a huge scriptPubKey
    CScript hugeScript;
    hugeScript.resize(MAX_BLOCK_SIZE + 1, 0x00);
    block.vtx[0].vout[0].scriptPubKey = hugeScript;
    block.hashMerkleRoot = block.BuildMerkleTree();

    BOOST_CHECK(!block.CheckBlock(false, true, false));
}

BOOST_AUTO_TEST_CASE(checkblock_tx_timestamp_after_block_rejected)
{
    CBlock block = MakeMinimalPoW();
    // Transaction timestamp after block timestamp
    block.vtx[0].nTime = block.nTime + 1;
    block.hashMerkleRoot = block.BuildMerkleTree();

    BOOST_CHECK(!block.CheckBlock(false, true, false));
}

BOOST_AUTO_TEST_CASE(checkblock_coinbase_value_positive)
{
    // A valid PoW block's coinbase can have a positive value
    CBlock block = MakeMinimalPoW();
    block.vtx[0].vout[0].nValue = 50 * COIN;
    block.hashMerkleRoot = block.BuildMerkleTree();
    BOOST_CHECK(block.CheckBlock(false, true, false));
}

BOOST_AUTO_TEST_CASE(checkblock_single_coinbase_valid)
{
    CBlock block = MakeMinimalPoW();
    BOOST_CHECK_EQUAL(block.vtx.size(), 1u);
    BOOST_CHECK(block.vtx[0].IsCoinBase());
    BOOST_CHECK(block.CheckBlock(false, true, false));
}

BOOST_AUTO_TEST_CASE(checkblock_duplicate_txids_rejected)
{
    CBlock block = MakeMinimalPoW();
    // Add a duplicate of the coinbase (same hash)
    block.vtx.push_back(block.vtx[0]);
    block.hashMerkleRoot = block.BuildMerkleTree();

    // CheckBlock rejects duplicate txids (and also multiple coinbases)
    BOOST_CHECK(!block.CheckBlock(false, true, false));
}

BOOST_AUTO_TEST_SUITE_END()
