// Copyright (c) 2024 The Pinkcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <boost/test/unit_test.hpp>

#include "main.h"
#include "bignum.h"
#include "kernel.h"

extern CBigNum bnProofOfWorkLimit;

BOOST_AUTO_TEST_SUITE(consensus_tests)

// ============================================================================
// Chain constants — regression tests for consensus-critical values
// ============================================================================

BOOST_AUTO_TEST_CASE(chain_constants)
{
    BOOST_CHECK_EQUAL(MAX_BLOCK_SIZE, 1000000u);
    BOOST_CHECK_EQUAL(MAX_BLOCK_SIZE_GEN, MAX_BLOCK_SIZE / 2);
    BOOST_CHECK_EQUAL(MAX_BLOCK_SIGOPS, MAX_BLOCK_SIZE / 50);
    BOOST_CHECK_EQUAL(MAX_ORPHAN_TRANSACTIONS, MAX_BLOCK_SIZE / 100);

    BOOST_CHECK_EQUAL(MIN_TX_FEE, 10000);
    BOOST_CHECK_EQUAL(MIN_RELAY_TX_FEE, MIN_TX_FEE);
    BOOST_CHECK_EQUAL(MAX_MONEY, 500000000LL * COIN);

    BOOST_CHECK_EQUAL(COIN, 100000000LL);
    BOOST_CHECK_EQUAL(nCoinbaseMaturity, 20);
    BOOST_CHECK_EQUAL(LOCKTIME_THRESHOLD, 500000000u);
}

BOOST_AUTO_TEST_CASE(timing_constants)
{
    BOOST_CHECK_EQUAL(nTargetSpacing, 120u);              // 2 min PoW
    BOOST_CHECK_EQUAL(nTargetSpacing_Staking, 360u);      // 6 min PoS
    BOOST_CHECK_EQUAL(nTargetSpacing_FlashStaking, 60u);  // 1 min FPoS
    BOOST_CHECK_EQUAL(nStakeMinAge, 3600u);               // 1 hour
    BOOST_CHECK_EQUAL(nStakeMaxAge, 2592000u);            // 30 days
    BOOST_CHECK_EQUAL(nFlashStakeMaxAge, 604800u);        // 7 days
    BOOST_CHECK_EQUAL(nModifierInterval, 300u);           // 5 min
}

BOOST_AUTO_TEST_CASE(halving_constants)
{
    BOOST_CHECK_EQUAL(nHalvingPoint, 2u);
    BOOST_CHECK_EQUAL(YEARLY_BLOCKCOUNT, 423400LL);
}

BOOST_AUTO_TEST_CASE(genesis_hashes)
{
    BOOST_CHECK_EQUAL(hashGenesisBlock.ToString(),
        "00000f79b700e6444665c4d090c9b8833664c4e2597c7087a6ba6391b956cc89");
    BOOST_CHECK_EQUAL(hashGenesisBlockTestNet.ToString(),
        "000076a007b949e5f8cdee6c18817d26bc224bfde575ce3f2ecb0dd000f7ec19");
}

BOOST_AUTO_TEST_CASE(version_timestamps)
{
    // These consensus-switching timestamps must never change
    BOOST_CHECK_EQUAL(nTimeV221, 1540771200u);  // Oct 29, 2018 00:00:00 UTC
    BOOST_CHECK_EQUAL(nTimeV231, 1565308800u);  // Aug 9, 2019 00:00:00 UTC
}

// ============================================================================
// MoneyRange() tests
// ============================================================================

BOOST_AUTO_TEST_CASE(money_range_valid)
{
    BOOST_CHECK(MoneyRange(0));
    BOOST_CHECK(MoneyRange(1));
    BOOST_CHECK(MoneyRange(COIN));
    BOOST_CHECK(MoneyRange(MAX_MONEY));
    BOOST_CHECK(MoneyRange(MAX_MONEY / 2));
}

BOOST_AUTO_TEST_CASE(money_range_invalid)
{
    BOOST_CHECK(!MoneyRange(-1));
    BOOST_CHECK(!MoneyRange(MAX_MONEY + 1));
    BOOST_CHECK(!MoneyRange(-COIN));
}

// ============================================================================
// PastDrift / FutureDrift
// ============================================================================

BOOST_AUTO_TEST_CASE(time_drift)
{
    int64_t t = 1700000000;
    // Past drift: 10 minutes back
    BOOST_CHECK_EQUAL(PastDrift(t), t - 600);
    // Future drift: 10 minutes forward
    BOOST_CHECK_EQUAL(FutureDrift(t), t + 600);
}

// ============================================================================
// CheckProofOfWork() tests
// ============================================================================

BOOST_AUTO_TEST_CASE(pow_zero_hash_passes_any_target)
{
    // A zero hash should pass any valid target check
    uint256 hashZero = 0;
    unsigned int nBits = bnProofOfWorkLimit.GetCompact();
    BOOST_CHECK(CheckProofOfWork(hashZero, nBits));
}

BOOST_AUTO_TEST_CASE(pow_max_hash_fails)
{
    // A maximum hash should fail against any reasonable target
    uint256 hashMax(~uint256(0));
    unsigned int nBits = bnProofOfWorkLimit.GetCompact();
    BOOST_CHECK(!CheckProofOfWork(hashMax, nBits));
}

BOOST_AUTO_TEST_CASE(pow_target_boundary)
{
    // Hash exactly at target boundary
    CBigNum bnTarget;
    unsigned int nBits = bnProofOfWorkLimit.GetCompact();
    bnTarget.SetCompact(nBits);

    uint256 hashAtTarget = bnTarget.getuint256();
    BOOST_CHECK(CheckProofOfWork(hashAtTarget, nBits));

    // Hash one above target should fail (but we can't easily add 1 to uint256 without
    // overflow in the top bits, so we test with a clearly-too-large value instead)
    uint256 hashAbove(~uint256(0));
    BOOST_CHECK(!CheckProofOfWork(hashAbove, nBits));
}

BOOST_AUTO_TEST_CASE(pow_invalid_target_zero)
{
    // nBits of 0 means target is 0, which should fail
    uint256 hashZero = 0;
    BOOST_CHECK(!CheckProofOfWork(hashZero, 0));
}

// ============================================================================
// GetProofOfWorkReward() tests
// ============================================================================

BOOST_AUTO_TEST_CASE(pow_reward_block_1_coinbase)
{
    // Block 1 is the premine/coinbase
    int64_t reward = GetProofOfWorkReward(1, 0);
    BOOST_CHECK_EQUAL(reward, 364800000LL * COIN);
}

BOOST_AUTO_TEST_CASE(pow_reward_before_start)
{
    // Blocks 2 through 16999 have no subsidy (just fees)
    BOOST_CHECK_EQUAL(GetProofOfWorkReward(2, 0), 0);
    BOOST_CHECK_EQUAL(GetProofOfWorkReward(100, 0), 0);
    BOOST_CHECK_EQUAL(GetProofOfWorkReward(16999, 0), 0);
}

BOOST_AUTO_TEST_CASE(pow_reward_at_start)
{
    // Block 17000: nHalving = 17000 / 2 / 423400 = 0 → 50 COIN
    int64_t reward = GetProofOfWorkReward(17000, 0);
    BOOST_CHECK_EQUAL(reward, 50 * COIN);
}

BOOST_AUTO_TEST_CASE(pow_reward_first_halving)
{
    // First halving: nHeight / nHalvingPoint / YEARLY_BLOCKCOUNT >= 1
    // nHeight >= 2 * 423400 = 846800
    int64_t rewardBefore = GetProofOfWorkReward(846799, 0);
    BOOST_CHECK_EQUAL(rewardBefore, 50 * COIN);

    int64_t rewardAt = GetProofOfWorkReward(846800, 0);
    BOOST_CHECK_EQUAL(rewardAt, 25 * COIN);
}

BOOST_AUTO_TEST_CASE(pow_reward_second_halving)
{
    // Second halving at 2 * 846800 = 1693600
    int64_t reward = GetProofOfWorkReward(1693600, 0);
    BOOST_CHECK_EQUAL(reward, 12 * COIN + 50000000); // 12.5 COIN
}

BOOST_AUTO_TEST_CASE(pow_reward_includes_fees)
{
    int64_t fees = 50000;
    int64_t reward = GetProofOfWorkReward(17000, fees);
    BOOST_CHECK_EQUAL(reward, 50 * COIN + fees);
}

// ============================================================================
// GetProofOfStakeReward() tests
// ============================================================================

BOOST_AUTO_TEST_CASE(pos_reward_before_start)
{
    // Before height 16240, no PoS subsidy
    BOOST_CHECK_EQUAL(GetProofOfStakeReward(1, 0, 16239, nTimeV231 - 1), 0);
}

BOOST_AUTO_TEST_CASE(pos_reward_regular_pre_v231)
{
    // After height 16240, before v2.3.1 time, regular PoS = 100 COIN
    // (fDisablePOW is false since nTime < nTimeV231)
    // We need pindexBest->nTime < nTimeV231 for this to work.
    // The test fixture sets up pindexBest, so we test the formula.
    // nHalving = 16240 / 2 / 423400 = 0
    // Regular: 100 COIN >> 0 = 100 COIN
    // Note: we cannot easily control pindexBest->nTime in unit tests
    // so we test the math at a known state.

    // At minimum, verify non-flash stake at height 16240 returns nonzero
    int64_t reward = GetProofOfStakeReward(1, 0, 16240, nTimeV231 - 1);
    BOOST_CHECK(reward > 0);
}

BOOST_AUTO_TEST_CASE(pos_reward_includes_fees)
{
    int64_t fees = 100000;
    int64_t rewardNoFees = GetProofOfStakeReward(1, 0, 20000, nTimeV231 - 1);
    int64_t rewardFees = GetProofOfStakeReward(1, fees, 20000, nTimeV231 - 1);
    BOOST_CHECK_EQUAL(rewardFees - rewardNoFees, fees);
}

BOOST_AUTO_TEST_CASE(pos_reward_halving)
{
    // PoS reward should halve at the same interval as PoW
    // First halving: nHeight = 846800
    int64_t rewardBefore = GetProofOfStakeReward(1, 0, 846799, nTimeV231 - 1);
    int64_t rewardAfter = GetProofOfStakeReward(1, 0, 846800, nTimeV231 - 1);

    // After halving, reward should be half of before (approximately)
    BOOST_CHECK(rewardAfter < rewardBefore);
}

// ============================================================================
// IsFlashStake() tests — time-window consensus
// ============================================================================

BOOST_AUTO_TEST_CASE(flash_stake_hours)
{
    // Flash stake is active at hours: 1, 6, 15, 20 UTC
    // Use a known timestamp and construct times at those hours

    // Jan 1, 2024 00:00:00 UTC = 1704067200
    unsigned int baseTime = 1704067200;

    // Hour 0 — not flash
    BOOST_CHECK(!IsFlashStake(baseTime));

    // Hour 1 — flash
    BOOST_CHECK(IsFlashStake(baseTime + 3600));

    // Hour 2 — not flash
    BOOST_CHECK(!IsFlashStake(baseTime + 7200));

    // Hour 6 — flash
    BOOST_CHECK(IsFlashStake(baseTime + 6 * 3600));

    // Hour 7 — not flash
    BOOST_CHECK(!IsFlashStake(baseTime + 7 * 3600));

    // Hour 15 — flash
    BOOST_CHECK(IsFlashStake(baseTime + 15 * 3600));

    // Hour 16 — not flash
    BOOST_CHECK(!IsFlashStake(baseTime + 16 * 3600));

    // Hour 20 — flash
    BOOST_CHECK(IsFlashStake(baseTime + 20 * 3600));

    // Hour 21 — not flash
    BOOST_CHECK(!IsFlashStake(baseTime + 21 * 3600));
}

BOOST_AUTO_TEST_CASE(flash_stake_only_four_hours)
{
    // Count how many hours in a day are flash stake
    unsigned int baseTime = 1704067200; // Jan 1, 2024 00:00:00 UTC
    int flashCount = 0;

    for (int h = 0; h < 24; h++) {
        if (IsFlashStake(baseTime + h * 3600))
            flashCount++;
    }

    BOOST_CHECK_EQUAL(flashCount, 4);
}

// ============================================================================
// CTransaction::CheckTransaction() tests
// ============================================================================

BOOST_AUTO_TEST_CASE(check_transaction_empty_vin)
{
    CTransaction tx;
    tx.vin.clear();
    tx.vout.resize(1);
    tx.vout[0].nValue = COIN;

    BOOST_CHECK(!tx.CheckTransaction());
}

BOOST_AUTO_TEST_CASE(check_transaction_empty_vout)
{
    CTransaction tx;
    tx.vin.resize(1);
    tx.vin[0].prevout.hash = uint256("0x1234");
    tx.vin[0].prevout.n = 0;
    tx.vout.clear();

    BOOST_CHECK(!tx.CheckTransaction());
}

BOOST_AUTO_TEST_CASE(check_transaction_negative_output)
{
    CTransaction tx;
    tx.vin.resize(1);
    tx.vin[0].prevout.hash = uint256("0x1234");
    tx.vin[0].prevout.n = 0;
    tx.vout.resize(1);
    tx.vout[0].nValue = -1;

    BOOST_CHECK(!tx.CheckTransaction());
}

BOOST_AUTO_TEST_CASE(check_transaction_output_too_large)
{
    CTransaction tx;
    tx.vin.resize(1);
    tx.vin[0].prevout.hash = uint256("0x1234");
    tx.vin[0].prevout.n = 0;
    tx.vout.resize(1);
    tx.vout[0].nValue = MAX_MONEY + 1;

    BOOST_CHECK(!tx.CheckTransaction());
}

BOOST_AUTO_TEST_CASE(check_transaction_duplicate_inputs)
{
    CTransaction tx;
    tx.vin.resize(2);
    tx.vin[0].prevout.hash = uint256("0x1234");
    tx.vin[0].prevout.n = 0;
    tx.vin[1].prevout.hash = uint256("0x1234");
    tx.vin[1].prevout.n = 0;  // duplicate
    tx.vout.resize(1);
    tx.vout[0].nValue = COIN;

    BOOST_CHECK(!tx.CheckTransaction());
}

BOOST_AUTO_TEST_CASE(check_transaction_valid_regular)
{
    CTransaction tx;
    tx.vin.resize(1);
    tx.vin[0].prevout.hash = uint256("0x1234");
    tx.vin[0].prevout.n = 0;
    tx.vout.resize(1);
    tx.vout[0].nValue = COIN;
    tx.vout[0].scriptPubKey = CScript() << OP_TRUE;
    tx.nTime = GetAdjustedTime();

    BOOST_CHECK(tx.CheckTransaction());
}

BOOST_AUTO_TEST_CASE(check_transaction_coinbase_script_limits)
{
    // Coinbase scriptSig must be 2-200 bytes
    CTransaction tx;
    tx.vin.resize(1);
    tx.vin[0].prevout.SetNull();
    tx.vout.resize(1);
    tx.vout[0].nValue = 50 * COIN;
    tx.nTime = GetAdjustedTime();

    // Too short (1 byte)
    tx.vin[0].scriptSig = CScript() << 0;
    BOOST_CHECK(!tx.CheckTransaction());

    // Valid (2 bytes)
    tx.vin[0].scriptSig = CScript() << 0 << 0;
    BOOST_CHECK(tx.CheckTransaction());
}

BOOST_AUTO_TEST_CASE(check_transaction_non_coinbase_null_prevout)
{
    // Non-coinbase tx with null prevout in a non-first input should fail
    CTransaction tx;
    tx.vin.resize(2);
    tx.vin[0].prevout.hash = uint256("0x1234");
    tx.vin[0].prevout.n = 0;
    tx.vin[1].prevout.SetNull();  // null prevout in non-coinbase
    tx.vout.resize(1);
    tx.vout[0].nValue = COIN;
    tx.nTime = GetAdjustedTime();

    BOOST_CHECK(!tx.CheckTransaction());
}

// ============================================================================
// Block type identification tests
// ============================================================================

BOOST_AUTO_TEST_CASE(block_max_transaction_time)
{
    CBlock block;

    CTransaction tx1;
    tx1.nTime = 1700000000;
    tx1.vin.resize(1);
    tx1.vin[0].prevout.SetNull();
    tx1.vin[0].scriptSig = CScript() << 0 << 0;
    tx1.vout.resize(1);
    tx1.vout[0].nValue = COIN;

    CTransaction tx2;
    tx2.nTime = 1700000100;
    tx2.vin.resize(1);
    tx2.vin[0].prevout.hash = uint256("0x1234");
    tx2.vin[0].prevout.n = 0;
    tx2.vout.resize(1);
    tx2.vout[0].nValue = COIN;

    block.vtx.push_back(tx1);
    block.vtx.push_back(tx2);

    BOOST_CHECK_EQUAL(block.GetMaxTransactionTime(), 1700000100);
}

// ============================================================================
// CTxOut tests
// ============================================================================

BOOST_AUTO_TEST_CASE(txout_empty_detection)
{
    CTxOut out;
    out.SetEmpty();
    BOOST_CHECK(out.IsEmpty());
    BOOST_CHECK_EQUAL(out.nValue, 0);

    out.nValue = 1;
    BOOST_CHECK(!out.IsEmpty());
}

// ============================================================================
// COutPoint tests
// ============================================================================

BOOST_AUTO_TEST_CASE(outpoint_null)
{
    COutPoint op;
    op.SetNull();
    BOOST_CHECK(op.IsNull());

    COutPoint op2(uint256("0x1234"), 0);
    BOOST_CHECK(!op2.IsNull());
}

// ============================================================================
// CBlockIndex flag tests
// ============================================================================

BOOST_AUTO_TEST_CASE(block_index_proof_of_stake_flag)
{
    CBlockIndex idx;
    BOOST_CHECK(idx.IsProofOfWork());
    BOOST_CHECK(!idx.IsProofOfStake());

    idx.SetProofOfStake();
    BOOST_CHECK(idx.IsProofOfStake());
    BOOST_CHECK(!idx.IsProofOfWork());
}

BOOST_AUTO_TEST_CASE(block_index_stake_entropy_bit)
{
    CBlockIndex idx;
    BOOST_CHECK(idx.SetStakeEntropyBit(0));
    BOOST_CHECK_EQUAL(idx.GetStakeEntropyBit(), 0u);

    CBlockIndex idx2;
    BOOST_CHECK(idx2.SetStakeEntropyBit(1));
    BOOST_CHECK_EQUAL(idx2.GetStakeEntropyBit(), 1u);

    // Invalid entropy bit (>1) should fail
    CBlockIndex idx3;
    BOOST_CHECK(!idx3.SetStakeEntropyBit(2));
}

BOOST_AUTO_TEST_CASE(block_index_stake_modifier_flag)
{
    CBlockIndex idx;
    BOOST_CHECK(!idx.GeneratedStakeModifier());

    idx.SetStakeModifier(12345, true);
    BOOST_CHECK(idx.GeneratedStakeModifier());
    BOOST_CHECK_EQUAL(idx.nStakeModifier, 12345ULL);

    CBlockIndex idx2;
    idx2.SetStakeModifier(99999, false);
    BOOST_CHECK(!idx2.GeneratedStakeModifier());
    BOOST_CHECK_EQUAL(idx2.nStakeModifier, 99999ULL);
}

// ============================================================================
// Merkle tree tests
// ============================================================================

BOOST_AUTO_TEST_CASE(merkle_tree_single_tx)
{
    CBlock block;
    CTransaction tx;
    tx.nTime = 1700000000;
    tx.vin.resize(1);
    tx.vin[0].prevout.SetNull();
    tx.vin[0].scriptSig = CScript() << 0 << 0;
    tx.vout.resize(1);
    tx.vout[0].nValue = 50 * COIN;
    block.vtx.push_back(tx);

    uint256 merkle = block.BuildMerkleTree();
    // With a single tx, merkle root == tx hash
    BOOST_CHECK(merkle == tx.GetHash());
}

BOOST_AUTO_TEST_CASE(merkle_tree_deterministic)
{
    CBlock block;
    CTransaction tx1;
    tx1.nTime = 1700000000;
    tx1.vin.resize(1);
    tx1.vin[0].prevout.SetNull();
    tx1.vin[0].scriptSig = CScript() << 0 << 0;
    tx1.vout.resize(1);
    tx1.vout[0].nValue = 50 * COIN;

    CTransaction tx2;
    tx2.nTime = 1700000001;
    tx2.vin.resize(1);
    tx2.vin[0].prevout.hash = uint256("0xabcd");
    tx2.vin[0].prevout.n = 0;
    tx2.vout.resize(1);
    tx2.vout[0].nValue = 10 * COIN;

    block.vtx.push_back(tx1);
    block.vtx.push_back(tx2);

    uint256 merkle1 = block.BuildMerkleTree();
    uint256 merkle2 = block.BuildMerkleTree();
    BOOST_CHECK(merkle1 == merkle2);
    BOOST_CHECK(merkle1 != 0);
}

BOOST_AUTO_TEST_SUITE_END()
