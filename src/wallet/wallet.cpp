// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2012 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "txdb.h"
#include "stakedb.h"
#include "wallet.h"
#include "walletdb.h"
#include "crypter.h"
#include "init.h"
#include "ui_interface.h"
#include "base58.h"
#include "kernel.h"
#include "coincontrol.h"
#include "arith_uint256.h"
#include <memory>

DBErrors CWallet::LoadWallet(bool& fFirstRunRet)
{
    if (!fFileBacked)
        return DB_LOAD_OK;
    fFirstRunRet = false;
    DBErrors nLoadWalletRet = CWalletDB(strWalletFile,"cr+").LoadWallet(this);
    if (nLoadWalletRet == DB_NEED_REWRITE)
    {
        if (CDB::Rewrite(strWalletFile, "\x04pool"))
        {
            LOCK(cs_wallet);
            setKeyPool.clear();
            // Note: can't top-up keypool here, because wallet is locked.
            // User will be prompted to unlock wallet the next operation
            // the requires a new key.
        }
    }

    if (nLoadWalletRet != DB_LOAD_OK)
        return nLoadWalletRet;
    fFirstRunRet = !vchDefaultKey.IsValid();

    NewThread(ThreadFlushWalletDB, strWalletFile);
    return DB_LOAD_OK;
}

SDBErrors CWallet::LoadStakeDB(bool& fFirstRunRet)
{
    if (!fFileBacked)
        return SDB_LOAD_OK;
    fFirstRunRet = false;
    SDBErrors nLoadStakeDBRet = CStakeDB(strWalletFile,"cr+").LoadWallet(this);

    if (nLoadStakeDBRet != SDB_LOAD_OK)
        return nLoadStakeDBRet;

    NewThread(ThreadFlushStakeDB, strWalletFile);
    return SDB_LOAD_OK;
}

void CWallet::PrintWallet(const CBlock& block)
{
    {
        LOCK(cs_wallet);
        if (block.IsProofOfWork() && mapWallet.count(block.vtx[0].GetHash()))
        {
            CWalletTx& wtx = mapWallet[block.vtx[0].GetHash()];
            LogPrint(BCLog::WALLET, "    mine:  %d  %d  %" PRId64 """", wtx.GetDepthInMainChain(), wtx.GetBlocksToMaturity(), wtx.GetCredit());
        }
        if (block.IsProofOfStake() && mapWallet.count(block.vtx[1].GetHash()))
        {
            CWalletTx& wtx = mapWallet[block.vtx[1].GetHash()];
            LogPrint(BCLog::WALLET, "    stake: %d  %d  %" PRId64 """", wtx.GetDepthInMainChain(), wtx.GetBlocksToMaturity(), wtx.GetCredit());
         }

    }
    LogPrint(BCLog::WALLET, "\n");
}

bool GetWalletFile(CWallet* pwallet, std::string &strWalletFileOut)
{
    if (!pwallet->fFileBacked)
        return false;
    strWalletFileOut = pwallet->strWalletFile;
    return true;
}

bool GetPStakeDB(CWallet *pstakeDB)
{
    return pstakeDB;
}

std::map<CTxDestination, int64_t> CWallet::GetAddressBalances()
{
    std::map<CTxDestination, int64_t> balances;

    {
        LOCK(cs_wallet);
        for (auto walletEntry : mapWallet)
        {
            CWalletTx *pcoin = &walletEntry.second;

            if (!pcoin->IsFinal() || !pcoin->IsTrusted())
                continue;

            if ((pcoin->IsCoinBase() || pcoin->IsCoinStake()) && pcoin->GetBlocksToMaturity() > 0)
                continue;

            int nDepth = pcoin->GetDepthInMainChain();
            if (nDepth < (pcoin->IsFromMe() ? 0 : 1))
                continue;

            for (unsigned int i = 0; i < pcoin->vout.size(); i++)
            {
                CTxDestination addr;
                if (!IsMine(pcoin->vout[i]))
                    continue;
                if(!ExtractDestination(pcoin->vout[i].scriptPubKey, addr))
                    continue;

                int64_t n = pcoin->IsSpent(i) ? 0 : pcoin->vout[i].nValue;

                if (!balances.count(addr))
                    balances[addr] = 0;
                balances[addr] += n;
            }
        }
    }

    return balances;
}

std::set< std::set<CTxDestination> > CWallet::GetAddressGroupings()
{
    AssertLockHeld(cs_wallet); // mapWallet
    std::set< std::set<CTxDestination> > groupings;
    std::set<CTxDestination> grouping;

    for (auto walletEntry : mapWallet)
    {
        CWalletTx *pcoin = &walletEntry.second;

        if (pcoin->vin.size() > 0 && IsMine(pcoin->vin[0]))
        {
            // group all input addresses with each other
            for (CTxIn txin : pcoin->vin)
            {
                CTxDestination address;
                if(!ExtractDestination(mapWallet[txin.prevout.hash].vout[txin.prevout.n].scriptPubKey, address))
                    continue;
                grouping.insert(address);
            }

            // group change with input addresses
            for (CTxOut txout : pcoin->vout)
                if (IsChange(txout))
                {
                    CWalletTx tx = mapWallet[pcoin->vin[0].prevout.hash];
                    CTxDestination txoutAddr;
                    if(!ExtractDestination(txout.scriptPubKey, txoutAddr))
                        continue;
                    grouping.insert(txoutAddr);
                }
            groupings.insert(grouping);
            grouping.clear();
        }

        // group lone addrs by themselves
        for (unsigned int i = 0; i < pcoin->vout.size(); i++)
            if (IsMine(pcoin->vout[i]))
            {
                CTxDestination address;
                if(!ExtractDestination(pcoin->vout[i].scriptPubKey, address))
                    continue;
                grouping.insert(address);
                groupings.insert(grouping);
                grouping.clear();
            }
    }

    std::set< std::shared_ptr<std::set<CTxDestination>> > uniqueGroupings; // a set of pointers to groups of addresses
    std::map< CTxDestination, std::shared_ptr<std::set<CTxDestination>> > setmap;  // map addresses to the unique group containing it
    for (std::set<CTxDestination> grouping : groupings)
    {
        // make a set of all the groups hit by this new group
        std::set< std::shared_ptr<std::set<CTxDestination>> > hits;
        std::map< CTxDestination, std::shared_ptr<std::set<CTxDestination>> >::iterator it;
        for (CTxDestination address : grouping)
            if ((it = setmap.find(address)) != setmap.end())
                hits.insert((*it).second);

        // merge all hit groups into a new single group
        auto merged = std::make_shared<std::set<CTxDestination>>(grouping);
        for (const auto& hit : hits)
        {
            merged->insert(hit->begin(), hit->end());
            uniqueGroupings.erase(hit);
        }
        uniqueGroupings.insert(merged);

        // update setmap
        for (CTxDestination element : *merged)
            setmap[element] = merged;
    }

    std::set< std::set<CTxDestination> > ret;
    for (const auto& uniqueGrouping : uniqueGroupings)
    {
        ret.insert(*uniqueGrouping);
    }

    return ret;
}

bool CReserveKey::GetReservedKey(CPubKey& pubkey)
{
    if (nIndex == -1)
    {
        CKeyPool keypool;
        pwallet->ReserveKeyFromKeyPool(nIndex, keypool);
        if (nIndex != -1)
            vchPubKey = keypool.vchPubKey;
        else {
            if (pwallet->vchDefaultKey.IsValid()) {
                LogPrintf("CReserveKey::GetReservedKey(): Warning: Using default key instead of a new key, top up your keypool!");
                vchPubKey = pwallet->vchDefaultKey;
            } else
                return false;
        }
    }
    assert(vchPubKey.IsValid());
    pubkey = vchPubKey;
    return true;
}

void CReserveKey::KeepKey()
{
    if (nIndex != -1)
        pwallet->KeepKey(nIndex);
    nIndex = -1;
    vchPubKey = CPubKey();
}

void CReserveKey::ReturnKey()
{
    if (nIndex != -1)
        pwallet->ReturnKey(nIndex);
    nIndex = -1;
    vchPubKey = CPubKey();
}

void CWallet::GetAllReserveKeys(std::set<CKeyID>& setAddress) const
{
    setAddress.clear();

    CWalletDB walletdb(strWalletFile);

    LOCK2(cs_main, cs_wallet);
    for (const int64_t& id : setKeyPool)
    {
        CKeyPool keypool;
        if (!walletdb.ReadPool(id, keypool))
            throw std::runtime_error("GetAllReserveKeyHashes() : read failed");
        assert(keypool.vchPubKey.IsValid());
        CKeyID keyID = keypool.vchPubKey.GetID();
        if (!HaveKey(keyID))
            throw std::runtime_error("GetAllReserveKeyHashes() : unknown key in key pool");
        setAddress.insert(keyID);
    }
}

void CWallet::GetKeyBirthTimes(std::map<CKeyID, int64_t> &mapKeyBirth) const {
    AssertLockHeld(cs_wallet); // mapKeyMetadata
    mapKeyBirth.clear();

    // get birth times for keys with metadata
    for (const auto& entry : mapKeyMetadata)
        if (entry.second.nCreateTime)
            mapKeyBirth[entry.first] = entry.second.nCreateTime;

    // map in which we'll infer heights of other keys
    CBlockIndex *pindexMax = FindBlockByHeight(std::max(0, nBestHeight - 144)); // the tip can be reorganised; use a 144-block safety margin
    std::map<CKeyID, CBlockIndex*> mapKeyFirstBlock;
    std::set<CKeyID> setKeys;
    GetKeys(setKeys);
    for (const CKeyID &keyid : setKeys) {
        if (mapKeyBirth.count(keyid) == 0)
            mapKeyFirstBlock[keyid] = pindexMax;
    }
    setKeys.clear();

    // if there are no such keys, we're done
    if (mapKeyFirstBlock.empty())
        return;

    // find first block that affects those keys, if there are any left
    std::vector<CKeyID> vAffected;
    for (const auto& entry : mapWallet) {
        // iterate over all wallet transactions...
        const CWalletTx &wtx = entry.second;
        auto blit = mapBlockIndex.find(wtx.hashBlock);
        if (blit != mapBlockIndex.end() && blit->second->IsInMainChain()) {
            // ... which are already in a block
            int nHeight = blit->second->nHeight;
            for (const CTxOut &txout : wtx.vout) {
                // iterate over all their outputs
                ::ExtractAffectedKeys(*this, txout.scriptPubKey, vAffected);
                for (const CKeyID &keyid : vAffected) {
                    // ... and all their affected keys
                    auto rit = mapKeyFirstBlock.find(keyid);
                    if (rit != mapKeyFirstBlock.end() && nHeight < rit->second->nHeight)
                        rit->second = blit->second;
                }
                vAffected.clear();
            }
        }
    }

    // Extract block timestamps for those keys
    for (const auto& entry : mapKeyFirstBlock)
        mapKeyBirth[entry.first] = entry.second->nTime - 7200; // block times can be 2h off
}
