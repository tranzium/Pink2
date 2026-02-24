// Copyright (c) 2024 The Pinkcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Tests for CBlock::CheckBlock(), CheckBlockSignature(), difficulty
// retargeting (GetNextTargetRequired), and related consensus validation
// functions that are context-independent (no DB or disk state needed).

#include <boost/test/unit_test.hpp>

#include "main.h"
#include "kernel.h"

// bnProofOfWorkLimit, bnProofOfStakeLimit, bnProofOfFlashStakeLimit declared in main.h
extern unsigned int nModifierInterval;

// ---------------------------------------------------------------------------
// Helpers — build minimal valid PoW / PoS blocks for CheckBlock tests
// ---------------------------------------------------------------------------

namespace {

// Build a minimal valid PoW block.  Callers may mutate it to test failures.
// fCheckPOW=false is used for most tests since we can't cheaply mine a
// real scrypt hash.  The block passes CheckBlock(false, true, true).
CBlock MakeMinimalPoWBlock()
{
    CBlock block;
    block.nVersion = CBlock::CURRENT_VERSION;
    block.nTime = GetAdjustedTime();
    block.nBits = bnProofOfWorkLimit.GetCompact();
    block.nNonce = 0;

    // Coinbase transaction
    CTransaction coinbase;
    coinbase.nTime = block.nTime;
    coinbase.vin.resize(1);
    coinbase.vin[0].prevout.SetNull();
    coinbase.vin[0].scriptSig = CScript() << 0 << 0;
    coinbase.vout.resize(1);
    coinbase.vout[0].nValue = 50 * COIN;
    coinbase.vout[0].scriptPubKey = CScript() << OP_TRUE;

    block.vtx.push_back(coinbase);

    // Set correct merkle root
    block.hashMerkleRoot = block.BuildMerkleTree();

    return block;
}

// Build a minimal valid PoS block.  Uses fCheckSig=false to skip the
// block-signature check (which requires a real key pair).
CBlock MakeMinimalPoSBlock()
{
    CBlock block;
    block.nVersion = CBlock::CURRENT_VERSION;
    block.nTime = GetAdjustedTime();
    block.nBits = bnProofOfStakeLimit.GetCompact();
    block.nNonce = 0;

    // Coinbase: single empty output (PoS requirement)
    CTransaction coinbase;
    coinbase.nTime = block.nTime;
    coinbase.vin.resize(1);
    coinbase.vin[0].prevout.SetNull();
    coinbase.vin[0].scriptSig = CScript() << 0 << 0;
    coinbase.vout.resize(1);
    coinbase.vout[0].SetEmpty();

    // Coinstake: first output empty, second output has value
    CTransaction coinstake;
    coinstake.nTime = block.nTime;  // must equal block time
    coinstake.vin.resize(1);
    coinstake.vin[0].prevout.hash = uint256("0xabcdef1234567890abcdef1234567890abcdef1234567890abcdef1234567890");
    coinstake.vin[0].prevout.n = 0;
    coinstake.vout.resize(2);
    coinstake.vout[0].SetEmpty();     // marker for coinstake
    coinstake.vout[1].nValue = 100 * COIN;
    coinstake.vout[1].scriptPubKey = CScript() << OP_TRUE;

    block.vtx.push_back(coinbase);
    block.vtx.push_back(coinstake);

    block.hashMerkleRoot = block.BuildMerkleTree();

    return block;
}

} // anonymous namespace

// ===========================================================================
BOOST_AUTO_TEST_SUITE(checkblock_tests)
// ===========================================================================

// ---- Size limits ---------------------------------------------------------

BOOST_AUTO_TEST_CASE(checkblock_empty_block_fails)
{
    CBlock block;
    // No transactions at all
    BOOST_CHECK(!block.CheckBlock(false, false, false));
}

BOOST_AUTO_TEST_CASE(checkblock_valid_pow_block)
{
    CBlock block = MakeMinimalPoWBlock();
    // Skip PoW check (fCheckPOW=false), check merkle (true), skip sig (false)
    BOOST_CHECK(block.CheckBlock(false, true, false));
}

BOOST_AUTO_TEST_CASE(checkblock_valid_pos_block)
{
    CBlock block = MakeMinimalPoSBlock();
    // Skip PoW check, check merkle, skip block signature
    BOOST_CHECK(block.CheckBlock(false, true, false));
}

// ---- First-tx-is-coinbase -----------------------------------------------

BOOST_AUTO_TEST_CASE(checkblock_first_tx_not_coinbase)
{
    CBlock block = MakeMinimalPoWBlock();
    // Make first tx a non-coinbase by giving it a real prevout
    block.vtx[0].vin[0].prevout.hash = uint256("0x1234");
    block.vtx[0].vin[0].prevout.n = 0;
    block.hashMerkleRoot = block.BuildMerkleTree();

    BOOST_CHECK(!block.CheckBlock(false, true, false));
}

BOOST_AUTO_TEST_CASE(checkblock_multiple_coinbases)
{
    CBlock block = MakeMinimalPoWBlock();

    // Add a second coinbase
    CTransaction coinbase2;
    coinbase2.nTime = block.nTime;
    coinbase2.vin.resize(1);
    coinbase2.vin[0].prevout.SetNull();
    coinbase2.vin[0].scriptSig = CScript() << 1 << 0;
    coinbase2.vout.resize(1);
    coinbase2.vout[0].nValue = 50 * COIN;
    coinbase2.vout[0].scriptPubKey = CScript() << OP_TRUE;

    block.vtx.push_back(coinbase2);
    block.hashMerkleRoot = block.BuildMerkleTree();

    BOOST_CHECK(!block.CheckBlock(false, true, false));
}

// ---- PoS structure checks -----------------------------------------------

BOOST_AUTO_TEST_CASE(checkblock_pos_coinbase_not_empty)
{
    CBlock block = MakeMinimalPoSBlock();
    // Give coinbase a non-empty output (violates PoS rule)
    block.vtx[0].vout[0].nValue = COIN;
    block.vtx[0].vout[0].scriptPubKey = CScript() << OP_TRUE;
    block.hashMerkleRoot = block.BuildMerkleTree();

    BOOST_CHECK(!block.CheckBlock(false, true, false));
}

BOOST_AUTO_TEST_CASE(checkblock_pos_coinbase_multiple_outputs)
{
    CBlock block = MakeMinimalPoSBlock();
    // Add a second output to coinbase (must have exactly 1 empty output)
    block.vtx[0].vout.resize(2);
    block.vtx[0].vout[0].SetEmpty();
    block.vtx[0].vout[1].nValue = COIN;
    block.hashMerkleRoot = block.BuildMerkleTree();

    BOOST_CHECK(!block.CheckBlock(false, true, false));
}

BOOST_AUTO_TEST_CASE(checkblock_pos_like_block_treated_as_pow)
{
    // If vtx[1] is NOT a coinstake, IsProofOfStake() returns false
    // and the block is treated as PoW regardless of coinbase structure.
    // This documents that the "second tx not coinstake" check in
    // CheckBlock (line ~2240) is unreachable dead code because
    // IsProofOfStake() already requires vtx[1].IsCoinStake().
    CBlock block = MakeMinimalPoSBlock();
    // Make vtx[1] a regular tx (not coinstake) by making vout[0] non-empty
    block.vtx[1].vout[0].nValue = 50 * COIN;
    block.vtx[1].vout[0].scriptPubKey = CScript() << OP_TRUE;
    block.hashMerkleRoot = block.BuildMerkleTree();

    // Block is now treated as PoW (IsProofOfStake = false) and passes
    BOOST_CHECK(block.CheckBlock(false, true, false));
    BOOST_CHECK(!block.IsProofOfStake());
    BOOST_CHECK(block.IsProofOfWork());
}

BOOST_AUTO_TEST_CASE(checkblock_pos_multiple_coinstakes)
{
    CBlock block = MakeMinimalPoSBlock();

    // Add a second coinstake
    CTransaction coinstake2;
    coinstake2.nTime = block.nTime;
    coinstake2.vin.resize(1);
    coinstake2.vin[0].prevout.hash = uint256("0x9999");
    coinstake2.vin[0].prevout.n = 1;
    coinstake2.vout.resize(2);
    coinstake2.vout[0].SetEmpty();
    coinstake2.vout[1].nValue = 50 * COIN;
    coinstake2.vout[1].scriptPubKey = CScript() << OP_TRUE;

    block.vtx.push_back(coinstake2);
    block.hashMerkleRoot = block.BuildMerkleTree();

    BOOST_CHECK(!block.CheckBlock(false, true, false));
}

BOOST_AUTO_TEST_CASE(checkblock_pos_coinstake_timestamp_mismatch)
{
    CBlock block = MakeMinimalPoSBlock();
    // Set coinstake time different from block time
    block.vtx[1].nTime = block.nTime - 1;
    block.hashMerkleRoot = block.BuildMerkleTree();

    BOOST_CHECK(!block.CheckBlock(false, true, false));
}

// ---- Transaction validation within block --------------------------------

BOOST_AUTO_TEST_CASE(checkblock_bad_transaction_fails)
{
    CBlock block = MakeMinimalPoWBlock();

    // Add a transaction that fails CheckTransaction (empty vin)
    CTransaction badTx;
    badTx.nTime = block.nTime;
    badTx.vin.clear();
    badTx.vout.resize(1);
    badTx.vout[0].nValue = COIN;

    block.vtx.push_back(badTx);
    block.hashMerkleRoot = block.BuildMerkleTree();

    BOOST_CHECK(!block.CheckBlock(false, true, false));
}

BOOST_AUTO_TEST_CASE(checkblock_tx_timestamp_after_block)
{
    CBlock block = MakeMinimalPoWBlock();

    // Add a regular tx whose timestamp is after the block's
    CTransaction futureTx;
    futureTx.nTime = block.nTime + 1;
    futureTx.vin.resize(1);
    futureTx.vin[0].prevout.hash = uint256("0x5678");
    futureTx.vin[0].prevout.n = 0;
    futureTx.vout.resize(1);
    futureTx.vout[0].nValue = COIN;

    block.vtx.push_back(futureTx);
    block.hashMerkleRoot = block.BuildMerkleTree();

    BOOST_CHECK(!block.CheckBlock(false, true, false));
}

// ---- Duplicate txids ----------------------------------------------------

BOOST_AUTO_TEST_CASE(checkblock_duplicate_txids)
{
    CBlock block = MakeMinimalPoWBlock();

    // Add a second copy of the same coinbase (same hash)
    // We need to craft two distinct coinbases with the same hash — that's
    // impossible, so instead insert the exact same tx object twice.
    // But that would also fail "more than one coinbase", so craft two
    // non-coinbase txs with identical contents.
    CTransaction tx;
    tx.nTime = block.nTime;
    tx.vin.resize(1);
    tx.vin[0].prevout.hash = uint256("0xaaaa");
    tx.vin[0].prevout.n = 0;
    tx.vout.resize(1);
    tx.vout[0].nValue = COIN;

    block.vtx.push_back(tx);
    block.vtx.push_back(tx);  // exact duplicate → same txid
    block.hashMerkleRoot = block.BuildMerkleTree();

    BOOST_CHECK(!block.CheckBlock(false, true, false));
}

// ---- Merkle root mismatch -----------------------------------------------

BOOST_AUTO_TEST_CASE(checkblock_merkle_root_mismatch)
{
    CBlock block = MakeMinimalPoWBlock();
    // Corrupt the merkle root
    block.hashMerkleRoot = uint256("0xdeadbeef");

    BOOST_CHECK(!block.CheckBlock(false, true, false));
}

BOOST_AUTO_TEST_CASE(checkblock_merkle_root_skip)
{
    CBlock block = MakeMinimalPoWBlock();
    // Corrupt merkle root but disable the check
    block.hashMerkleRoot = uint256("0xdeadbeef");

    // fCheckMerkleRoot=false → should still pass
    BOOST_CHECK(block.CheckBlock(false, false, false));
}

// ---- Coinbase timestamp check -------------------------------------------

BOOST_AUTO_TEST_CASE(checkblock_coinbase_timestamp_too_early)
{
    CBlock block = MakeMinimalPoWBlock();
    // Set coinbase time far in the past relative to block time
    // futureLimit = blockTime + 9min (post-block-315065)
    // Check: futureLimit > FutureDrift(vtx[0].nTime)
    // i.e. blockTime + 540 > vtx[0].nTime + 600
    // i.e. vtx[0].nTime < blockTime - 60
    block.vtx[0].nTime = block.nTime - 600;  // 10 minutes before block
    block.hashMerkleRoot = block.BuildMerkleTree();

    BOOST_CHECK(!block.CheckBlock(false, true, false));
}

// ---- CheckBlockSignature PoW -------------------------------------------

BOOST_AUTO_TEST_CASE(check_block_sig_pow_empty_is_valid)
{
    CBlock block = MakeMinimalPoWBlock();
    block.vchBlockSig.clear();
    // PoW block with empty signature → valid
    BOOST_CHECK(block.CheckBlockSignature());
}

BOOST_AUTO_TEST_CASE(check_block_sig_pow_nonempty_is_invalid)
{
    CBlock block = MakeMinimalPoWBlock();
    block.vchBlockSig.push_back(0x01);
    // PoW block with non-empty signature → invalid
    BOOST_CHECK(!block.CheckBlockSignature());
}

// ---- CBlock property tests ----------------------------------------------

BOOST_AUTO_TEST_CASE(block_is_null_when_nbits_zero)
{
    CBlock block;
    block.nBits = 0;
    BOOST_CHECK(block.IsNull());

    block.nBits = 1;
    BOOST_CHECK(!block.IsNull());
}

BOOST_AUTO_TEST_CASE(block_is_proof_of_stake_detection)
{
    // PoW block: only coinbase, no coinstake
    CBlock powBlock = MakeMinimalPoWBlock();
    BOOST_CHECK(powBlock.IsProofOfWork());
    BOOST_CHECK(!powBlock.IsProofOfStake());

    // PoS block: coinbase + coinstake
    CBlock posBlock = MakeMinimalPoSBlock();
    BOOST_CHECK(posBlock.IsProofOfStake());
    BOOST_CHECK(!posBlock.IsProofOfWork());
}

BOOST_AUTO_TEST_CASE(block_get_proof_of_stake)
{
    CBlock posBlock = MakeMinimalPoSBlock();
    auto pos = posBlock.GetProofOfStake();
    // Should return vtx[1].vin[0].prevout and vtx[1].nTime
    BOOST_CHECK(pos.first == posBlock.vtx[1].vin[0].prevout);
    BOOST_CHECK_EQUAL(pos.second, posBlock.vtx[1].nTime);

    // PoW block should return null outpoint and 0
    CBlock powBlock = MakeMinimalPoWBlock();
    auto powPos = powBlock.GetProofOfStake();
    BOOST_CHECK(powPos.first.IsNull());
    BOOST_CHECK_EQUAL(powPos.second, 0u);
}

BOOST_AUTO_TEST_CASE(block_get_block_time)
{
    CBlock block;
    block.nTime = 1700000000;
    BOOST_CHECK_EQUAL(block.GetBlockTime(), 1700000000);
}

BOOST_AUTO_TEST_CASE(block_get_max_transaction_time_empty)
{
    CBlock block;
    BOOST_CHECK_EQUAL(block.GetMaxTransactionTime(), 0);
}

// ---- CTransaction classification ----------------------------------------

BOOST_AUTO_TEST_CASE(tx_is_coinbase)
{
    CTransaction tx;
    tx.vin.resize(1);
    tx.vin[0].prevout.SetNull();
    tx.vout.resize(1);
    tx.vout[0].nValue = COIN;

    BOOST_CHECK(tx.IsCoinBase());
    BOOST_CHECK(!tx.IsCoinStake());
}

BOOST_AUTO_TEST_CASE(tx_is_coinstake)
{
    CTransaction tx;
    tx.vin.resize(1);
    tx.vin[0].prevout.hash = uint256("0x1234");
    tx.vin[0].prevout.n = 0;
    tx.vout.resize(2);
    tx.vout[0].SetEmpty();  // marker
    tx.vout[1].nValue = COIN;

    BOOST_CHECK(tx.IsCoinStake());
    BOOST_CHECK(!tx.IsCoinBase());
}

BOOST_AUTO_TEST_CASE(tx_is_null)
{
    CTransaction tx;
    tx.vin.clear();
    tx.vout.clear();
    BOOST_CHECK(tx.IsNull());

    tx.vin.resize(1);
    BOOST_CHECK(!tx.IsNull());
}

BOOST_AUTO_TEST_CASE(tx_is_newer_than_same)
{
    CTransaction tx1;
    tx1.vin.resize(1);
    tx1.vin[0].prevout.hash = uint256("0x1234");
    tx1.vin[0].prevout.n = 0;
    tx1.vin[0].nSequence = 1;

    CTransaction tx2 = tx1;  // identical

    // Same sequences → not newer
    BOOST_CHECK(!tx1.IsNewerThan(tx2));
}

BOOST_AUTO_TEST_CASE(tx_is_newer_than_different_inputs)
{
    CTransaction tx1, tx2;
    tx1.vin.resize(1);
    tx1.vin[0].prevout.hash = uint256("0x1234");
    tx1.vin[0].prevout.n = 0;

    tx2.vin.resize(1);
    tx2.vin[0].prevout.hash = uint256("0x5678");
    tx2.vin[0].prevout.n = 0;

    // Different inputs → false
    BOOST_CHECK(!tx1.IsNewerThan(tx2));
}

BOOST_AUTO_TEST_CASE(tx_is_newer_than_different_sizes)
{
    CTransaction tx1, tx2;
    tx1.vin.resize(1);
    tx2.vin.resize(2);

    // Different vin sizes → false
    BOOST_CHECK(!tx1.IsNewerThan(tx2));
}

// ---- CTxIn::IsFinal() ---------------------------------------------------

BOOST_AUTO_TEST_CASE(txin_is_final_max_sequence)
{
    CTxIn txin;
    txin.nSequence = std::numeric_limits<unsigned int>::max();
    BOOST_CHECK(txin.IsFinal());
}

BOOST_AUTO_TEST_CASE(txin_not_final_nonmax_sequence)
{
    CTxIn txin;
    txin.nSequence = 0;
    BOOST_CHECK(!txin.IsFinal());

    txin.nSequence = std::numeric_limits<unsigned int>::max() - 1;
    BOOST_CHECK(!txin.IsFinal());
}

// ---- CTransaction::GetValueOut() ----------------------------------------

BOOST_AUTO_TEST_CASE(tx_get_value_out_single)
{
    CTransaction tx;
    tx.vout.resize(1);
    tx.vout[0].nValue = 42 * COIN;
    BOOST_CHECK_EQUAL(tx.GetValueOut(), 42 * COIN);
}

BOOST_AUTO_TEST_CASE(tx_get_value_out_multiple)
{
    CTransaction tx;
    tx.vout.resize(3);
    tx.vout[0].nValue = 10 * COIN;
    tx.vout[1].nValue = 20 * COIN;
    tx.vout[2].nValue = 30 * COIN;
    BOOST_CHECK_EQUAL(tx.GetValueOut(), 60 * COIN);
}

BOOST_AUTO_TEST_CASE(tx_get_value_out_overflow_throws)
{
    CTransaction tx;
    tx.vout.resize(2);
    tx.vout[0].nValue = MAX_MONEY;
    tx.vout[1].nValue = 1;  // exceeds MAX_MONEY total

    BOOST_CHECK_THROW(tx.GetValueOut(), std::runtime_error);
}

// ---- CDiskTxPos ----------------------------------------------------------

BOOST_AUTO_TEST_CASE(disk_tx_pos_null)
{
    CDiskTxPos pos;
    BOOST_CHECK(pos.IsNull());

    CDiskTxPos pos2(1, 100, 200);
    BOOST_CHECK(!pos2.IsNull());
}

BOOST_AUTO_TEST_CASE(disk_tx_pos_equality)
{
    CDiskTxPos a(1, 100, 200);
    CDiskTxPos b(1, 100, 200);
    CDiskTxPos c(2, 100, 200);

    BOOST_CHECK(a == b);
    BOOST_CHECK(a != c);
}

// ---- CInPoint / COutPoint -----------------------------------------------

BOOST_AUTO_TEST_CASE(inpoint_null)
{
    CInPoint ip;
    BOOST_CHECK(ip.IsNull());

    CTransaction tx;
    CInPoint ip2(&tx, 0);
    BOOST_CHECK(!ip2.IsNull());
}

BOOST_AUTO_TEST_CASE(outpoint_ordering)
{
    COutPoint a(uint256("0x1000"), 0);
    COutPoint b(uint256("0x2000"), 0);
    COutPoint c(uint256("0x1000"), 1);

    BOOST_CHECK(a < b);
    BOOST_CHECK(a < c);
    BOOST_CHECK(!(b < a));
}

// ---- CTxOut properties ---------------------------------------------------

BOOST_AUTO_TEST_CASE(txout_null_detection)
{
    CTxOut out;
    BOOST_CHECK(out.IsNull());   // default nValue = -1

    out.nValue = 0;
    BOOST_CHECK(!out.IsNull());  // 0 is not null (-1 is)
}

BOOST_AUTO_TEST_CASE(txout_get_hash_deterministic)
{
    CTxOut a(COIN, CScript() << OP_TRUE);
    CTxOut b(COIN, CScript() << OP_TRUE);

    BOOST_CHECK(a.GetHash() == b.GetHash());
    BOOST_CHECK(a.GetHash() != 0);
}

// ---- CTransaction::GetLegacySigOpCount() --------------------------------

BOOST_AUTO_TEST_CASE(legacy_sigop_count_simple)
{
    CTransaction tx;
    tx.vin.resize(1);
    tx.vin[0].prevout.hash = uint256("0x1234");
    tx.vin[0].prevout.n = 0;
    tx.vin[0].scriptSig = CScript();  // no sigops in input
    tx.vout.resize(1);
    tx.vout[0].nValue = COIN;
    tx.vout[0].scriptPubKey = CScript() << OP_TRUE;  // no CHECKSIG

    BOOST_CHECK_EQUAL(tx.GetLegacySigOpCount(), 0u);
}

BOOST_AUTO_TEST_CASE(legacy_sigop_count_checksig)
{
    CTransaction tx;
    tx.vin.resize(1);
    tx.vin[0].prevout.hash = uint256("0x1234");
    tx.vin[0].prevout.n = 0;
    tx.vout.resize(1);
    tx.vout[0].nValue = COIN;
    // OP_CHECKSIG counts as 1 sigop
    tx.vout[0].scriptPubKey = CScript() << OP_DUP << OP_HASH160
        << std::vector<unsigned char>(20, 0) << OP_EQUALVERIFY << OP_CHECKSIG;

    BOOST_CHECK_EQUAL(tx.GetLegacySigOpCount(), 1u);
}

BOOST_AUTO_TEST_CASE(legacy_sigop_count_checkmultisig)
{
    CTransaction tx;
    tx.vin.resize(1);
    tx.vin[0].prevout.hash = uint256("0x1234");
    tx.vin[0].prevout.n = 0;
    tx.vout.resize(1);
    tx.vout[0].nValue = COIN;
    // Bare CHECKMULTISIG counts as 20 sigops (MAX_PUBKEYS_PER_MULTISIG)
    tx.vout[0].scriptPubKey = CScript() << OP_CHECKMULTISIG;

    BOOST_CHECK_EQUAL(tx.GetLegacySigOpCount(), 20u);
}

// ---- CTransaction::IsStandard() -----------------------------------------

BOOST_AUTO_TEST_CASE(tx_is_standard_simple)
{
    CTransaction tx;
    tx.nVersion = CTransaction::CURRENT_VERSION;
    tx.nTime = GetAdjustedTime();
    tx.vin.resize(1);
    tx.vin[0].prevout.hash = uint256("0x1234");
    tx.vin[0].prevout.n = 0;
    tx.vin[0].scriptSig = CScript() << std::vector<unsigned char>(72, 0x30);  // push-only
    tx.vout.resize(1);
    tx.vout[0].nValue = COIN;
    // Pay-to-pubkey-hash output
    tx.vout[0].scriptPubKey = CScript() << OP_DUP << OP_HASH160
        << std::vector<unsigned char>(20, 0xaa) << OP_EQUALVERIFY << OP_CHECKSIG;

    BOOST_CHECK(tx.IsStandard());
}

BOOST_AUTO_TEST_CASE(tx_is_standard_future_version_rejected)
{
    CTransaction tx;
    tx.nVersion = CTransaction::CURRENT_VERSION + 2;  // too high
    tx.nTime = GetAdjustedTime();
    tx.vin.resize(1);
    tx.vin[0].prevout.hash = uint256("0x1234");
    tx.vin[0].prevout.n = 0;
    tx.vin[0].scriptSig = CScript() << std::vector<unsigned char>(72, 0x30);
    tx.vout.resize(1);
    tx.vout[0].nValue = COIN;
    tx.vout[0].scriptPubKey = CScript() << OP_DUP << OP_HASH160
        << std::vector<unsigned char>(20, 0xaa) << OP_EQUALVERIFY << OP_CHECKSIG;

    BOOST_CHECK(!tx.IsStandard());
}

BOOST_AUTO_TEST_CASE(tx_is_standard_large_scriptsig_rejected)
{
    CTransaction tx;
    tx.nVersion = CTransaction::CURRENT_VERSION;
    tx.nTime = GetAdjustedTime();
    tx.vin.resize(1);
    tx.vin[0].prevout.hash = uint256("0x1234");
    tx.vin[0].prevout.n = 0;
    // scriptSig > 500 bytes → non-standard
    tx.vin[0].scriptSig = CScript() << std::vector<unsigned char>(501, 0x00);
    tx.vout.resize(1);
    tx.vout[0].nValue = COIN;
    tx.vout[0].scriptPubKey = CScript() << OP_DUP << OP_HASH160
        << std::vector<unsigned char>(20, 0xaa) << OP_EQUALVERIFY << OP_CHECKSIG;

    BOOST_CHECK(!tx.IsStandard());
}

BOOST_AUTO_TEST_CASE(tx_is_standard_zero_value_output_rejected)
{
    CTransaction tx;
    tx.nVersion = CTransaction::CURRENT_VERSION;
    tx.nTime = GetAdjustedTime();
    tx.vin.resize(1);
    tx.vin[0].prevout.hash = uint256("0x1234");
    tx.vin[0].prevout.n = 0;
    tx.vin[0].scriptSig = CScript() << std::vector<unsigned char>(72, 0x30);
    tx.vout.resize(1);
    tx.vout[0].nValue = 0;  // zero value non-OP_RETURN → non-standard
    tx.vout[0].scriptPubKey = CScript() << OP_DUP << OP_HASH160
        << std::vector<unsigned char>(20, 0xaa) << OP_EQUALVERIFY << OP_CHECKSIG;

    BOOST_CHECK(!tx.IsStandard());
}

// ---- CTxMemPool ---------------------------------------------------------

BOOST_AUTO_TEST_CASE(mempool_empty_operations)
{
    CTxMemPool pool;
    BOOST_CHECK_EQUAL(pool.size(), 0u);
    BOOST_CHECK(!pool.exists(uint256("0xabc")));

    CTransaction result;
    BOOST_CHECK(!pool.lookup(uint256("0xabc"), result));
}

BOOST_AUTO_TEST_CASE(mempool_add_unchecked)
{
    CTxMemPool pool;
    CTransaction tx;
    tx.nTime = GetAdjustedTime();
    tx.vin.resize(1);
    tx.vin[0].prevout.hash = uint256("0x1234");
    tx.vin[0].prevout.n = 0;
    tx.vout.resize(1);
    tx.vout[0].nValue = COIN;

    uint256 hash = tx.GetHash();
    pool.addUnchecked(hash, tx);

    BOOST_CHECK_EQUAL(pool.size(), 1u);
    BOOST_CHECK(pool.exists(hash));

    CTransaction result;
    BOOST_CHECK(pool.lookup(hash, result));
    BOOST_CHECK(result.GetHash() == hash);

    // Clear
    pool.clear();
    BOOST_CHECK_EQUAL(pool.size(), 0u);
}

// ---- GetNextTargetRequired: genesis/early chain -------------------------

BOOST_AUTO_TEST_CASE(difficulty_retarget_genesis_returns_limit)
{
    // When pindexLast is nullptr or very early chain, should return limit
    // V1: if pindexLast == nullptr → returns bnTargetLimit
    // We can't pass nullptr to GetNextTargetRequired because V1 is checked
    // via pindexLast->nHeight, but we can construct a minimal chain.

    // Build a 1-block chain at height 0 (genesis-like)
    CBlockIndex genesis;
    genesis.nHeight = 0;
    genesis.pprev = nullptr;
    genesis.nFlags = 0;  // PoW
    genesis.nBits = bnProofOfWorkLimit.GetCompact();
    genesis.nTime = 1371387277;  // Pinkcoin genesis time

    // V1 path (height < 817990): pindexPrev = GetLastBlockIndex → genesis
    // genesis->pprev == nullptr → return bnTargetLimit
    unsigned int result = GetNextTargetRequired(&genesis, false, genesis.nTime);
    BOOST_CHECK_EQUAL(result, bnProofOfWorkLimit.GetCompact());
}

BOOST_AUTO_TEST_CASE(difficulty_retarget_single_block_returns_limit)
{
    // Two-block chain: prev has prev → second block
    // But GetLastBlockIndex walks to genesis, genesis->pprev is nullptr
    // → returns limit
    CBlockIndex idx0, idx1;
    idx0.nHeight = 0;
    idx0.pprev = nullptr;
    idx0.nFlags = 0;
    idx0.nBits = bnProofOfWorkLimit.GetCompact();
    idx0.nTime = 1371387277;

    idx1.nHeight = 1;
    idx1.pprev = &idx0;
    idx1.nFlags = 0;
    idx1.nBits = bnProofOfWorkLimit.GetCompact();
    idx1.nTime = idx0.nTime + 120;

    unsigned int result = GetNextTargetRequired(&idx1, false, idx1.nTime + 120);
    BOOST_CHECK_EQUAL(result, bnProofOfWorkLimit.GetCompact());
}

BOOST_AUTO_TEST_CASE(difficulty_retarget_pos_returns_stake_limit)
{
    // PoS with early chain → returns bnProofOfStakeLimit
    CBlockIndex idx0;
    idx0.nHeight = 0;
    idx0.pprev = nullptr;
    idx0.SetProofOfStake();
    idx0.nBits = bnProofOfStakeLimit.GetCompact();
    idx0.nTime = 1371387277;

    unsigned int result = GetNextTargetRequired(&idx0, true, 1704067200);
    BOOST_CHECK_EQUAL(result, bnProofOfStakeLimit.GetCompact());
}

BOOST_AUTO_TEST_CASE(difficulty_retarget_flash_stake_returns_flash_limit)
{
    // Flash PoS with early chain → returns bnProofOfFlashStakeLimit
    CBlockIndex idx0;
    idx0.nHeight = 0;
    idx0.pprev = nullptr;
    idx0.SetProofOfStake();
    idx0.nBits = bnProofOfFlashStakeLimit.GetCompact();
    idx0.nTime = 1371387277;

    // Hour 1 UTC → flash stake
    unsigned int flashTime = 1704067200 + 3600;
    unsigned int result = GetNextTargetRequired(&idx0, true, flashTime);
    BOOST_CHECK_EQUAL(result, bnProofOfFlashStakeLimit.GetCompact());
}

// ---- Difficulty retarget with 3+ block chain ----------------------------

BOOST_AUTO_TEST_CASE(difficulty_retarget_v1_adjusts)
{
    // Build a 3-block PoW chain at early heights (< 817990)
    // Verify the retarget formula produces a different result when
    // actual spacing differs from target spacing
    CBlockIndex idx[3];
    for (int i = 0; i < 3; i++) {
        idx[i].nHeight = i;
        idx[i].pprev = (i > 0) ? &idx[i-1] : nullptr;
        idx[i].nFlags = 0; // PoW
        idx[i].nBits = bnProofOfWorkLimit.GetCompact();
    }
    // Set timestamps: target spacing is 120s
    idx[0].nTime = 1371387277;
    idx[1].nTime = idx[0].nTime + 120;   // normal spacing
    idx[2].nTime = idx[1].nTime + 120;   // normal spacing

    unsigned int normalResult = GetNextTargetRequired(&idx[2], false, idx[2].nTime + 120);

    // Now with double spacing
    idx[2].nTime = idx[1].nTime + 240;  // 2x target spacing
    unsigned int slowResult = GetNextTargetRequired(&idx[2], false, idx[2].nTime + 120);

    // Slower blocks → target should be easier (larger/higher compact value)
    // Both may be capped at bnProofOfWorkLimit, but the formula should
    // produce a result (even if capped)
    BOOST_CHECK(slowResult >= normalResult);
}

BOOST_AUTO_TEST_CASE(difficulty_retarget_v1_fast_blocks_harder)
{
    // Build 3-block PoW chain, fast block timing → harder difficulty
    CBlockIndex idx[3];
    for (int i = 0; i < 3; i++) {
        idx[i].nHeight = i;
        idx[i].pprev = (i > 0) ? &idx[i-1] : nullptr;
        idx[i].nFlags = 0;
    }
    // Use a moderately difficult target (not at limit)
    arith_uint256 bnModerate = bnProofOfWorkLimit;
    bnModerate >>= 4;  // 16x harder than limit
    unsigned int moderateBits = bnModerate.GetCompact();

    idx[0].nTime = 1371387277;
    idx[0].nBits = moderateBits;
    idx[1].nTime = idx[0].nTime + 120;
    idx[1].nBits = moderateBits;
    idx[2].nTime = idx[1].nTime + 120;
    idx[2].nBits = moderateBits;

    // Normal spacing result
    unsigned int normalResult = GetNextTargetRequired(&idx[2], false, idx[2].nTime + 120);

    // Fast blocks (30s instead of 120s)
    idx[2].nTime = idx[1].nTime + 30;
    unsigned int fastResult = GetNextTargetRequired(&idx[2], false, idx[2].nTime + 120);

    // Faster blocks → target should be harder (smaller/lower compact value)
    arith_uint256 bnNormal, bnFast;
    bnNormal.SetCompact(normalResult);
    bnFast.SetCompact(fastResult);
    BOOST_CHECK(bnFast <= bnNormal);
}

// ---- V1 → V2 fork boundary ---------------------------------------------

BOOST_AUTO_TEST_CASE(difficulty_retarget_version_switch)
{
    // Height 817989 → V1, height 817990 → V2
    CBlockIndex idx0, idx1, idx2;
    idx0.nHeight = 817987;
    idx0.pprev = nullptr;
    idx0.nFlags = 0;
    idx0.nBits = bnProofOfWorkLimit.GetCompact();
    idx0.nTime = 1540000000;

    idx1.nHeight = 817988;
    idx1.pprev = &idx0;
    idx1.nFlags = 0;
    idx1.nBits = bnProofOfWorkLimit.GetCompact();
    idx1.nTime = idx0.nTime + 120;

    idx2.nHeight = 817989;
    idx2.pprev = &idx1;
    idx2.nFlags = 0;
    idx2.nBits = bnProofOfWorkLimit.GetCompact();
    idx2.nTime = idx1.nTime + 120;

    // Height 817989 < 817990 → uses V1
    unsigned int v1Result = GetNextTargetRequired(&idx2, false, idx2.nTime + 120);

    // Now at height 817990 → uses V2
    idx2.nHeight = 817990;
    unsigned int v2Result = GetNextTargetRequired(&idx2, false, idx2.nTime + 120);

    // Both should return valid compact targets
    BOOST_CHECK(v1Result != 0);
    BOOST_CHECK(v2Result != 0);
    // Note: results may differ since V2 uses independent PoS/FPoS spacing
}

// ---- CBlockIndex flag combination tests ---------------------------------

BOOST_AUTO_TEST_CASE(block_index_is_fpos)
{
    // IsFPOS returns true only for PoS blocks where IsFlashStake(nTime) matches
    CBlockIndex idx;
    idx.SetProofOfStake();
    idx.nTime = 1704067200 + 3600;  // hour 1 → flash

    BOOST_CHECK(idx.IsFPOS(true));   // asking for flash = true
    BOOST_CHECK(!idx.IsFPOS(false)); // asking for non-flash = false

    // PoW block → never FPOS
    CBlockIndex powIdx;
    powIdx.nTime = 1704067200 + 3600;
    BOOST_CHECK(!powIdx.IsFPOS(true));
    BOOST_CHECK(!powIdx.IsFPOS(false));
}

BOOST_AUTO_TEST_CASE(block_index_check_index_always_true)
{
    // CheckIndex() is a stub that always returns true
    CBlockIndex idx;
    BOOST_CHECK(idx.CheckIndex());
}

BOOST_AUTO_TEST_CASE(block_index_get_past_time_limit)
{
    // GetPastTimeLimit() should equal GetMedianTimePast()
    CBlockIndex chain[3];
    chain[0].nTime = 100;
    chain[0].pprev = nullptr;
    chain[1].nTime = 200;
    chain[1].pprev = &chain[0];
    chain[2].nTime = 300;
    chain[2].pprev = &chain[1];

    BOOST_CHECK_EQUAL(chain[2].GetPastTimeLimit(), chain[2].GetMedianTimePast());
}

// ---- CBlockIndex construction from CBlock --------------------------------

BOOST_AUTO_TEST_CASE(block_index_from_pow_block)
{
    CBlock block = MakeMinimalPoWBlock();
    CBlockIndex idx(0, 0, block);

    BOOST_CHECK(idx.IsProofOfWork());
    BOOST_CHECK(!idx.IsProofOfStake());
    BOOST_CHECK(idx.prevoutStake.IsNull());
    BOOST_CHECK_EQUAL(idx.nStakeTime, 0u);
    BOOST_CHECK_EQUAL(idx.nVersion, block.nVersion);
    BOOST_CHECK(idx.hashMerkleRoot == block.hashMerkleRoot);
    BOOST_CHECK_EQUAL(idx.nTime, block.nTime);
    BOOST_CHECK_EQUAL(idx.nBits, block.nBits);
}

BOOST_AUTO_TEST_CASE(block_index_from_pos_block)
{
    CBlock block = MakeMinimalPoSBlock();
    CBlockIndex idx(0, 0, block);

    BOOST_CHECK(idx.IsProofOfStake());
    BOOST_CHECK(!idx.IsProofOfWork());
    BOOST_CHECK(idx.prevoutStake == block.vtx[1].vin[0].prevout);
    BOOST_CHECK_EQUAL(idx.nStakeTime, block.vtx[1].nTime);
}

// ---- CBlock::GetHash caching -------------------------------------------

BOOST_AUTO_TEST_CASE(block_hash_cached_matches_uncached)
{
    CBlock block = MakeMinimalPoWBlock();

    uint256 uncached = block.GetHash(false);
    uint256 cached = block.GetHash(true);
    uint256 cached2 = block.GetHash(true);  // second call uses cache

    BOOST_CHECK(uncached == cached);
    BOOST_CHECK(cached == cached2);
    BOOST_CHECK(uncached != 0);
}

// ---- CBlockLocator (basic) ----------------------------------------------

BOOST_AUTO_TEST_CASE(block_locator_null)
{
    CBlockLocator loc;
    BOOST_CHECK(loc.IsNull());
}

// ---- CTxIndex -----------------------------------------------------------

BOOST_AUTO_TEST_CASE(tx_index_null)
{
    CTxIndex idx;
    BOOST_CHECK(idx.IsNull());

    CDiskTxPos pos(1, 100, 200);
    CTxIndex idx2(pos, 3);
    BOOST_CHECK(!idx2.IsNull());
    BOOST_CHECK_EQUAL(idx2.vSpent.size(), 3u);
}

BOOST_AUTO_TEST_CASE(tx_index_equality)
{
    CDiskTxPos pos(1, 100, 200);
    CTxIndex a(pos, 2);
    CTxIndex b(pos, 2);
    BOOST_CHECK(a == b);

    CTxIndex c(CDiskTxPos(1, 100, 300), 2);
    BOOST_CHECK(a != c);
}

BOOST_AUTO_TEST_SUITE_END()
