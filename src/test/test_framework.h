// Copyright (c) 2024-2026 The Pinkcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Integration test framework providing a reusable mineable chain fixture.
// Mines real blocks via ProcessBlock() with temporarily lowered PoW
// difficulty so that valid nonces can be found quickly.

#ifndef PINKCOIN_TEST_FRAMEWORK_H
#define PINKCOIN_TEST_FRAMEWORK_H

#include "main.h"
#include "miner.h"
#include "wallet.h"

#include <vector>

extern CWallet* pwalletMain;

// ---------------------------------------------------------------------------
// TestChain — reusable fixture that mines a PoW chain via ProcessBlock().
//
// Usage:
//   BOOST_FIXTURE_TEST_SUITE(my_tests, TestChain)
//   BOOST_AUTO_TEST_CASE(some_test) {
//       // chainHeight() returns current tip height
//       // coinbaseTxns[0] is the coinbase of the first block mined
//       // MineEmptyBlocks(n) mines n more blocks on top
//   }
//   BOOST_AUTO_TEST_SUITE_END()
//
// The fixture ensures at least nDefaultBlocks (50) blocks exist.
// If another test suite already mined blocks (global state persists),
// the fixture loads coinbase transactions from existing blocks instead
// of mining new ones.
//
// PoW difficulty is temporarily lowered during mining so that valid
// nonces can be found in 1-4 scrypt hashes per block.
// ---------------------------------------------------------------------------
struct TestChain
{
    static const unsigned int nDefaultBlocks = 50;

    // Coinbase transactions from blocks available to this fixture.
    // coinbaseTxns[i] is the coinbase of block at height (nBaseHeight + i + 1).
    std::vector<CTransaction> coinbaseTxns;

    // Height of the chain when this fixture was constructed (before mining).
    // After mining (or loading), coinbaseTxns[0] is from block nBaseHeight+1.
    int nBaseHeight;

    // Number of blocks this fixture instance mined.
    unsigned int nBlocksMined;

    // Tracks total blocks mined across all TestChain instances (for bookkeeping).
    static unsigned int nNextNonceIndex;

    // Shared coinbase transactions from first mining pass.
    // Populated once; reused by subsequent fixture instances.
    static std::vector<CTransaction> s_coinbaseTxns;

    // -----------------------------------------------------------------------
    // Construction / destruction
    // -----------------------------------------------------------------------
    explicit TestChain(unsigned int nBlocks = nDefaultBlocks);
    ~TestChain();

    // -----------------------------------------------------------------------
    // Chain queries
    // -----------------------------------------------------------------------
    int chainHeight() const;
    CBlockIndex* chainTip() const;
    CBlockIndex* blockIndexAt(int nHeight) const;
    int64_t mintAt(int nHeight) const;
    int64_t moneySupply() const;

    // -----------------------------------------------------------------------
    // Mining helpers
    // -----------------------------------------------------------------------
    unsigned int MineEmptyBlocks(unsigned int nCount);
    bool MineOneBlock();

    // -----------------------------------------------------------------------
    // Transaction helpers
    // -----------------------------------------------------------------------

    // Create a transaction spending coinbaseTxns[nCoinbaseIndex] output 0.
    // The coinbase has empty scriptPubKey, so scriptSig = OP_1 suffices.
    // Sends nAmount to scriptPubKey; change (if any) goes to empty script.
    CTransaction CreateSpendTx(unsigned int nCoinbaseIndex,
                               const CScript& scriptPubKey,
                               int64_t nAmount,
                               int64_t nFee = MIN_TX_FEE) const;

    // -----------------------------------------------------------------------
    // Mempool helpers
    // -----------------------------------------------------------------------
    bool AddToMempool(CTransaction& tx);
    bool SubmitToMempool(CTransaction& tx);
    void ClearMempool();

    // -----------------------------------------------------------------------
    // Utility
    // -----------------------------------------------------------------------
    bool IsCoinbaseMature(unsigned int nCoinbaseIndex) const;
};

#endif // PINKCOIN_TEST_FRAMEWORK_H
