// Copyright (c) 2024-2026 The Pinkcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "test_framework.h"
#include "arith_uint256.h"
#include "txdb.h"

// bnProofOfWorkLimit declared in main.h

unsigned int TestChain::nNextNonceIndex = 0;
std::vector<CTransaction> TestChain::s_coinbaseTxns;

// ---------------------------------------------------------------------------
// Helper: mine a single block with brute-force nonce search.
//
// Temporarily lowers bnProofOfWorkLimit to a very easy target so that
// valid nonces can be found quickly (typically 1-4 scrypt hashes).
// Returns true if the block was processed successfully.
// ---------------------------------------------------------------------------
static bool MineAndProcessBlock(CBlock* pblock)
{
    uint256 hashTarget = ArithToUint256(arith_uint256().SetCompact(pblock->nBits));

    pblock->nNonce = 0;
    while (pblock->GetPoWHash() > hashTarget) {
        pblock->nNonce++;
        if (pblock->nNonce > 10000)
            return false;
    }

    return ProcessBlock(NULL, pblock);
}

// ---------------------------------------------------------------------------
// RAII guard for temporarily lowering PoW difficulty.
// ---------------------------------------------------------------------------
struct EasyPoW {
    arith_uint256 bnOrigLimit;

    EasyPoW() : bnOrigLimit(bnProofOfWorkLimit) {
        // Set easiest target: only top 2 bits must be zero.
        // ~50% of hashes pass on each try — typically finds a valid
        // nonce in 1-3 attempts.
        bnProofOfWorkLimit = UintToArith256(~uint256(0) >> 2);
    }

    ~EasyPoW() {
        bnProofOfWorkLimit = bnOrigLimit;
    }
};

// ---------------------------------------------------------------------------
// Constructor: mine nBlocks blocks onto the existing chain.
// ---------------------------------------------------------------------------
TestChain::TestChain(unsigned int nBlocks)
    : nBaseHeight(0), nBlocksMined(0)
{
    nBaseHeight = pindexBest ? pindexBest->nHeight : 0;

    // Calculate how many more blocks we need to mine.
    unsigned int nTarget = nBlocks;
    unsigned int nToMine = 0;
    if (static_cast<unsigned int>(nBaseHeight) < nTarget)
        nToMine = nTarget - static_cast<unsigned int>(nBaseHeight);

    if (nToMine == 0) {
        if (!s_coinbaseTxns.empty()) {
            // Fast path: reuse cached coinbase transactions.
            coinbaseTxns = s_coinbaseTxns;
            nBaseHeight = 0;
            nBlocksMined = 0;
            return;
        }

        // Blocks exist (e.g. from a previous test run) but we haven't
        // cached their coinbase transactions.  Try loading from disk.
        for (int h = 1; h <= nBaseHeight && h <= static_cast<int>(nTarget); ++h) {
            CBlockIndex* pindex = FindBlockByHeight(h);
            if (!pindex) break;
            CBlock block;
            if (!block.ReadFromDisk(pindex)) break;
            if (!block.vtx.empty())
                coinbaseTxns.push_back(block.vtx[0]);
        }
        if (!coinbaseTxns.empty()) {
            s_coinbaseTxns = coinbaseTxns;
            nBaseHeight = 0;
            nBlocksMined = 0;
            return;
        }

        // ReadFromDisk failed.  Mine fresh blocks on top of the
        // existing chain so we have coinbase outputs to work with.
        nToMine = nBlocks;
    }

    // Lower PoW difficulty so we can find valid nonces quickly.
    EasyPoW guard;

    // Reset the key pool — other test suites may have left stale entries.
    // In particular, wallet_encryption_lifecycle's EncryptWallet call on a
    // separate CWallet contaminates the shared mock BDB memory pool,
    // causing pwalletMain's pool reads to return compressed keys from the
    // test wallet even though pwalletMain only generates uncompressed keys.
    pwalletMain->NewKeyPool();

    for (unsigned int i = 0; i < nToMine; ++i)
    {
        CBlock* pblock = CreateNewBlock(pwalletMain.get());
        if (!pblock)
            break;

        pblock->nVersion = 1;
        pblock->nTime = pindexBest->GetMedianTimePast() + 1;

        // Coinbase scriptSig must start with serialized block height
        // (enforced by AcceptBlock) and be at least 2 bytes (CheckTransaction).
        {
            int nHeight = pindexBest->nHeight + 1;
            CScript scriptSig = CScript() << nHeight;
            while (scriptSig.size() < 2)
                scriptSig.push_back(0xFF);  // pad to minimum 2 bytes
            pblock->vtx[0].vin[0].scriptSig = scriptSig;
        }
        pblock->vtx[0].vout[0].scriptPubKey = CScript();

        // Make coinbase nTime deterministic (independent of wall clock).
        pblock->vtx[0].nTime = static_cast<unsigned int>(pblock->nTime);

        coinbaseTxns.push_back(pblock->vtx[0]);

        pblock->hashMerkleRoot = pblock->BuildMerkleTree();

        if (!MineAndProcessBlock(pblock)) {
            delete pblock;
            break;
        }

        delete pblock;
    }

    // Cache coinbase transactions for subsequent fixture instances.
    if (s_coinbaseTxns.empty())
        s_coinbaseTxns = coinbaseTxns;

    nBlocksMined = static_cast<unsigned int>(chainHeight()) - static_cast<unsigned int>(nBaseHeight);
    nNextNonceIndex += nBlocksMined;
}

TestChain::~TestChain()
{
    mempool.clear();
}

// ---------------------------------------------------------------------------
// Chain queries
// ---------------------------------------------------------------------------

int TestChain::chainHeight() const
{
    return pindexBest ? pindexBest->nHeight : 0;
}

CBlockIndex* TestChain::chainTip() const
{
    return pindexBest;
}

CBlockIndex* TestChain::blockIndexAt(int nHeight) const
{
    return FindBlockByHeight(nHeight);
}

int64_t TestChain::mintAt(int nHeight) const
{
    CBlockIndex* pindex = FindBlockByHeight(nHeight);
    return pindex ? pindex->nMint : 0;
}

int64_t TestChain::moneySupply() const
{
    return pindexBest ? pindexBest->nMoneySupply : 0;
}

// ---------------------------------------------------------------------------
// Mining helpers
// ---------------------------------------------------------------------------

unsigned int TestChain::MineEmptyBlocks(unsigned int nCount)
{
    mempool.clear();

    EasyPoW guard;

    // Reset the key pool — BDB 4.8 mock mode (DB_MPOOL_NOFILE) allows
    // cross-contamination between anonymous in-memory databases when
    // wallet_encryption_lifecycle's EncryptWallet writes compressed keys
    // to test_encrypt.dat, corrupting pwalletMain's pool reads.
    pwalletMain->NewKeyPool();

    unsigned int nMined = 0;
    for (unsigned int i = 0; i < nCount; ++i)
    {
        CBlock* pblock = CreateNewBlock(pwalletMain.get());
        if (!pblock)
            break;

        pblock->nVersion = 1;
        pblock->nTime = pindexBest->GetMedianTimePast() + 1;

        {
            int nHeight = pindexBest->nHeight + 1;
            CScript scriptSig = CScript() << nHeight;
            while (scriptSig.size() < 2)
                scriptSig.push_back(0xFF);
            pblock->vtx[0].vin[0].scriptSig = scriptSig;
        }
        pblock->vtx[0].vout[0].scriptPubKey = CScript();
        pblock->vtx[0].nTime = static_cast<unsigned int>(pblock->nTime);

        coinbaseTxns.push_back(pblock->vtx[0]);

        pblock->hashMerkleRoot = pblock->BuildMerkleTree();

        if (!MineAndProcessBlock(pblock)) {
            delete pblock;
            break;
        }

        delete pblock;

        ++nMined;
        ++nBlocksMined;
        ++nNextNonceIndex;
    }

    return nMined;
}

bool TestChain::MineOneBlock()
{
    return MineEmptyBlocks(1) == 1;
}

// ---------------------------------------------------------------------------
// Transaction helpers
// ---------------------------------------------------------------------------

CTransaction TestChain::CreateSpendTx(unsigned int nCoinbaseIndex,
                                       const CScript& scriptPubKey,
                                       int64_t nAmount,
                                       int64_t nFee) const
{
    assert(nCoinbaseIndex < coinbaseTxns.size());
    const CTransaction& coinbaseTx = coinbaseTxns[nCoinbaseIndex];

    CTransaction tx;
    tx.nTime = GetAdjustedTime();
    tx.vin.resize(1);
    tx.vin[0].prevout = COutPoint(coinbaseTx.GetHash(), 0);
    tx.vin[0].scriptSig = CScript() << OP_1;

    int64_t nCoinbaseValue = coinbaseTx.vout[0].nValue;
    int64_t nChange = nCoinbaseValue - nAmount - nFee;

    tx.vout.resize(1);
    tx.vout[0].nValue = nAmount;
    tx.vout[0].scriptPubKey = scriptPubKey;

    if (nChange > 0)
        tx.vout.push_back(CTxOut(nChange, CScript()));

    return tx;
}

// ---------------------------------------------------------------------------
// Mempool helpers
// ---------------------------------------------------------------------------

bool TestChain::AddToMempool(CTransaction& tx)
{
    return mempool.addUnchecked(tx.GetHash(), tx);
}

bool TestChain::SubmitToMempool(CTransaction& tx)
{
    CTxDB txdb("r");
    return tx.AcceptToMemoryPool(txdb);
}

void TestChain::ClearMempool()
{
    mempool.clear();
}

// ---------------------------------------------------------------------------
// Utility
// ---------------------------------------------------------------------------

bool TestChain::IsCoinbaseMature(unsigned int nCoinbaseIndex) const
{
    int nCoinbaseHeight = nBaseHeight + static_cast<int>(nCoinbaseIndex) + 1;
    int nDepth = chainHeight() - nCoinbaseHeight + 1;
    return nDepth >= (nCoinbaseMaturity + 10);
}
