// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2012 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// PoS staking and stealth address management operations
// for the CWallet class. Split from wallet.cpp for maintainability.

#include "wallet.h"
#include "walletdb.h"
#include "txdb.h"
#include "stakedb.h"
#include "init.h"
#include "kernel.h"
#include "stealth.h"
#include "base58.h"
#include "arith_uint256.h"
#include "logging.h"
#include "smessage/smessage.h"

bool CWallet::NewStealthAddress(std::string& sError, std::string& sLabel, CStealthAddress& sxAddr)
{
    ec_secret scan_secret;
    ec_secret spend_secret;

    if (GenerateRandomSecret(scan_secret) != 0
        || GenerateRandomSecret(spend_secret) != 0)
    {
        sError = "GenerateRandomSecret failed.";
        LogPrintf("Error CWallet::NewStealthAddress - %s\n", sError.c_str());
        return false;
    };

    ec_point scan_pubkey, spend_pubkey;
    if (SecretToPublicKey(scan_secret, scan_pubkey) != 0)
    {
        sError = "Could not get scan public key.";
        LogPrintf("Error CWallet::NewStealthAddress - %s\n", sError.c_str());
        return false;
    };

    if (SecretToPublicKey(spend_secret, spend_pubkey) != 0)
    {
        sError = "Could not get spend public key.";
        LogPrintf("Error CWallet::NewStealthAddress - %s\n", sError.c_str());
        return false;
    };

    {
        LogPrint(BCLog::WALLET, "getnewstealthaddress: ");
        LogPrint(BCLog::WALLET, "scan_pubkey ");
        for (uint32_t i = 0; i < scan_pubkey.size(); ++i)
          LogPrint(BCLog::WALLET, "%02x", scan_pubkey[i]);
        LogPrint(BCLog::WALLET, "\n");

        LogPrint(BCLog::WALLET, "spend_pubkey ");
        for (uint32_t i = 0; i < spend_pubkey.size(); ++i)
          LogPrint(BCLog::WALLET, "%02x", spend_pubkey[i]);
        LogPrint(BCLog::WALLET, "\n");
    };


    sxAddr.label = sLabel;
    sxAddr.scan_pubkey = scan_pubkey;
    sxAddr.spend_pubkey = spend_pubkey;

    sxAddr.scan_secret.resize(32);
    memcpy(&sxAddr.scan_secret[0], &scan_secret.e[0], 32);
    sxAddr.spend_secret.resize(32);
    memcpy(&sxAddr.spend_secret[0], &spend_secret.e[0], 32);

    return true;
}

bool CWallet::AddStealthAddress(CStealthAddress& sxAddr)
{
    LOCK(cs_wallet);

    // must add before changing spend_secret
    stealthAddresses.insert(sxAddr);

    bool fOwned = sxAddr.scan_secret.size() == ec_secret_size;



    if (fOwned)
    {
        // -- owned addresses can only be added when wallet is unlocked
        if (IsLocked())
        {
            LogPrintf("Error: CWallet::AddStealthAddress wallet must be unlocked.\n");
            stealthAddresses.erase(sxAddr);
            return false;
        };

        if (IsCrypted())
        {
            std::vector<unsigned char> vchCryptedSecret;
            CSecret vchSecret;
            vchSecret.resize(32);
            memcpy(&vchSecret[0], &sxAddr.spend_secret[0], 32);

            // IV derived from public key — inherited Bitcoin Core design pattern.
            // Security comes from the master key, not IV randomness.
            // Changing this would break existing encrypted wallet.dat files.
            uint256 iv = Hash(sxAddr.spend_pubkey.begin(), sxAddr.spend_pubkey.end());
            if (!EncryptSecret(vMasterKey, vchSecret, iv, vchCryptedSecret))
            {
                LogPrintf("Error: Failed encrypting stealth key %s\n", sxAddr.Encoded().c_str());
                stealthAddresses.erase(sxAddr);
                return false;
            };
            sxAddr.spend_secret = vchCryptedSecret;
        };
    };


    bool rv = CWalletDB(strWalletFile).WriteStealthAddress(sxAddr);

    if (rv)
        NotifyAddressBookChanged(this, sxAddr, sxAddr.label, fOwned, CT_NEW);

    return rv;
}

bool CWallet::UnlockStealthAddresses(const CKeyingMaterial& vMasterKeyIn)
{
    // -- decrypt spend_secret of stealth addresses
    std::set<CStealthAddress>::iterator it;
    for (it = stealthAddresses.begin(); it != stealthAddresses.end(); ++it)
    {
        if (it->scan_secret.size() < 32)
            continue; // stealth address is not owned

        // -- CStealthAddress are only sorted on spend_pubkey
        CStealthAddress &sxAddr = const_cast<CStealthAddress&>(*it);

        LogPrint(BCLog::WALLET, "Decrypting stealth key %s\n", sxAddr.Encoded().c_str());

        CSecret vchSecret;
        // IV derived from public key — inherited Bitcoin Core design pattern.
        // Security comes from the master key, not IV randomness.
        // Changing this would break existing encrypted wallet.dat files.
        uint256 iv = Hash(sxAddr.spend_pubkey.begin(), sxAddr.spend_pubkey.end());
        if(!DecryptSecret(vMasterKeyIn, sxAddr.spend_secret, iv, vchSecret)
            || vchSecret.size() != 32)
        {
            LogPrintf("Error: Failed decrypting stealth key %s\n", sxAddr.Encoded().c_str());
            continue;
        };

        ec_secret testSecret;
        memcpy(&testSecret.e[0], &vchSecret[0], 32);
        ec_point pkSpendTest;

        if (SecretToPublicKey(testSecret, pkSpendTest) != 0
            || pkSpendTest != sxAddr.spend_pubkey)
        {
            LogPrintf("Error: Failed decrypting stealth key, public key mismatch %s\n", sxAddr.Encoded().c_str());
            continue;
        };

        sxAddr.spend_secret.resize(32);
        memcpy(&sxAddr.spend_secret[0], &vchSecret[0], 32);
    };

    CryptedKeyMap::iterator mi = mapCryptedKeys.begin();
    for (; mi != mapCryptedKeys.end(); ++mi)
    {
        CPubKey &pubKey = (*mi).second.first;
        std::vector<unsigned char> &vchCryptedSecret = (*mi).second.second;
        if (vchCryptedSecret.size() != 0)
            continue;

        CKeyID ckid = pubKey.GetID();
        CBitcoinAddress addr(ckid);

        auto mi = mapStealthKeyMeta.find(ckid);
        if (mi == mapStealthKeyMeta.end())
        {
            LogPrintf("Error: No metadata found to add secret for %s\n", addr.ToString().c_str());
            continue;
        };

        CStealthKeyMetadata& sxKeyMeta = mi->second;

        CStealthAddress sxFind;
        sxFind.scan_pubkey = sxKeyMeta.pkScan.Raw();

        auto si = stealthAddresses.find(sxFind);
        if (si == stealthAddresses.end())
        {
            LogPrint(BCLog::WALLET, "No stealth key found to add secret for %s\n", addr.ToString().c_str());
            continue;
        };

        LogPrint(BCLog::WALLET, "Expanding secret for %s\n", addr.ToString().c_str());

        ec_secret sSpendR;
        ec_secret sSpend;
        ec_secret sScan;

        if (si->spend_secret.size() != ec_secret_size
            || si->scan_secret.size() != ec_secret_size)
        {
            LogPrint(BCLog::WALLET, "Stealth address has no secret key for %s\n", addr.ToString().c_str());
            continue;
        }
        memcpy(&sScan.e[0], &si->scan_secret[0], ec_secret_size);
        memcpy(&sSpend.e[0], &si->spend_secret[0], ec_secret_size);

        ec_point pkEphem = sxKeyMeta.pkEphem.Raw();
        if (StealthSecretSpend(sScan, pkEphem, sSpend, sSpendR) != 0)
        {
            LogPrintf("StealthSecretSpend() failed.\n");
            continue;
        };

        ec_point pkTestSpendR;
        if (SecretToPublicKey(sSpendR, pkTestSpendR) != 0)
        {
            LogPrintf("SecretToPublicKey() failed.\n");
            continue;
        };

        CSecret vchSecret;
        vchSecret.resize(ec_secret_size);

        memcpy(&vchSecret[0], &sSpendR.e[0], ec_secret_size);
        CKey ckey;

        try {
            ckey.SetSecret(vchSecret, true);
        } catch (std::exception& e) {
            LogPrintf("ckey.SetSecret() threw: %s.\n", e.what());
            continue;
        };

        CPubKey cpkT = ckey.GetPubKey();

        if (!cpkT.IsValid())
        {
            LogPrintf("cpkT is invalid.\n");
            continue;
        };

        if (cpkT != pubKey)
        {
            LogPrintf("Error: Generated secret does not match.\n");
            LogPrint(BCLog::WALLET, "cpkT   %s\n", HexStr(cpkT.Raw()).c_str());
            LogPrint(BCLog::WALLET, "pubKey %s\n", HexStr(pubKey.Raw()).c_str());
            continue;
        };

        if (!ckey.IsValid())
        {
            LogPrintf("Reconstructed key is invalid.\n");
            continue;
        };

        {
            CKeyID keyID = cpkT.GetID();
            CBitcoinAddress coinAddress(keyID);
            LogPrint(BCLog::WALLET, "Adding secret to key %s.\n", coinAddress.ToString().c_str());
        };

        if (!AddKey(ckey))
        {
            LogPrintf("AddKey failed.\n");
            continue;
        };

        if (!CWalletDB(strWalletFile).EraseStealthKeyMeta(ckid))
            LogPrintf("EraseStealthKeyMeta failed for %s\n", addr.ToString().c_str());
    };
    return true;
}

bool CWallet::UpdateStealthAddress(std::string &addr, std::string &label, bool addIfNotExist)
{
    LogPrint(BCLog::WALLET, "UpdateStealthAddress %s\n", addr.c_str());


    CStealthAddress sxAddr;

    if (!sxAddr.SetEncoded(addr))
        return false;

    std::set<CStealthAddress>::iterator it;
    it = stealthAddresses.find(sxAddr);

    ChangeType nMode = CT_UPDATED;
    CStealthAddress sxFound;
    if (it == stealthAddresses.end())
    {
        if (addIfNotExist)
        {
            sxFound = sxAddr;
            sxFound.label = label;
            stealthAddresses.insert(sxFound);
            nMode = CT_NEW;
        } else
        {
            LogPrint(BCLog::WALLET, "UpdateStealthAddress %s, not in set\n", addr.c_str());
            return false;
        };
    } else
    {
        sxFound = const_cast<CStealthAddress&>(*it);

        if (sxFound.label == label)
        {
            // no change
            return true;
        };

        it->label = label; // update in .stealthAddresses

        if (sxFound.scan_secret.size() == ec_secret_size)
        {
            LogPrint(BCLog::WALLET, "UpdateStealthAddress: todo - update owned stealth address.\n");
            return false;
        };
    };

    sxFound.label = label;

    if (!CWalletDB(strWalletFile).WriteStealthAddress(sxFound))
    {
        LogPrintf("UpdateStealthAddress(%s) Write to db failed.\n", addr.c_str());
        return false;
    };

    bool fOwned = sxFound.scan_secret.size() == ec_secret_size;
    NotifyAddressBookChanged(this, sxFound, sxFound.label, fOwned, nMode);

    return true;
}

bool CWallet::FindStealthTransactions(const CTransaction& tx, mapValue_t& mapNarr)
{
    // Do not check Coinbase or Coinstake for stealth transactions.
    if (tx.IsCoinBase() || tx.IsCoinStake())
    {
        return false;
    }

    LogPrint(BCLog::WALLET, "FindStealthTransactions() tx: %s\n", tx.GetHash().GetHex().c_str());

    mapNarr.clear();

    LOCK(cs_wallet);
    ec_secret sSpendR;
    ec_secret sSpend;
    ec_secret sScan;
    ec_secret sShared;

    ec_point pkExtracted;

    std::vector<uint8_t> vchEphemPK;
    std::vector<uint8_t> vchDataB;
    std::vector<uint8_t> vchENarr;
    opcodetype opCode;
    char cbuf[256];

    int32_t nOutputIdOuter = -1;
    for (const CTxOut& txout : tx.vout)
    {
        nOutputIdOuter++;
        // -- for each OP_RETURN need to check all other valid outputs

        //printf("txout scriptPubKey %s\n",  txout.scriptPubKey.ToString().c_str());
        CScript::const_iterator itTxA = txout.scriptPubKey.begin();

        if (!txout.scriptPubKey.GetOp(itTxA, opCode, vchEphemPK)
            || opCode != OP_RETURN)
            continue;
        else
        if (!txout.scriptPubKey.GetOp(itTxA, opCode, vchEphemPK)
            || vchEphemPK.size() != 33)
        {
            // -- look for plaintext notes
            if (vchEphemPK.size() > 1
                && vchEphemPK[0] == 'n'
                && vchEphemPK[1] == 'p')
            {
                if (txout.scriptPubKey.GetOp(itTxA, opCode, vchENarr)
                    && opCode == OP_RETURN
                    && txout.scriptPubKey.GetOp(itTxA, opCode, vchENarr)
                    && vchENarr.size() > 0)
                {
                    std::string sNarr = std::string(vchENarr.begin(), vchENarr.end());

                    snprintf(cbuf, sizeof(cbuf), "n_%d", nOutputIdOuter-1); // plaintext note always matches preceding value output
                    mapNarr[cbuf] = sNarr;
                } else
                {
                    LogPrintf("Warning: FindStealthTransactions() tx: %s, Could not extract plaintext note.\n", tx.GetHash().GetHex().c_str());
                };
            }

            continue;
        }

        int32_t nOutputId = -1;
        nStealth++;
        for (const CTxOut& txoutB : tx.vout)
        {
            nOutputId++;

            if (&txoutB == &txout)
                continue;

            bool txnMatch = false; // only 1 txn will match an ephem pk
            //printf("txoutB scriptPubKey %s\n",  txoutB.scriptPubKey.ToString().c_str());

            CTxDestination address;
            if (!ExtractDestination(txoutB.scriptPubKey, address))
                continue;

            if (!std::holds_alternative<CKeyID>(address))
                continue;

            CKeyID ckidMatch = std::get<CKeyID>(address);

            if (HaveKey(ckidMatch)) // no point checking if already have key
                continue;

            std::set<CStealthAddress>::iterator it;
            for (it = stealthAddresses.begin(); it != stealthAddresses.end(); ++it)
            {
                if (it->scan_secret.size() != ec_secret_size)
                    continue; // stealth address is not owned

                //printf("it->Encodeded() %s\n",  it->Encoded().c_str());
                memcpy(&sScan.e[0], &it->scan_secret[0], ec_secret_size);

                if (StealthSecret(sScan, vchEphemPK, it->spend_pubkey, sShared, pkExtracted) != 0)
                {
                    LogPrintf("StealthSecret failed.\n");
                    continue;
                };
                //printf("pkExtracted %u: %s\n", pkExtracted.size(), HexStr(pkExtracted).c_str());

                CPubKey cpkE(pkExtracted);

                if (!cpkE.IsValid())
                    continue;
                CKeyID ckidE = cpkE.GetID();

                if (ckidMatch != ckidE)
                    continue;

                LogPrint(BCLog::WALLET, "Found stealth txn to address %s\n", it->Encoded().c_str());

                if (IsLocked())
                {
                    LogPrint(BCLog::WALLET, "Wallet is locked, adding key without secret.\n");

                    // -- add key without secret
                    std::vector<uint8_t> vchEmpty;
                    AddCryptedKey(cpkE, vchEmpty);
                    CKeyID keyId = cpkE.GetID();
                    CBitcoinAddress coinAddress(keyId);
                    std::string sLabel = it->Encoded();
                    SetAddressBookName(keyId, sLabel);

                    CPubKey cpkEphem(vchEphemPK);
                    CPubKey cpkScan(it->scan_pubkey);
                    CStealthKeyMetadata lockedSkMeta(cpkEphem, cpkScan);

                    if (!CWalletDB(strWalletFile).WriteStealthKeyMeta(keyId, lockedSkMeta))
                        LogPrintf("WriteStealthKeyMeta failed for %s\n", coinAddress.ToString().c_str());

                    mapStealthKeyMeta[keyId] = lockedSkMeta;
                    nFoundStealth++;
                } else
                {
                    if (it->spend_secret.size() != ec_secret_size)
                        continue;
                    memcpy(&sSpend.e[0], &it->spend_secret[0], ec_secret_size);


                    if (StealthSharedToSecretSpend(sShared, sSpend, sSpendR) != 0)
                    {
                        LogPrintf("StealthSharedToSecretSpend() failed.\n");
                        continue;
                    };

                    ec_point pkTestSpendR;
                    if (SecretToPublicKey(sSpendR, pkTestSpendR) != 0)
                    {
                        LogPrintf("SecretToPublicKey() failed.\n");
                        continue;
                    };

                    CSecret vchSecret;
                    vchSecret.resize(ec_secret_size);

                    memcpy(&vchSecret[0], &sSpendR.e[0], ec_secret_size);
                    CKey ckey;

                    try {
                        ckey.SetSecret(vchSecret, true);
                    } catch (std::exception& e) {
                        LogPrintf("ckey.SetSecret() threw: %s.\n", e.what());
                        continue;
                    };

                    CPubKey cpkT = ckey.GetPubKey();
                    if (!cpkT.IsValid())
                    {
                        LogPrintf("cpkT is invalid.\n");
                        continue;
                    };

                    if (!ckey.IsValid())
                    {
                        LogPrintf("Reconstructed key is invalid.\n");
                        continue;
                    };

                    CKeyID keyID = cpkT.GetID();
                    {
                        CBitcoinAddress coinAddress(keyID);
                        LogPrint(BCLog::WALLET, "Adding key %s.\n", coinAddress.ToString().c_str());
                    };

                    if (!AddKey(ckey))
                    {
                        LogPrintf("AddKey failed.\n");
                        continue;
                    };

                    std::string sLabel = it->Encoded();
                    SetAddressBookName(keyID, sLabel);
                    nFoundStealth++;
                };

                if (txout.scriptPubKey.GetOp(itTxA, opCode, vchENarr)
                    && opCode == OP_RETURN
                    && txout.scriptPubKey.GetOp(itTxA, opCode, vchENarr)
                    && vchENarr.size() > 0)
                {
                    SecMsgCrypter crypter;
                    crypter.SetKey(&sShared.e[0], &vchEphemPK[0]);
                    std::vector<uint8_t> vchNarr;
                    if (!crypter.Decrypt(&vchENarr[0], vchENarr.size(), vchNarr))
                    {
                        LogPrintf("Decrypt note failed.\n");
                        continue;
                    };
                    std::string sNarr = std::string(vchNarr.begin(), vchNarr.end());

                    snprintf(cbuf, sizeof(cbuf), "n_%d", nOutputId);
                    mapNarr[cbuf] = sNarr;
                };

                txnMatch = true;
                break;
            };
            if (txnMatch)
                break;
        };
    };

    return true;
};



// NovaCoin: get current stake weight
bool CWallet::GetStakeWeight(const CKeyStore& keystore, uint64_t& nMinWeight, uint64_t& nMaxWeight, uint64_t& nWeight)
{
    // Choose coins to use
    int64_t nBalance = GetBalance();

    if (nBalance <= nReserveBalance)
        return false;

    std::vector<const CWalletTx*> vwtxPrev;

    std::set<std::pair<const CWalletTx*,unsigned int> > setCoins;
    int64_t nValueIn = 0;


    if (!SelectCoinsForStaking(nBalance - nReserveBalance, GetAdjustedTime(), setCoins, nValueIn))
        return false;

    if (setCoins.empty())
        return false;


    nMinWeight = nMaxWeight = nWeight = 0;

    CTxDB txdb("r");
    for (auto pcoin : setCoins)
    {
        CTxIndex txindex;
        {
            LOCK2(cs_main, cs_wallet);
            if (!txdb.ReadTxIndex(pcoin.first->GetHash(), txindex))
                continue;
        }

        bool fFlashStake = (pindexBest->nTime > nTimeV231) && IsFlashStake(static_cast<unsigned int>(GetAdjustedTime()));

        int64_t nTimeWeight = GetWeight(static_cast<int64_t>(pcoin.first->nTime), static_cast<int64_t>(GetAdjustedTime()), fFlashStake);
        // Original: bnCoinDayWeight = nValue * nTimeWeight / COIN / (24 * 60 * 60)


        int64_t bnCoinDayWeight_Calc;

        int nDayTime = 24 * 60 * 60; // Length of a Day
        int64_t nValue = pcoin.first->vout[pcoin.second].nValue / COIN; // Less than 1 coin will never stake.

        // int64_t nDivideBase = nDayTime * CENT;
        bnCoinDayWeight_Calc = nValue * nTimeWeight / nDayTime;


        arith_uint256 bnCoinDayWeight(static_cast<uint64_t>(bnCoinDayWeight_Calc));

        // Weight is greater than zero
        if (nTimeWeight > 0)
        {
            nWeight += bnCoinDayWeight.GetLow64();
        }

                // Weight is greater than zero, but the maximum value isn't reached yet
                if (nTimeWeight > 0 && nTimeWeight < nStakeMaxAge)
                {
                    nMinWeight += bnCoinDayWeight.GetLow64();
                }

                // Maximum weight was reached
                if (nTimeWeight == nStakeMaxAge)
                {
                    nMaxWeight += bnCoinDayWeight.GetLow64();
                }
    }

    return true;
}

bool CWallet::CreateCoinStake(const CKeyStore& keystore, unsigned int nBits, int64_t nSearchInterval, int64_t nFees, CTransaction& txNew, CKey& key)
{
    CBlockIndex* pindexPrev = pindexBest;
    arith_uint256 bnTargetPerCoinDay;
    bnTargetPerCoinDay.SetCompact(nBits);

    txNew.vin.clear();
    txNew.vout.clear();

    // Mark coin stake transaction
    CScript scriptEmpty;
    scriptEmpty.clear();
    txNew.vout.push_back(CTxOut(0, scriptEmpty));

    // Choose coins to use
    int64_t nBalance = GetBalance();

    if (nBalance <= nReserveBalance)
        return false;

    std::vector<const CWalletTx*> vwtxPrev;

    std::set<std::pair<const CWalletTx*,unsigned int> > setCoins;
    int64_t nValueIn = 0;

    // Select coins with suitable depth
    if (!SelectCoinsForStaking(nBalance - nReserveBalance, txNew.nTime, setCoins, nValueIn))
        return false;

    if (setCoins.empty())
        return false;

    int64_t nCredit = 0;
    CScript scriptPubKeyKernel;

   // CScript scriptD4L;
   // scriptD4L.SetDestination(addrD4L.Get());


    CTxDB txdb("r");
    for (auto pcoin : setCoins)
    {
        CTxIndex txindex;
        {
            LOCK2(cs_main, cs_wallet);
            if (!txdb.ReadTxIndex(pcoin.first->GetHash(), txindex))
                continue;
        }

        // Read block header
        CBlock block;
        {
            LOCK2(cs_main, cs_wallet);
            if (!block.ReadFromDisk(txindex.pos.nFile, txindex.pos.nBlockPos, false))
                continue;
        }

        static int nMaxStakeSearchInterval = 60;

        unsigned int nStakeMinAgeCurrent = nStakeMinAge;

        if (block.GetBlockTime() + nStakeMinAgeCurrent > txNew.nTime - nMaxStakeSearchInterval)
            continue; // only count coins meeting min age requirement

        bool fKernelFound = false;
        for (unsigned int n=0; n<std::min(nSearchInterval,static_cast<int64_t>(nMaxStakeSearchInterval)) && !fKernelFound && !fShutdown && pindexPrev == pindexBest; n++)
        {
            // Search backward in time from the given txNew timestamp
            // Search nSearchInterval seconds back up to nMaxStakeSearchInterval
            uint256 hashProofOfStake = 0, targetProofOfStake = 0;
            COutPoint prevoutStake = COutPoint(pcoin.first->GetHash(), pcoin.second);
            if (CheckStakeKernelHash(nBits, block, txindex.pos.nTxPos - txindex.pos.nBlockPos, *pcoin.first, prevoutStake, txNew.nTime - n, hashProofOfStake, targetProofOfStake))
            {
                // Found a kernel
                LogPrint(BCLog::STAKE, "CreateCoinStake : kernel found\n");
                std::vector<valtype> vSolutions;
                txnouttype whichType;
                CScript scriptPubKeyOut;
                scriptPubKeyKernel = pcoin.first->vout[pcoin.second].scriptPubKey;
                if (!Solver(scriptPubKeyKernel, whichType, vSolutions))
                {
                    LogPrint(BCLog::STAKE, "CreateCoinStake : failed to parse kernel\n");
                    break;
                }
                LogPrint(BCLog::STAKE, "CreateCoinStake : parsed kernel type=%d\n", whichType);
                if (whichType != TX_PUBKEY && whichType != TX_PUBKEYHASH)
                {
                    LogPrint(BCLog::STAKE, "CreateCoinStake : no support for kernel type=%d\n", whichType);
                    break;  // only support pay to public key and pay to address
                }
                if (whichType == TX_PUBKEYHASH) // pay to address type
                {
                    // convert to pay to public key type
                    if (!keystore.GetKey(uint160(vSolutions[0]), key))
                    {
                        LogPrint(BCLog::STAKE, "CreateCoinStake : failed to get key for kernel type=%d\n", whichType);
                        break;  // unable to find corresponding public key
                    }
                    scriptPubKeyOut << key.GetPubKey() << OP_CHECKSIG;
                }
                if (whichType == TX_PUBKEY)
                {
                    valtype& vchPubKey = vSolutions[0];
                    if (!keystore.GetKey(Hash160(vchPubKey), key))
                    {
                        LogPrint(BCLog::STAKE, "CreateCoinStake : failed to get key for kernel type=%d\n", whichType);
                        break;  // unable to find corresponding public key
                    }

                    if (key.GetPubKey() != vchPubKey)
                    {
                        LogPrint(BCLog::STAKE, "CreateCoinStake : invalid key for kernel type=%d\n", whichType);
                        break; // keys mismatch
                    }

                    scriptPubKeyOut = scriptPubKeyKernel;
                }

                txNew.nTime -= n;
                txNew.vin.push_back(CTxIn(pcoin.first->GetHash(), pcoin.second));
                nCredit += pcoin.first->vout[pcoin.second].nValue;
                vwtxPrev.push_back(pcoin.first);
                txNew.vout.push_back(CTxOut(0, scriptPubKeyOut));

                //decide whether to split the stake into two outputs - presstab
				uint64_t nCoinAge;
				CTxDB txdb("r");
				if (txNew.GetCoinAge(txdb, nCoinAge))
				{
                    int64_t nTotalSize = pcoin.first->vout[pcoin.second].nValue + GetProofOfStakeReward(nCoinAge, 0, pindexBest->nHeight + 1, txNew.nTime);
                    if (nTotalSize > nSplitThreshold * COIN)
						txNew.vout.push_back(CTxOut(0, scriptPubKeyOut)); //split stake
				}


                LogPrint(BCLog::STAKE, "CreateCoinStake : added kernel type=%d\n", whichType);
                fKernelFound = true;
                break;
            }
        }

        if (fKernelFound || fShutdown)
            break; // if kernel is found stop searching
    }

    if (nCredit == 0 || nCredit > nBalance - nReserveBalance)
        return false;

    for (auto pcoin : setCoins)
    {
        // Attempt to add more inputs
        // Only add coins of the same key/address as kernel
        if (txNew.vout.size() == 2 && ((pcoin.first->vout[pcoin.second].scriptPubKey == scriptPubKeyKernel || pcoin.first->vout[pcoin.second].scriptPubKey == txNew.vout[1].scriptPubKey))
            && pcoin.first->GetHash() != txNew.vin[0].prevout.hash)
        {
            bool fFlashStake = (pindexBest->nTime > nTimeV231) && IsFlashStake(static_cast<unsigned int>(GetAdjustedTime()));

            int64_t nTimeWeight = GetWeight(static_cast<int64_t>(pcoin.first->nTime), static_cast<int64_t>(txNew.nTime), fFlashStake);

            // Stop adding more inputs if already too many inputs
            if (txNew.vin.size() >= 100)
                break;
            // Stop adding more inputs if value is already pretty significant
            if (nCredit >= (nCombineThreshold * COIN))
                break;
            // Stop adding inputs if reached reserve limit
            if (nCredit + pcoin.first->vout[pcoin.second].nValue > nBalance - nReserveBalance)
                break;
            // Do not add additional significant input
            if (pcoin.first->vout[pcoin.second].nValue >= (nCombineThreshold * COIN))
                continue;
            // Do not add input that is still too young
            unsigned int nStakeMinAgeCurrent = nStakeMinAge;

            if (nTimeWeight < nStakeMinAgeCurrent)
                continue;

            txNew.vin.push_back(CTxIn(pcoin.first->GetHash(), pcoin.second));
            nCredit += pcoin.first->vout[pcoin.second].nValue;
            vwtxPrev.push_back(pcoin.first);
        }
    }

    // Calculate coin age reward

    int64_t nReward;
    {
        uint64_t nCoinAge;
        CTxDB txdb("r");
        if (!txNew.GetCoinAge(txdb, nCoinAge))
            return error("CreateCoinStake : failed to calculate coin age");

        nReward = GetProofOfStakeReward(nCoinAge, nFees, pindexBest->nHeight + 1, txNew.nTime);
        if (nReward <= 0 && pindexBest->nHeight > 16240)
            return false;


    }

    int64_t stakeOutCount = CountStakeOut();
    int64_t stakeOutReward =  AggregateStakeOut(txNew, nReward);

    nCredit += nReward - stakeOutReward;
    // Set output amount
    if (txNew.vout.size() == static_cast<uint64_t>(3 + stakeOutCount))
    {
        txNew.vout[1].nValue = ((nCredit) / 2 / CENT) * CENT;
        txNew.vout[2].nValue = (nCredit) - txNew.vout[1].nValue;

    }
    else {
        txNew.vout[1].nValue = nCredit;
    }


    // Sign
    int nIn = 0;
    for (const CWalletTx* pcoin : vwtxPrev)
    {
        if (!SignSignature(*this, *pcoin, txNew, nIn++))
            return error("CreateCoinStake : failed to sign coinstake");
    }

    // Limit size
    unsigned int nBytes = ::GetSerializeSize(txNew, SER_NETWORK, PROTOCOL_VERSION);
    if (nBytes >= MAX_BLOCK_SIZE_GEN/5)
        return error("CreateCoinStake : exceeded coinstake size limit");

    // Successfully generated coinstake
    return true;
}

int64_t CWallet::AggregateStakeOut(CTransaction &txNew, int64_t &nReward)
{
    double percentTotal = 0;
    int64_t nRewardPCTotal = 0;

    for (mapAddress mapStake : pstakeDB->mapAddressPercent)
    {
        double nPercent = atof(mapStake.second.c_str());
        CBitcoinAddress stakeOutAddress(mapStake.first);
        if (nPercent > 0 && nPercent <= 100 && stakeOutAddress.IsValid())
        {
            percentTotal += nPercent;
            if (percentTotal <= 100)
            {
                int64_t nRewardPC = (nReward * nPercent) / 100;
                nRewardPCTotal += nRewardPC;

                CScript stakeOut;
                stakeOut.SetDestination(stakeOutAddress.Get());

                txNew.vout.push_back(CTxOut(nRewardPC, stakeOut));
            } else {
                percentTotal -= nPercent; // Attempted to stakeout over 100%
            }
        }
    }

    return nRewardPCTotal;
}

int64_t CWallet::CountStakeOut()
{
    int64_t countOut = 0;
    double percentTotal = 0;

    for (mapAddress mapStake : pstakeDB->mapAddressPercent)
    {
        double nPercent = atof(mapStake.second.c_str());
        CBitcoinAddress stakeOutAddress(mapStake.first);
        if (nPercent > 0 && nPercent <= 100 && stakeOutAddress.IsValid())
        {
            percentTotal += nPercent;
            if (percentTotal <= 100) countOut++;
        }
    }
    return countOut;
}
