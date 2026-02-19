// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2012 The Bitcoin developers
// Copyright (c) 2024 The Pinkcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Reward calculation, difficulty retargeting, and flash-stake timing.
// Extracted from main.cpp — identical function signatures and behavior.

#include "main.h"
#include "kernel.h"

#include <ctime>

using namespace std;

// Flash stake hours (UTC)
static const unsigned int nHour1 = 15;  // 7am UTC-8
static const unsigned int nHour2 = 20;  // 12pm UTC-8
static const unsigned int nHour3 = 1;   // 5pm UTC-8
static const unsigned int nHour4 = 6;   // 10pm UTC-8

// Retargeting timespans
static const int64_t nTargetTimespan = 60 * 60;              // 60 mins
static const int64_t nStakeTargetTimespan = 2 * 60 * 60;     // 2 Hours
static const int64_t nFlashStakeTargetTimespan = 10 * 60;    // 10 mins

int64_t GetProofOfWorkReward(int nHeight, int64_t nFees)
{
    int64_t nSubsidy = 0;
    int64_t nHalving = 0;

    if (nHeight == 1)
        nSubsidy = 364800000 * COIN; // Pinkcoin Coinbase.

    if (nHeight >= 17000)
    {
        nHalving = nHeight / nHalvingPoint / YEARLY_BLOCKCOUNT;
        nSubsidy = (50 * COIN) >> nHalving;
    }

    if (fDebug && GetBoolArg("-printcreation"))
        printf("GetProofOfWorkReward() : create=%s nSubsidy=%" PRId64 "\n", FormatMoney(nSubsidy).c_str(), nSubsidy);

    return nSubsidy + nFees;
}

// miner's coin stake reward based on coin age spent (coin-days)
int64_t GetProofOfStakeReward(int64_t nCoinAge, int64_t nFees, int nHeight, unsigned int nTime)
{
    int64_t nSubsidy = 0;
    int64_t nHalving = 0;

    bool fDisablePOW = pindexBest->nTime > nTimeV231;

    if (nHeight >= 16240)
    {
        if (IsFlashStake(nTime))
        {
            nHalving = nHeight / nHalvingPoint / YEARLY_BLOCKCOUNT;
            nSubsidy = (150 * COIN) >> nHalving;
        } else {
            nHalving = nHeight / nHalvingPoint / YEARLY_BLOCKCOUNT;
            nSubsidy = (100 * COIN) >> nHalving;

            if (fDisablePOW)
                nSubsidy /= 3;
        }


        // NOTE: Integer division bug — 16/10 evaluates to 1 in C++, so this
        // line is effectively nSubsidy *= 1 (no-op). This has been the consensus
        // rule since nTimeV231 (Aug 9, 2019). Do NOT "fix" without a hard fork.
        // Original intent: multiply by 1.6 to compensate for PoW removal.
        if (fDisablePOW)
        {
            nSubsidy *= 16 / 10;
        }

    }

    if (fDebug && GetBoolArg("-printcreation"))
        printf("GetProofOfStakeReward(): create=%s nCoinAge=%" PRId64 "\n", FormatMoney(nSubsidy).c_str(), nCoinAge);

    return nSubsidy + nFees;
}

//
// maximum nBits value could possible be required nTime after
//
static unsigned int ComputeMaxBits(CBigNum bnTargetLimit, unsigned int nBase, int64_t nTime)
{
    CBigNum bnResult;
    bnResult.SetCompact(nBase);
    bnResult *= 2;
    while (nTime > 0 && bnResult < bnTargetLimit)
    {
        // Maximum 200% adjustment per day...
        bnResult *= 2;
        nTime -= 24 * 60 * 60;
    }
    if (bnResult > bnTargetLimit)
        bnResult = bnTargetLimit;
    return bnResult.GetCompact();
}

//
// minimum amount of work that could possibly be required nTime after
// minimum proof-of-work required was nBase
//
unsigned int ComputeMinWork(unsigned int nBase, int64_t nTime)
{
    return ComputeMaxBits(bnProofOfWorkLimit, nBase, nTime);
}

//
// minimum amount of stake that could possibly be required nTime after
// minimum proof-of-stake required was nBase
//
unsigned int ComputeMinStake(unsigned int nBase, int64_t nTime, unsigned int nBlockTime)
{

    CBigNum bnStakeTarget = bnProofOfStakeLimit;

    if (IsFlashStake(nBlockTime))
        bnStakeTarget = bnProofOfFlashStakeLimit;

    return ComputeMaxBits(bnStakeTarget, nBase, nTime);
}


// ppcoin: find last block index up to pindex
const CBlockIndex* GetLastBlockIndex(const CBlockIndex* pindex, bool fProofOfStake)
{
    while (pindex && pindex->pprev && (pindex->IsProofOfStake() != fProofOfStake))
        pindex = pindex->pprev;
    return pindex;
}

const CBlockIndex* GetLastBlockIndex2(const CBlockIndex* pindex, bool fFlashStake)
{
    //bool bFlashStake = true;
    if (pindex->nHeight >= 771000) {

        while (pindex && pindex->pprev && !pindex->IsFPOS(fFlashStake))
            pindex = pindex->pprev;

    } else {

        while (pindex && pindex->pprev && IsFlashStake(pindex->nTime) != fFlashStake)
            pindex = pindex->pprev;
        while (pindex && pindex->pprev && (!pindex->IsProofOfStake()))
            pindex = pindex->pprev;
    }
    return pindex;
}


unsigned int GetNextTargetRequired(const CBlockIndex* pindexLast, bool fProofOfStake, unsigned int nBlockTime)
{
    if (pindexLast->nHeight < 817990) {
        return GetNextTargetRequiredV1(pindexLast, fProofOfStake, nBlockTime);
    }
    // Fork fixing nActualSpacing calculations for PoS/FPoS blocks
    // (makes PoS and FPoS next target calculations completelly independent).
    return GetNextTargetRequiredV2(pindexLast, fProofOfStake, nBlockTime);
}

unsigned int GetNextTargetRequiredV1(const CBlockIndex* pindexLast, bool fProofOfStake, unsigned int nBlockTime)
{
    CBigNum bnStakeTarget = bnProofOfStakeLimit;

    if (IsFlashStake(nBlockTime))
        bnStakeTarget = bnProofOfFlashStakeLimit;

    CBigNum bnTargetLimit = fProofOfStake ? bnStakeTarget : bnProofOfWorkLimit;

    if (pindexLast == nullptr)
        return bnTargetLimit.GetCompact(); // genesis block

    const CBlockIndex* pindexPrev = GetLastBlockIndex(pindexLast, fProofOfStake);
    if (pindexPrev->pprev == nullptr)
        return bnTargetLimit.GetCompact(); // first block
    const CBlockIndex* pindexPrevPrev = GetLastBlockIndex(pindexPrev->pprev, fProofOfStake);
    if (pindexPrevPrev->pprev == nullptr)
        return bnTargetLimit.GetCompact(); // second block


    bool fFlashStake = false;
    bool fFlashFlip = false;

    int nTS;
    nTS = nTargetSpacing;

    if (fProofOfStake)
    {
        if (IsFlashStake(nBlockTime))
        {
            fFlashStake = true;
            nTS = nTargetSpacing_FlashStaking;
            if (!IsFlashStake(pindexPrev->nTime))
                fFlashFlip = true;
        }
        else {
            nTS = (pindexBest->nTime > nTimeV231) ? nTargetSpacing : nTargetSpacing_Staking;
            if (IsFlashStake(pindexPrev->nTime))
                fFlashFlip = true;
        }
    }


    // ppcoin: target change every block
    // ppcoin: retarget with exponential moving toward target spacing
    CBigNum bnNew;
    int64_t nInterval;
    int64_t nActualSpacing;

    if (fFlashFlip)
    {
        // Replaces old version of nActualSpacing calculation for flip
        // blocks as it was discovered to always resolve to 0.
        const CBlockIndex* pPrev = GetLastBlockIndex2(pindexPrev, fFlashStake);
        if (pPrev == nullptr)
            return bnTargetLimit.GetCompact();
        nActualSpacing = 0;
        bnNew.SetCompact(pPrev->nBits);
    }
    else {
        bnNew.SetCompact(pindexPrev->nBits);
        nActualSpacing = pindexPrev->GetBlockTime() - pindexPrevPrev->GetBlockTime();
    }


    if (pindexPrev->nHeight < 315065)
    {
        if (nActualSpacing < 0)
            nActualSpacing = nTS;
    }


    if (fFlashStake)
        nInterval = nFlashStakeTargetTimespan / (nTS);
    else if (fProofOfStake)
        nInterval = nStakeTargetTimespan / (nTS);
    else
        nInterval = nTargetTimespan / (nTS);

    bnNew *= ((nInterval - 1) * (nTS) + nActualSpacing + nActualSpacing);
    bnNew /= ((nInterval + 1) * (nTS));

    if (bnNew <= 0 || bnNew > bnTargetLimit)
        bnNew = bnTargetLimit;

    return bnNew.GetCompact();
}

unsigned int GetNextTargetRequiredV2(const CBlockIndex* pindexLast, bool fProofOfStake, unsigned int nBlockTime)
{
    CBigNum bnTargetLimit;
    int64_t nActualSpacing;
    int nTS;
    CBigNum bnNew;

    bool fFlashStake = false;

    // Gets first previous block (PoW/PoS).
    const CBlockIndex* pindexPrev = GetLastBlockIndex(pindexLast, fProofOfStake);

    if (fProofOfStake) // PoS or FPoS block
    {
        if (IsFlashStake(nBlockTime)) {
            fFlashStake = true;
            bnTargetLimit = bnProofOfFlashStakeLimit;
            nTS = nTargetSpacing_FlashStaking;
        }
        else {
            bnTargetLimit = bnProofOfStakeLimit;
            nTS = (pindexBest->nTime > nTimeV231) ? nTargetSpacing : nTargetSpacing_Staking;
        }

        // Gets two previous same algorithm (PoS or FPoS) blocks.
        const CBlockIndex* pPrevSameAlgo = GetLastBlockIndex2(pindexPrev, fFlashStake);
        const CBlockIndex* pPrevPrevSameAlgo = GetLastBlockIndex2(pPrevSameAlgo->pprev, fFlashStake);

        nActualSpacing = pPrevSameAlgo->GetBlockTime() - pPrevPrevSameAlgo->GetBlockTime();
        // Makes sure that time spacing between consecutive
        // PoS/FPoS periods is removed from nActualSpacing final value.
        // Additionally slows down target adjustment in case of very large
        // block spacing (higher than 1h) for all PoS blocks.
        nActualSpacing %= 3600;
        bnNew.SetCompact(pPrevSameAlgo->nBits);
    }
    else // PoW block
    {
        bnTargetLimit = bnProofOfWorkLimit;
        nTS = nTargetSpacing;

        // Gets second PoW block.
        const CBlockIndex* pindexPrevPrev = GetLastBlockIndex(pindexPrev->pprev, false);

        nActualSpacing = pindexPrev->GetBlockTime() - pindexPrevPrev->GetBlockTime();
        bnNew.SetCompact(pindexPrev->nBits);
    }


    // ppcoin: target change every block
    // ppcoin: retarget with exponential moving toward target spacing
    int64_t nInterval;

    if (fFlashStake)
        nInterval = nFlashStakeTargetTimespan / nTS;
    else if (fProofOfStake)
        nInterval = nStakeTargetTimespan / nTS;
    else
        nInterval = nTargetTimespan / nTS;

    bnNew *= ((nInterval - 1) * nTS + nActualSpacing + nActualSpacing);
    bnNew /= ((nInterval + 1) * nTS);

    if (bnNew <= 0 || bnNew > bnTargetLimit)
        bnNew = bnTargetLimit;

    return bnNew.GetCompact();
}

bool IsFlashStake(unsigned int nTime)
{

    time_t rawtime;
    struct tm * ptm;

    bool bIsFlash = false;

    rawtime = nTime;
    ptm = gmtime ( &rawtime );
    int nHour = ptm->tm_hour ;
    switch(nHour)
    {
        case nHour1:
        bIsFlash = true;
        break;
        case nHour2:
        bIsFlash = true;
        break;
        case nHour3:
        bIsFlash = true;
        break;
        case nHour4:
        bIsFlash = true;
        break;
    }

    return bIsFlash;
}
