// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2012 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "main.h"
#include "arith_uint256.h"
#include "db.h"
#include "txdb.h"
#include "init.h"
#include "miner.h"
#include "bitcoinrpc.h"

#include "time.h"



json getsubsidy(const json& params, bool fHelp)
{
    if (fHelp || params.size() > 1)
        throw std::runtime_error(
            "getsubsidy [nTarget]\n"
            "Returns proof-of-work subsidy value for the specified value of target.");

    int nShowHeight;
    if (!params.empty())
        nShowHeight = atoi(params[0].get<std::string>());
    else
        nShowHeight = nBestHeight+1; // block currently being solved

    return static_cast<uint64_t>(GetProofOfWorkReward(nShowHeight, 0));
}

json getmininginfo(const json& params, bool fHelp)
{
    if (fHelp || !params.empty())
        throw std::runtime_error(
            "getmininginfo\n"
            "Returns an object containing mining-related information.");

    uint64_t nMinWeight = 0, nMaxWeight = 0, nWeight = 0;
    pwalletMain->GetStakeWeight(*pwalletMain, nMinWeight, nMaxWeight, nWeight);

    time_t rawtime;
    time ( &rawtime );

    bool is_pow_disabled = pindexBest->nTime > nTimeV231;
    bool is_flash_stake = IsFlashStake(rawtime);
    bool staking_status = nLastCoinStakeSearchInterval && nWeight;
    unsigned int block_target_spacing = is_pow_disabled && is_flash_stake ? nTargetSpacing_FlashStaking : nTargetSpacing;
    int staking_estimated_time = staking_status ? (block_target_spacing * GetPoSKernelPS() / nWeight) : -1;
    bool is_targeting_fpos = static_cast<int64_t>(nSplitThreshold) >= 100000;

    json obj, obj_staking_info, obj_staking_weight, obj_diff;

    obj["blocks"] = static_cast<int>(nBestHeight);
    obj["next-block-value-pos"] = ValueFromAmount(GetProofOfStakeReward(0, 0, nBestHeight+1, 0));
    if(!is_pow_disabled)
        obj["next-block-value-pow"] = ValueFromAmount(GetProofOfWorkReward(nBestHeight+1, 0));
    obj["last-block-size"] = static_cast<uint64_t>(nLastBlockSize);
    obj["last-block-tx"] = static_cast<uint64_t>(nLastBlockTx);
    obj["pooledtx"] = static_cast<uint64_t>(mempool.size());
    obj["tx-fee"] = ValueFromAmount(static_cast<int64_t>(MIN_TX_FEE));

    obj_staking_info["enabled"] = staking_status;
    obj_staking_info["targeting-fpos"] = is_targeting_fpos;
    obj_staking_info["estimated-time"] = staking_estimated_time;
    obj_staking_info["search-interval"] = static_cast<int>(nLastCoinStakeSearchInterval);
    obj_staking_info["utxo-combine-threshold"] = static_cast<int64_t>(nCombineThreshold);
    obj_staking_info["utxo-split-threshold"] = static_cast<int64_t>(nSplitThreshold);
    obj["staking"] = obj_staking_info;

    obj_staking_weight["minimum"] = static_cast<uint64_t>(nMinWeight);
    obj_staking_weight["maximum"] = static_cast<uint64_t>(nMaxWeight);
    obj_staking_weight["combined"] = static_cast<uint64_t>(nWeight);
    obj_staking_weight["network"] = static_cast<uint64_t>(GetPoSKernelPS());
    obj["stakeweight"] = obj_staking_weight;

    if(!is_pow_disabled)
        obj_diff["proof-of-work"] = GetDifficulty();
    obj_diff["proof-of-stake"] = GetDifficulty(GetLastBlockIndex2(GetLastBlockIndex(pindexBest, true), false));
    obj_diff["proof-of-stake(flash)"] = GetDifficulty(GetLastBlockIndex2(pindexBest, true));
    obj["difficulty"] = obj_diff;

    obj["netstakeweight"] = static_cast<uint64_t>(GetPoSKernelPS());
    if(!is_pow_disabled)
        obj["netmhashps"] = GetPoWMHashPS();
    obj["testnet"] = fTestNet;
    obj["errors"] = GetWarnings("statusbar");
    return obj;
}

json getstakinginfo(const json& params, bool fHelp)
{
    if (fHelp || !params.empty())
        throw std::runtime_error(
            "getstakinginfo\n"
            "Returns an object containing staking-related information.");

    uint64_t nMinWeight = 0, nMaxWeight = 0, nWeight = 0;
    pwalletMain->GetStakeWeight(*pwalletMain, nMinWeight, nMaxWeight, nWeight);

    uint64_t nNetworkWeight = GetPoSKernelPS();
    bool staking = nLastCoinStakeSearchInterval && nWeight;

    unsigned int nTS;
    nTS = (pindexBest->nTime > nTimeV231) ? nTargetSpacing : nTargetSpacing_Staking;

    time_t rawtime;
    time ( &rawtime );

    if (IsFlashStake(rawtime))
        nTS = nTargetSpacing_FlashStaking;

    int nExpectedTime = staking ? (nTS * nNetworkWeight / nWeight) : -1;

    json obj;

    obj["enabled"] = GetBoolArg("-staking", true);
    obj["staking"] = staking;
    obj["errors"] = GetWarnings("statusbar");

    obj["currentblocksize"] = static_cast<uint64_t>(nLastBlockSize);
    obj["currentblocktx"] = static_cast<uint64_t>(nLastBlockTx);
    obj["pooledtx"] = static_cast<uint64_t>(mempool.size());

    obj["difficulty"] = GetDifficulty(GetLastBlockIndex2(GetLastBlockIndex(pindexBest, true), false));
    obj["difficulty (flash)"] = GetDifficulty(GetLastBlockIndex2(pindexBest, true));
    obj["search-interval"] = static_cast<int>(nLastCoinStakeSearchInterval);

    obj["weight"] = static_cast<uint64_t>(nWeight);
    obj["netstakeweight"] = static_cast<uint64_t>(nNetworkWeight);

    obj["expectedtime"] = nExpectedTime;

    return obj;
}

json getworkex(const json& params, bool fHelp)
{
    if (fHelp || params.size() > 2)
        throw std::runtime_error(
            "getworkex [data, coinbase]\n"
            "If [data, coinbase] is not specified, returns extended work data.\n"
        );

    if (vNodes.empty())
        throw JSONRPCError(-9, "Pinkcoin is not connected!");

    if (IsInitialBlockDownload())
        throw JSONRPCError(-10, "Pinkcoin is downloading blocks...");

    using mapNewBlock_t = std::map<uint256, std::pair<CBlock*, CScript> >;
    static mapNewBlock_t mapNewBlock;
    static std::vector<std::unique_ptr<CBlock>> vNewBlock;
    static CReserveKey reservekey(pwalletMain.get());

    if (params.empty())
    {
        // Update block
        static unsigned int nTransactionsUpdatedLast;
        static CBlockIndex* pindexPrev;
        static int64_t nStart;
        static CBlock* pblock;
        if (pindexPrev != pindexBest ||
            (nTransactionsUpdated != nTransactionsUpdatedLast && GetAdjustedTime() - nStart > 60))
        {
            if (pindexPrev != pindexBest)
            {
                // Deallocate old blocks since they're obsolete now
                mapNewBlock.clear();
                vNewBlock.clear();
            }
            nTransactionsUpdatedLast = nTransactionsUpdated;
            pindexPrev = pindexBest;
            nStart = GetAdjustedTime();

            // Create new block
            pblock = CreateNewBlock(pwalletMain.get());
            if (!pblock)
                throw JSONRPCError(-7, "Out of memory");
            vNewBlock.push_back(std::unique_ptr<CBlock>(pblock));
        }

        // Update nTime
        pblock->nTime = std::max(pindexPrev->GetPastTimeLimit()+1, GetAdjustedTime());
        pblock->nNonce = 0;

        // Update nExtraNonce
        static unsigned int nExtraNonce = 0;
        IncrementExtraNonce(pblock, pindexPrev, nExtraNonce);

        // Save
        mapNewBlock[pblock->hashMerkleRoot] = std::make_pair(pblock, pblock->vtx[0].vin[0].scriptSig);

        // Prebuild hash buffers
        char pmidstate[32];
        char pdata[128];
        char phash1[64];
        FormatHashBuffers(pblock, pmidstate, pdata, phash1);

        uint256 hashTarget = ArithToUint256(arith_uint256().SetCompact(pblock->nBits));

        CTransaction coinbaseTx = pblock->vtx[0];
        std::vector<uint256> merkle = pblock->GetMerkleBranch(0);

        json result;
        result["data"] = HexStr(CharCast(pdata), CharEnd(pdata));
        result["target"] = HexStr(CharCast(hashTarget), CharEnd(hashTarget));

        CDataStream ssTx(SER_NETWORK, PROTOCOL_VERSION);
        ssTx << coinbaseTx;
        result["coinbase"] = HexStr(ssTx.begin(), ssTx.end());

        json merkle_arr = json::array();

        for (const uint256& merkleh : merkle) {
            merkle_arr.push_back(HexStr(CharCast(merkleh), CharEnd(merkleh)));
        }

        result["merkle"] = merkle_arr;


        return result;
    }
    else
    {
        // Parse parameters
        std::vector<unsigned char> vchData = ParseHex(params[0].get<std::string>());
        std::vector<unsigned char> coinbase;

        if(params.size() == 2)
            coinbase = ParseHex(params[1].get<std::string>());

        if (vchData.size() != 128)
            throw JSONRPCError(-8, "Invalid parameter");

        CBlock* pdata = reinterpret_cast<CBlock*>(&vchData[0]);

        // Byte reverse
        for (int i = 0; i < 128/4; i++)
            reinterpret_cast<unsigned int*>(pdata)[i] = ByteReverse(reinterpret_cast<unsigned int*>(pdata)[i]);

        // Get saved block
        if (!mapNewBlock.count(pdata->hashMerkleRoot))
            return false;
        CBlock* pblock = mapNewBlock[pdata->hashMerkleRoot].first;

        pblock->nTime = pdata->nTime;
        pblock->nNonce = pdata->nNonce;

        if(coinbase.empty())
            pblock->vtx[0].vin[0].scriptSig = mapNewBlock[pdata->hashMerkleRoot].second;
        else
            CDataStream(coinbase, SER_NETWORK, PROTOCOL_VERSION) >> pblock->vtx[0]; // FIXME - HACK!

        pblock->hashMerkleRoot = pblock->BuildMerkleTree();

        return CheckWork(pblock, *pwalletMain, reservekey);
    }
}


json getwork(const json& params, bool fHelp)
{
    if (fHelp || params.size() > 1)
        throw std::runtime_error(
            "getwork [data]\n"
            "If [data] is not specified, returns formatted hash data to work on:\n"
            "  \"midstate\" : precomputed hash state after hashing the first half of the data (DEPRECATED)\n" // deprecated
            "  \"data\" : block data\n"
            "  \"hash1\" : formatted hash buffer for second hash (DEPRECATED)\n" // deprecated
            "  \"target\" : little endian hash target\n"
            "If [data] is specified, tries to solve the block and returns true if it was successful.");

    if (vNodes.empty())
        throw JSONRPCError(RPC_CLIENT_NOT_CONNECTED, "Pinkcoin is not connected!");

    if (IsInitialBlockDownload())
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "Pinkcoin is downloading blocks...");

    using mapNewBlock_t = std::map<uint256, std::pair<CBlock*, CScript> >;
    static mapNewBlock_t mapNewBlock;    // FIXME: thread safety
    static std::vector<std::unique_ptr<CBlock>> vNewBlock;
    static CReserveKey reservekey(pwalletMain.get());

    if (params.empty())
    {
        // Update block
        static unsigned int nTransactionsUpdatedLast;
        static CBlockIndex* pindexPrev;
        static int64_t nStart;
        static CBlock* pblock;
        if (pindexPrev != pindexBest ||
            (nTransactionsUpdated != nTransactionsUpdatedLast && GetAdjustedTime() - nStart > 60))
        {
            if (pindexPrev != pindexBest)
            {
                // Deallocate old blocks since they're obsolete now
                mapNewBlock.clear();
                vNewBlock.clear();
            }

            // Clear pindexPrev so future getworks make a new block, despite any failures from here on
            pindexPrev = nullptr;

            // Store the pindexBest used before CreateNewBlock, to avoid races
            nTransactionsUpdatedLast = nTransactionsUpdated;
            CBlockIndex* pindexPrevNew = pindexBest;
            nStart = GetAdjustedTime();

            // Create new block
            pblock = CreateNewBlock(pwalletMain.get());
            if (!pblock)
                throw JSONRPCError(RPC_OUT_OF_MEMORY, "Out of memory");
            vNewBlock.push_back(std::unique_ptr<CBlock>(pblock));

            // Need to update only after we know CreateNewBlock succeeded
            pindexPrev = pindexPrevNew;
        }

        // Update nTime
        pblock->UpdateTime(pindexPrev);
        pblock->nNonce = 0;

        // Update nExtraNonce
        static unsigned int nExtraNonce = 0;
        IncrementExtraNonce(pblock, pindexPrev, nExtraNonce);

        // Save
        mapNewBlock[pblock->hashMerkleRoot] = std::make_pair(pblock, pblock->vtx[0].vin[0].scriptSig);

        // Pre-build hash buffers
        char pmidstate[32];
        char pdata[128];
        char phash1[64];
        FormatHashBuffers(pblock, pmidstate, pdata, phash1);

        uint256 hashTarget = ArithToUint256(arith_uint256().SetCompact(pblock->nBits));

        json result;
        result["midstate"] = HexStr(CharCast(pmidstate), CharEnd(pmidstate)); // deprecated
        result["data"] = HexStr(CharCast(pdata), CharEnd(pdata));
        result["hash1"] = HexStr(CharCast(phash1), CharEnd(phash1)); // deprecated
        result["target"] = HexStr(CharCast(hashTarget), CharEnd(hashTarget));
        return result;
    }
    else
    {
        // Parse parameters
        std::vector<unsigned char> vchData = ParseHex(params[0].get<std::string>());
        if (vchData.size() != 128)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter");
        CBlock* pdata = reinterpret_cast<CBlock*>(&vchData[0]);

        // Byte reverse
        for (int i = 0; i < 128/4; i++)
            reinterpret_cast<unsigned int*>(pdata)[i] = ByteReverse(reinterpret_cast<unsigned int*>(pdata)[i]);

        // Get saved block
        if (!mapNewBlock.count(pdata->hashMerkleRoot))
            return false;
        CBlock* pblock = mapNewBlock[pdata->hashMerkleRoot].first;

        pblock->nTime = pdata->nTime;
        pblock->nNonce = pdata->nNonce;
        pblock->vtx[0].vin[0].scriptSig = mapNewBlock[pdata->hashMerkleRoot].second;
        pblock->hashMerkleRoot = pblock->BuildMerkleTree();

        return CheckWork(pblock, *pwalletMain, reservekey);
    }
}


json getblocktemplate(const json& params, bool fHelp)
{
    if (fHelp || params.size() > 1)
        throw std::runtime_error(
            "getblocktemplate [params]\n"
            "Returns data needed to construct a block to work on:\n"
            "  \"version\" : block version\n"
            "  \"previousblockhash\" : hash of current highest block\n"
            "  \"transactions\" : contents of non-coinbase transactions that should be included in the next block\n"
            "  \"coinbaseaux\" : data that should be included in coinbase\n"
            "  \"coinbasevalue\" : maximum allowable input to coinbase transaction, including the generation award and transaction fees\n"
            "  \"target\" : hash target\n"
            "  \"mintime\" : minimum timestamp appropriate for next block\n"
            "  \"curtime\" : current timestamp\n"
            "  \"mutable\" : list of ways the block template may be changed\n"
            "  \"noncerange\" : range of valid nonces\n"
            "  \"sigoplimit\" : limit of sigops in blocks\n"
            "  \"sizelimit\" : limit of block size\n"
            "  \"bits\" : compressed target of next block\n"
            "  \"height\" : height of the next block\n"
            "See https://en.bitcoin.it/wiki/BIP_0022 for full specification.");

    std::string strMode = "template";
    if (!params.empty())
    {
        const json& oparam = params[0];
        json modeval = oparam.contains("mode") ? oparam["mode"] : json(nullptr);
        if (modeval.is_string())
            strMode = modeval.get<std::string>();
        else if (modeval.is_null())
        {
            /* Do nothing */
        }
        else
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid mode");
    }

    if (strMode != "template")
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid mode");

    if (vNodes.empty())
        throw JSONRPCError(RPC_CLIENT_NOT_CONNECTED, "Pinkcoin is not connected!");

    if (IsInitialBlockDownload())
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "Pinkcoin is downloading blocks...");

    static CReserveKey reservekey(pwalletMain.get());

    // Update block
    static unsigned int nTransactionsUpdatedLast;
    static CBlockIndex* pindexPrev;
    static int64_t nStart;
    static std::unique_ptr<CBlock> pblock;
    if (pindexPrev != pindexBest ||
        (nTransactionsUpdated != nTransactionsUpdatedLast && GetAdjustedTime() - nStart > 5))
    {
        // Clear pindexPrev so future calls make a new block, despite any failures from here on
        pindexPrev = nullptr;

        // Store the pindexBest used before CreateNewBlock, to avoid races
        nTransactionsUpdatedLast = nTransactionsUpdated;
        CBlockIndex* pindexPrevNew = pindexBest;
        nStart = GetAdjustedTime();

        // Create new block
        pblock.reset(CreateNewBlock(pwalletMain.get()));
        if (!pblock)
            throw JSONRPCError(RPC_OUT_OF_MEMORY, "Out of memory");

        // Need to update only after we know CreateNewBlock succeeded
        pindexPrev = pindexPrevNew;
    }

    // Update nTime
    pblock->UpdateTime(pindexPrev);
    pblock->nNonce = 0;

    json transactions = json::array();
    std::map<uint256, int64_t> setTxIndex;
    int i = 0;
    CTxDB txdb("r");
    for (CTransaction& tx : pblock->vtx)
    {
        uint256 txHash = tx.GetHash();
        setTxIndex[txHash] = i++;

        if (tx.IsCoinBase() || tx.IsCoinStake())
            continue;

        json entry;

        CDataStream ssTx(SER_NETWORK, PROTOCOL_VERSION);
        ssTx << tx;
        entry["data"] = HexStr(ssTx.begin(), ssTx.end());

        entry["hash"] = txHash.GetHex();

        MapPrevTx mapInputs;
        std::map<uint256, CTxIndex> mapUnused;
        bool fInvalid = false;
        if (tx.FetchInputs(txdb, mapUnused, false, false, mapInputs, fInvalid))
        {
            entry["fee"] = static_cast<int64_t>(tx.GetValueIn(mapInputs) - tx.GetValueOut());

            json deps = json::array();
            for (MapPrevTx::value_type& inp : mapInputs)
            {
                if (setTxIndex.count(inp.first))
                    deps.push_back(setTxIndex[inp.first]);
            }
            entry["depends"] = deps;

            int64_t nSigOps = tx.GetLegacySigOpCount();
            nSigOps += tx.GetP2SHSigOpCount(mapInputs);
            entry["sigops"] = nSigOps;
        }

        transactions.push_back(entry);
    }

    json aux;
    aux["flags"] = HexStr(COINBASE_FLAGS.begin(), COINBASE_FLAGS.end());

    uint256 hashTarget = ArithToUint256(arith_uint256().SetCompact(pblock->nBits));

    static json aMutable = json::array();
    if (aMutable.empty())
    {
        aMutable.push_back("time");
        aMutable.push_back("transactions");
        aMutable.push_back("prevblock");
    }

    json result;
    result["version"] = pblock->nVersion;
    result["previousblockhash"] = pblock->hashPrevBlock.GetHex();
    result["transactions"] = transactions;
    result["coinbaseaux"] = aux;
    result["coinbasevalue"] = static_cast<int64_t>(pblock->vtx[0].vout[0].nValue);
    result["target"] = hashTarget.GetHex();
    result["mintime"] = static_cast<int64_t>(pindexPrev->GetPastTimeLimit()+1);
    result["mutable"] = aMutable;
    result["noncerange"] = "00000000ffffffff";
    result["sigoplimit"] = static_cast<int64_t>(MAX_BLOCK_SIGOPS);
    result["sizelimit"] = static_cast<int64_t>(MAX_BLOCK_SIZE);
    result["curtime"] = static_cast<int64_t>(pblock->nTime);
    result["bits"] = HexBits(pblock->nBits);
    result["height"] = static_cast<int64_t>(pindexPrev->nHeight+1);

    return result;
}

json submitblock(const json& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 2)
        throw std::runtime_error(
            "submitblock <hex data> [optional-params-obj]\n"
            "[optional-params-obj] parameter is currently ignored.\n"
            "Attempts to submit new block to network.\n"
            "See https://en.bitcoin.it/wiki/BIP_0022 for full specification.");

    std::vector<unsigned char> blockData(ParseHex(params[0].get<std::string>()));
    CDataStream ssBlock(blockData, SER_NETWORK, PROTOCOL_VERSION);
    CBlock block;
    try {
        ssBlock >> block;
    }
    catch (std::exception &e) {
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Block decode failed");
    }

    bool fAccepted = ProcessBlock(nullptr, &block);
    if (!fAccepted)
        return "rejected";

    return nullptr;
}
