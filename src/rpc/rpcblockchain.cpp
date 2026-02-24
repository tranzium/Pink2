// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2012 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "main.h"
#include "bitcoinrpc.h"


extern void TxToJSON(const CTransaction& tx, const uint256 hashBlock, json& entry);
extern enum Checkpoints::CPMode CheckpointsMode;

double GetDifficulty(const CBlockIndex* blockindex)
{
    // Floating point number that is a multiple of the minimum difficulty,
    // minimum difficulty = 1.0.
    if (blockindex == nullptr)
    {
        if (pindexBest == nullptr)
            return 1.0;
        else
            blockindex = GetLastBlockIndex(pindexBest, false);
    }

    int nShift = (blockindex->nBits >> 24) & 0xff;

    double dDiff =
        static_cast<double>(0x0000ffff) / static_cast<double>(blockindex->nBits & 0x00ffffff);

    while (nShift < 29)
    {
        dDiff *= 256.0;
        nShift++;
    }
    while (nShift > 29)
    {
        dDiff /= 256.0;
        nShift--;
    }

    return dDiff;
}

double GetPoWMHashPS()
{

    int nPoWInterval = 72;
    int64_t nTargetSpacingWorkMin = 30, nTargetSpacingWork = 30;

    CBlockIndex* pindex = pindexGenesisBlock;
    CBlockIndex* pindexPrevWork = pindexGenesisBlock;

    while (pindex)
    {
        if (pindex->IsProofOfWork())
        {
            int64_t nActualSpacingWork = pindex->GetBlockTime() - pindexPrevWork->GetBlockTime();
            nTargetSpacingWork = ((nPoWInterval - 1) * nTargetSpacingWork + nActualSpacingWork + nActualSpacingWork) / (nPoWInterval + 1);
            nTargetSpacingWork = std::max(nTargetSpacingWork, nTargetSpacingWorkMin);
            pindexPrevWork = pindex;
        }

        pindex = pindex->pnext;
    }

    return GetDifficulty() * 4294.967296 / nTargetSpacingWork;
}

double GetPoSKernelPS()
{
    int nPoSInterval = 72;
    double dStakeKernelsTriedAvg = 0;
    int nStakesHandled = 0, nStakesTime = 0;

    CBlockIndex* pindex = pindexBest;;
    CBlockIndex* pindexPrevStake = nullptr;

    while (pindex && nStakesHandled < nPoSInterval)
    {
        if (pindex->IsProofOfStake())
        {
            dStakeKernelsTriedAvg += GetDifficulty(pindex) * 4294967296.0;
            nStakesTime += pindexPrevStake ? (pindexPrevStake->nTime - pindex->nTime) : 0;
            pindexPrevStake = pindex;
            nStakesHandled++;
        }

        pindex = pindex->pprev;
    }

    return nStakesTime ? dStakeKernelsTriedAvg / nStakesTime : 0;
}

json blockToJSON(const CBlock& block, const CBlockIndex* blockindex, bool fPrintTransactionDetail)
{
    json result;
    result["hash"] = block.GetHash().GetHex();
    int confirmations = -1;
    // Only report confirmations if the block is on the main chain
    if (blockindex->IsInMainChain())
        confirmations = nBestHeight - blockindex->nHeight + 1;
    result["confirmations"] = confirmations;
    result["size"] = static_cast<int>(::GetSerializeSize(block, SER_NETWORK, PROTOCOL_VERSION));
    result["height"] = blockindex->nHeight;
    result["version"] = block.nVersion;
    result["merkleroot"] = block.hashMerkleRoot.GetHex();
    result["mint"] = ValueFromAmount(blockindex->nMint);
    result["time"] = static_cast<int64_t>(block.GetBlockTime());
    result["nonce"] = static_cast<uint64_t>(block.nNonce);
    result["bits"] = HexBits(block.nBits);
    result["difficulty"] = GetDifficulty(blockindex);
    result["blocktrust"] = leftTrim(blockindex->GetBlockTrust().GetHex(), '0');
    result["chaintrust"] = leftTrim(blockindex->nChainTrust.GetHex(), '0');
    if (blockindex->pprev)
        result["previousblockhash"] = blockindex->pprev->GetBlockHash().GetHex();
    if (blockindex->pnext)
        result["nextblockhash"] = blockindex->pnext->GetBlockHash().GetHex();

    result["flags"] = strprintf("%s%s", blockindex->IsProofOfStake()? "proof-of-stake" : "proof-of-work", blockindex->GeneratedStakeModifier()? " stake-modifier": "");
    result["proofhash"] = blockindex->hashProof.GetHex();
    result["entropybit"] = static_cast<int>(blockindex->GetStakeEntropyBit());
    result["modifier"] = strprintf("%016" PRIx64, blockindex->nStakeModifier);
    json txinfo = json::array();
    for (const CTransaction& tx : block.vtx)
    {
        if (fPrintTransactionDetail)
        {
            json entry;

            entry["txid"] = tx.GetHash().GetHex();
            TxToJSON(tx, 0, entry);

            txinfo.push_back(entry);
        }
        else
            txinfo.push_back(tx.GetHash().GetHex());
    }

    result["tx"] = txinfo;

    if (block.IsProofOfStake())
        result["signature"] = HexStr(block.vchBlockSig.begin(), block.vchBlockSig.end());

    return result;
}

json getbestblockhash(const json& params, bool fHelp)
{
    if (fHelp || !params.empty())
        throw std::runtime_error(
            "getbestblockhash\n"
            "Returns the hash of the best block in the longest block chain.");

    return hashBestChain.GetHex();
}

json getblockcount(const json& params, bool fHelp)
{
    if (fHelp || !params.empty())
        throw std::runtime_error(
            "getblockcount\n"
            "Returns the number of blocks in the longest block chain.");

    return nBestHeight;
}


json getdifficulty(const json& params, bool fHelp)
{
    if (fHelp || !params.empty())
        throw std::runtime_error(
            "getdifficulty\n"
            "Returns the difficulty as a multiple of the minimum difficulty.");

    json obj;
    obj["proof-of-work"] = GetDifficulty();
    obj["proof-of-stake"] = GetDifficulty(GetLastBlockIndex2(GetLastBlockIndex(pindexBest, true), false));
    obj["proof-of-stake (flash)"] = GetDifficulty(GetLastBlockIndex2(pindexBest, true));
    obj["search-interval"] = static_cast<int>(nLastCoinStakeSearchInterval);
    return obj;
}


json settxfee(const json& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 1 || AmountFromValue(params[0]) < MIN_TX_FEE)
        throw std::runtime_error(
            "settxfee <amount>\n"
            "<amount> is a real and is rounded to the nearest 0.01");

    nTransactionFee = AmountFromValue(params[0]);
    nTransactionFee = (nTransactionFee / CENT) * CENT;  // round to cent

    return true;
}

json getrawmempool(const json& params, bool fHelp)
{
    if (fHelp || !params.empty())
        throw std::runtime_error(
            "getrawmempool\n"
            "Returns all transaction ids in memory pool.");

    std::vector<uint256> vtxid;
    mempool.queryHashes(vtxid);

    json a = json::array();
    for (const uint256& hash : vtxid)
        a.push_back(hash.ToString());

    return a;
}

json getblockhash(const json& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw std::runtime_error(
            "getblockhash <index>\n"
            "Returns hash of block in best-block-chain at <index>.");

    int nHeight = params[0].get<int>();
    if (nHeight < 0 || nHeight > nBestHeight)
        throw std::runtime_error("Block number out of range.");

    CBlockIndex* pblockindex = FindBlockByHeight(nHeight);
    return pblockindex->phashBlock->GetHex();
}

json getblock(const json& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 2)
        throw std::runtime_error(
            "getblock <hash> [txinfo]\n"
            "txinfo optional to print more detailed tx info\n"
            "Returns details of a block with given block-hash.");

    std::string strHash = params[0].get<std::string>();
    uint256 hash(strHash);

    if (mapBlockIndex.count(hash) == 0)
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");

    CBlock block;
    CBlockIndex* pblockindex = mapBlockIndex[hash];
    block.ReadFromDisk(pblockindex, true);

    return blockToJSON(block, pblockindex, params.size() > 1 ? params[1].get<bool>() : false);
}

json getblockbynumber(const json& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 2)
        throw std::runtime_error(
            "getblockbynumber <number> [txinfo]\n"
            "txinfo optional to print more detailed tx info\n"
            "Returns details of a block with given block-number.");

    int nHeight = params[0].get<int>();
    if (nHeight < 0 || nHeight > nBestHeight)
        throw std::runtime_error("Block number out of range.");

    CBlock block;
    CBlockIndex* pblockindex = mapBlockIndex[hashBestChain];
    while (pblockindex->nHeight > nHeight)
        pblockindex = pblockindex->pprev;

    uint256 hash = *pblockindex->phashBlock;

    pblockindex = mapBlockIndex[hash];
    block.ReadFromDisk(pblockindex, true);

    return blockToJSON(block, pblockindex, params.size() > 1 ? params[1].get<bool>() : false);
}

// ppcoin: get information of sync-checkpoint
json getcheckpoint(const json& params, bool fHelp)
{
    if (fHelp || !params.empty())
        throw std::runtime_error(
            "getcheckpoint\n"
            "Show info of synchronized checkpoint.\n");

    json result;
    CBlockIndex* pindexCheckpoint;

    result["synccheckpoint"] = Checkpoints::hashSyncCheckpoint.ToString().c_str();
    pindexCheckpoint = mapBlockIndex[Checkpoints::hashSyncCheckpoint];
    result["height"] = pindexCheckpoint->nHeight;
    result["timestamp"] = DateTimeStrFormat(pindexCheckpoint->GetBlockTime()).c_str();

    // Check that the block satisfies synchronized checkpoint
    if (CheckpointsMode == Checkpoints::STRICT)
        result["policy"] = "strict";

    if (CheckpointsMode == Checkpoints::ADVISORY)
        result["policy"] = "advisory";

    if (CheckpointsMode == Checkpoints::PERMISSIVE)
        result["policy"] = "permissive";

    if (mapArgs.count("-checkpointkey"))
        result["checkpointmaster"] = true;

    return result;
}
