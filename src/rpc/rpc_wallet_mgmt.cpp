// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2012 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// RPC wallet commands: encryption, backup, staking config, scanning.

#include "wallet.h"
#include "walletdb.h"
#include "stakedb.h"
#include "db_cursor_guard.h"
#include "bitcoinrpc.h"
#include "init.h"
#include "base58.h"

using namespace json_spirit;


static CCriticalSection cs_nWalletUnlockTime;

Value backupwallet(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw std::runtime_error(
            "backupwallet <destination>\n"
            "Safely copies wallet.dat to destination, which can be a directory or a path with filename.");

    std::string strDest = params[0].get_str();
    if (!BackupWallet(*pwalletMain, strDest))
        throw JSONRPCError(RPC_WALLET_ERROR, "Error: Wallet backup failed!");

    return Value::null;
}


Value keypoolrefill(const Array& params, bool fHelp)
{
    if (fHelp || params.size() > 1)
        throw std::runtime_error(
            "keypoolrefill [new-size]\n"
            "Fills the keypool."
            + HelpRequiringPassphrase());

    unsigned int nSize = std::max(GetArg("-keypool", 100), static_cast<int64_t>(0));
    if (!params.empty()) {
        if (params[0].get_int() < 0)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, expected valid size");
        nSize = static_cast<unsigned int>(params[0].get_int());
    }

    EnsureWalletIsUnlocked();

    pwalletMain->TopUpKeyPool(nSize);

    if (pwalletMain->GetKeyPoolSize() < nSize)
        throw JSONRPCError(RPC_WALLET_ERROR, "Error refreshing keypool.");

    return Value::null;
}


void ThreadTopUpKeyPool(void* parg)
{
    // Make this thread recognisable as the key-topping-up thread
    RenameThread("pinkcoin-key-top");

    pwalletMain->TopUpKeyPool();
}

void ThreadCleanWalletPassphrase(void* parg)
{
    // Make this thread recognisable as the wallet relocking thread
    RenameThread("pinkcoin-lock-wa");

    std::unique_ptr<int64_t> pSleepTime(static_cast<int64_t*>(parg));
    int64_t nMyWakeTime = GetTimeMillis() + *pSleepTime * 1000;

    ENTER_CRITICAL_SECTION(cs_nWalletUnlockTime);

    if (nWalletUnlockTime == 0)
    {
        nWalletUnlockTime = nMyWakeTime;

        do
        {
            if (nWalletUnlockTime==0)
                break;
            int64_t nToSleep = nWalletUnlockTime - GetTimeMillis();
            if (nToSleep <= 0)
                break;

            LEAVE_CRITICAL_SECTION(cs_nWalletUnlockTime);
            MilliSleep(nToSleep);
            ENTER_CRITICAL_SECTION(cs_nWalletUnlockTime);

        } while(1);

        if (nWalletUnlockTime)
        {
            nWalletUnlockTime = 0;
            pwalletMain->Lock();
        }
    }
    else
    {
        if (nWalletUnlockTime < nMyWakeTime)
            nWalletUnlockTime = nMyWakeTime;
    }

    LEAVE_CRITICAL_SECTION(cs_nWalletUnlockTime);
}

Value walletpassphrase(const Array& params, bool fHelp)
{
    if (pwalletMain->IsCrypted() && (fHelp || params.size() < 2 || params.size() > 3))
        throw std::runtime_error(
            "walletpassphrase <passphrase> <timeout> [stakingonly]\n"
            "Stores the wallet decryption key in memory for <timeout> seconds.\n"
            "if [stakingonly] is true sending functions are disabled.");
    if (fHelp)
        return true;
    if (!pwalletMain->IsCrypted())
        throw JSONRPCError(RPC_WALLET_WRONG_ENC_STATE, "Error: running with an unencrypted wallet, but walletpassphrase was called.");

    if (!pwalletMain->IsLocked())
        throw JSONRPCError(RPC_WALLET_ALREADY_UNLOCKED, "Error: Wallet is already unlocked, use walletlock first if need to change unlock settings.");

    int64_t nSleepTime = params[1].get_int64();
    if (nSleepTime <= 0 || nSleepTime >= std::numeric_limits<int64_t>::max() / 1000000000)
        throw std::runtime_error("timeout is out of bounds");

    // Note that the walletpassphrase is stored in params[0] which is not mlock()ed
    SecureString strWalletPass;
    strWalletPass.reserve(100);
    strWalletPass = params[0].get_str().c_str();

    // Cleanse non-mlocked source to prevent paging passphrase to swap
    {
        std::string& src = const_cast<std::string&>(params[0].get_str());
        OPENSSL_cleanse(src.data(), src.size());
    }

    if (strWalletPass.length() > 0)
    {
        if (!pwalletMain->Unlock(strWalletPass))
            throw JSONRPCError(RPC_WALLET_PASSPHRASE_INCORRECT, "Error: The wallet passphrase entered was incorrect.");
    }
    else
        throw std::runtime_error(
            "walletpassphrase <passphrase> <timeout>\n"
            "Stores the wallet decryption key in memory for <timeout> seconds.");

    NewThread(ThreadTopUpKeyPool, nullptr);
    int64_t* pnSleepTime = new int64_t(nSleepTime);
    NewThread(ThreadCleanWalletPassphrase, pnSleepTime);

    // ppcoin: if user OS account compromised prevent trivial sendmoney commands
    if (params.size() > 2)
        fWalletUnlockStakingOnly = params[2].get_bool();
    else
        fWalletUnlockStakingOnly = false;

    return Value::null;
}


Value walletpassphrasechange(const Array& params, bool fHelp)
{
    if (pwalletMain->IsCrypted() && (fHelp || params.size() != 2))
        throw std::runtime_error(
            "walletpassphrasechange <oldpassphrase> <newpassphrase>\n"
            "Changes the wallet passphrase from <oldpassphrase> to <newpassphrase>.");
    if (fHelp)
        return true;
    if (!pwalletMain->IsCrypted())
        throw JSONRPCError(RPC_WALLET_WRONG_ENC_STATE, "Error: running with an unencrypted wallet, but walletpassphrasechange was called.");

    SecureString strOldWalletPass;
    strOldWalletPass.reserve(100);
    strOldWalletPass = params[0].get_str().c_str();

    SecureString strNewWalletPass;
    strNewWalletPass.reserve(100);
    strNewWalletPass = params[1].get_str().c_str();

    // Cleanse non-mlocked sources to prevent paging passphrases to swap
    {
        std::string& src0 = const_cast<std::string&>(params[0].get_str());
        OPENSSL_cleanse(src0.data(), src0.size());
        std::string& src1 = const_cast<std::string&>(params[1].get_str());
        OPENSSL_cleanse(src1.data(), src1.size());
    }

    if (strOldWalletPass.length() < 1 || strNewWalletPass.length() < 1)
        throw std::runtime_error(
            "walletpassphrasechange <oldpassphrase> <newpassphrase>\n"
            "Changes the wallet passphrase from <oldpassphrase> to <newpassphrase>.");

    if (!pwalletMain->ChangeWalletPassphrase(strOldWalletPass, strNewWalletPass))
        throw JSONRPCError(RPC_WALLET_PASSPHRASE_INCORRECT, "Error: The wallet passphrase entered was incorrect.");

    return Value::null;
}


Value walletlock(const Array& params, bool fHelp)
{
    if (pwalletMain->IsCrypted() && (fHelp || !params.empty()))
        throw std::runtime_error(
            "walletlock\n"
            "Removes the wallet encryption key from memory, locking the wallet.\n"
            "After calling this method, you will need to call walletpassphrase again\n"
            "before being able to call any methods which require the wallet to be unlocked.");
    if (fHelp)
        return true;
    if (!pwalletMain->IsCrypted())
        throw JSONRPCError(RPC_WALLET_WRONG_ENC_STATE, "Error: running with an unencrypted wallet, but walletlock was called.");

    {
        LOCK(cs_nWalletUnlockTime);
        pwalletMain->Lock();
        nWalletUnlockTime = 0;
    }

    return Value::null;
}


Value encryptwallet(const Array& params, bool fHelp)
{
    if (!pwalletMain->IsCrypted() && (fHelp || params.size() != 1))
        throw std::runtime_error(
            "encryptwallet <passphrase>\n"
            "Encrypts the wallet with <passphrase>.");
    if (fHelp)
        return true;
    if (pwalletMain->IsCrypted())
        throw JSONRPCError(RPC_WALLET_WRONG_ENC_STATE, "Error: running with an encrypted wallet, but encryptwallet was called.");

    SecureString strWalletPass;
    strWalletPass.reserve(100);
    strWalletPass = params[0].get_str().c_str();

    // Cleanse non-mlocked source to prevent paging passphrase to swap
    {
        std::string& src = const_cast<std::string&>(params[0].get_str());
        OPENSSL_cleanse(src.data(), src.size());
    }

    if (strWalletPass.length() < 1)
        throw std::runtime_error(
            "encryptwallet <passphrase>\n"
            "Encrypts the wallet with <passphrase>.");

    if (!pwalletMain->EncryptWallet(strWalletPass))
        throw JSONRPCError(RPC_WALLET_ENCRYPTION_FAILED, "Error: Failed to encrypt the wallet.");

    // BDB seems to have a bad habit of writing old data into
    // slack space in .dat files; that is bad if the old data is
    // unencrypted private keys. So:
    StartShutdown();
    return "wallet encrypted; Pinkcoin server stopping, restart to run with encrypted wallet.  The keypool has been flushed, you need to make a new backup.";
}

// ppcoin: reserve balance from being staked for network protection
Value reservebalance(const Array& params, bool fHelp)
{
    if (fHelp || params.size() > 2)
        throw std::runtime_error(
            "reservebalance [<reserve> [amount]]\n"
            "<reserve> is true or false to turn balance reserve on or off.\n"
            "<amount> is a real and rounded to cent.\n"
            "Set reserve amount not participating in network protection.\n"
            "If no parameters provided current setting is printed.\n");

    if (!params.empty())
    {
        bool fReserve = params[0].get_bool();
        if (fReserve)
        {
            if (params.size() == 1)
                throw std::runtime_error("must provide amount to reserve balance.\n");
            int64_t nAmount = AmountFromValue(params[1]);
            nAmount = (nAmount / CENT) * CENT;  // round to cent
            if (nAmount < 0)
                throw std::runtime_error("amount cannot be negative.\n");
            nReserveBalance = nAmount;
        }
        else
        {
            if (params.size() > 1)
                throw std::runtime_error("cannot specify amount to turn off reserve.\n");
            nReserveBalance = 0;
        }
    }

    Object result;
    result.push_back(Pair("reserve", (nReserveBalance > 0)));
    result.push_back(Pair("amount", ValueFromAmount(nReserveBalance)));
    return result;
}


// Set the minimum coin chunk size.
// Stakes of coins smaller than this will be combined with other chunks in the wallet.
Value combinethreshold(const Array& params, bool fHelp)
{
    if (fHelp || params.size() > 1)
        throw std::runtime_error(
            "combinethreshold [amount]\n"
            "<amount> is a real and a whole number.\n"
            "Set minimum coin chunk amount before combining stakes.\n"
            "If no parameters provided current setting is printed.\n");

    if (!params.empty())
    {
       if (params.size() == 1)
        {
           int64_t nAmount = params[0].get_int64();
           if (nAmount < 100)
               throw std::runtime_error("Cannot set combine threshold lower than 100 coins.\n");
           if (nAmount >= nSplitThreshold)
               throw std::runtime_error("Combine threshold must be half the combined threshold or less.\n");
           nCombineThreshold = nAmount;
        }
    }

    Object result;
    result.push_back(Pair("combine threshold", nCombineThreshold));
    return result;
}

// Set the maximum coin chunk size.
// Stakes of coins larger than this will be split evenly into two chunks in the wallet.
Value splitthreshold(const Array& params, bool fHelp)
{
    if (fHelp || params.size() > 1)
        throw std::runtime_error(
            "splitthreshold [amount]\n"
            "<amount> is a real and a whole number.\n"
            "Set maximum coin chunk amount before splitting stakes.\n"
            "If no parameters provided current setting is printed.\n");

    if (!params.empty())
    {
       if (params.size() == 1)
        {
           int64_t nAmount = params[0].get_int64();
           if (nAmount > 1000000)
               throw std::runtime_error("Cannot set split threshold higher than 1000000 coins.\n");
           if (nAmount <= nCombineThreshold)
               throw std::runtime_error("Split threshold must be more than combined threshold.\n");
           nSplitThreshold = nAmount;
        }
    }

    Object result;
    result.push_back(Pair("split threshold", nSplitThreshold));
    return result;
}

// ppcoin: check wallet integrity
Value checkwallet(const Array& params, bool fHelp)
{
    if (fHelp || !params.empty())
        throw std::runtime_error(
            "checkwallet\n"
            "Check wallet for integrity.\n");

    int nMismatchSpent;
    int64_t nBalanceInQuestion;
    int nOrphansFound;
    pwalletMain->FixSpentCoins(nMismatchSpent, nBalanceInQuestion, nOrphansFound, true);
    Object result;
    if (nMismatchSpent == 0)
        result.push_back(Pair("wallet check passed", true));
    else
    {
        result.push_back(Pair("mismatched spent coins", nMismatchSpent));
        result.push_back(Pair("amount in question", ValueFromAmount(nBalanceInQuestion)));
    }
    return result;
}


// ppcoin: repair wallet
Value repairwallet(const Array& params, bool fHelp)
{
    if (fHelp || !params.empty())
        throw std::runtime_error(
            "repairwallet\n"
            "Repair wallet if checkwallet reports any problem.\n");

    int nMismatchSpent;
    int64_t nBalanceInQuestion;
    int nOrphansFound;
    pwalletMain->FixSpentCoins(nMismatchSpent, nBalanceInQuestion, nOrphansFound, false);
    Object result;
    if (nMismatchSpent == 0)
        result.push_back(Pair("wallet check passed", true));
    else
    {
        result.push_back(Pair("mismatched spent coins", nMismatchSpent));
        result.push_back(Pair("amount affected by repair", ValueFromAmount(nBalanceInQuestion)));
    }
    return result;
}

// NovaCoin: resend unconfirmed wallet transactions
Value resendtx(const Array& params, bool fHelp)
{
    if (fHelp || params.size() > 1)
        throw std::runtime_error(
            "resendtx\n"
            "Re-send unconfirmed transactions.\n"
        );

    ResendWalletTransactions(true);

    return Value::null;
}

Value clearwallettransactions(const Array& params, bool fHelp)
{
    if (fHelp || !params.empty())
        throw std::runtime_error(
            "clearwallettransactions \n"
            "delete all transactions from wallet - reload with scanforalltxns\n"
            "Warning: Backup your wallet first!");



    Object result;

    uint32_t nTransactions = 0;

    char cbuf[256];

    {
        LOCK2(cs_main, pwalletMain->cs_wallet);

        CWalletDB walletdb(pwalletMain->strWalletFile);
        walletdb.TxnBegin();
        BdbCursorGuard cursor(walletdb.GetTxnCursor());
        if (!cursor)
            throw std::runtime_error("Cannot get wallet DB cursor");

        Dbt datKey;
        Dbt datValue;

        datKey.set_flags(DB_DBT_USERMEM);
        datValue.set_flags(DB_DBT_USERMEM);

        std::vector<unsigned char> vchKey;
        std::vector<unsigned char> vchType;
        std::vector<unsigned char> vchKeyData;
        std::vector<unsigned char> vchValueData;

        vchKeyData.resize(100);
        vchValueData.resize(100);

        datKey.set_ulen(vchKeyData.size());
        datKey.set_data(&vchKeyData[0]);

        datValue.set_ulen(vchValueData.size());
        datValue.set_data(&vchValueData[0]);

        unsigned int fFlags = DB_NEXT; // same as using DB_FIRST for new cursor
        while (true)
        {
            int ret = cursor.get()->get(&datKey, &datValue, fFlags);

            if (ret == ENOMEM
                || ret == DB_BUFFER_SMALL)
            {
                if (datKey.get_size() > datKey.get_ulen())
                {
                    vchKeyData.resize(datKey.get_size());
                    datKey.set_ulen(vchKeyData.size());
                    datKey.set_data(&vchKeyData[0]);
                };

                if (datValue.get_size() > datValue.get_ulen())
                {
                    vchValueData.resize(datValue.get_size());
                    datValue.set_ulen(vchValueData.size());
                    datValue.set_data(&vchValueData[0]);
                };
                // -- try once more, when DB_BUFFER_SMALL cursor is not expected to move
                ret = cursor.get()->get(&datKey, &datValue, fFlags);
            };

            if (ret == DB_NOTFOUND)
                break;
            else
            if (datKey.get_data() == nullptr || datValue.get_data() == nullptr
                || ret != 0)
            {
                snprintf(cbuf, sizeof(cbuf), "wallet DB error %d, %s", ret, db_strerror(ret));
                throw std::runtime_error(cbuf);
            };

            CDataStream ssValue(SER_DISK, CLIENT_VERSION);
            ssValue.SetType(SER_DISK);
            ssValue.clear();
            ssValue.write(reinterpret_cast<char*>(datKey.get_data()), datKey.get_size());

            ssValue >> vchType;


            std::string strType(vchType.begin(), vchType.end());

            //printf("strType %s\n", strType.c_str());

            if (strType == "tx")
            {
                uint256 hash;
                ssValue >> hash;

                if ((ret = cursor.get()->del(0)) != 0)
                {
                    printf("Delete transaction failed %d, %s\n", ret, db_strerror(ret));
                    continue;
                };

                pwalletMain->mapWallet.erase(hash);
                pwalletMain->NotifyTransactionChanged(pwalletMain, hash, CT_DELETED);

                nTransactions++;
            };
        };
        // cursor closed by BdbCursorGuard destructor
        walletdb.TxnCommit();


        //pwalletMain->mapWallet.clear();
    }

    snprintf(cbuf, sizeof(cbuf), "Removed %u transactions.", nTransactions);
    result.push_back(Pair("complete", std::string(cbuf)));
    result.push_back(Pair("", "Reload with scanforstealthtxns or re-download blockchain."));


    return result;
}

Value scanforalltxns(const Array& params, bool fHelp)
{
    if (fHelp || params.size() > 1)
        throw std::runtime_error(
            "scanforalltxns [fromHeight]\n"
            "Scan blockchain for owned transactions.");

    Object result;
    int32_t nFromHeight = 0;

    CBlockIndex *pindex = pindexGenesisBlock;


    if (!params.empty())
        nFromHeight = params[0].get_int();


    if (nFromHeight > 0)
    {
        pindex = mapBlockIndex[hashBestChain];
        while (pindex->nHeight > nFromHeight
            && pindex->pprev)
            pindex = pindex->pprev;
    };

    if (pindex == nullptr)
        throw std::runtime_error("Genesis Block is not set.");

    {
        LOCK2(cs_main, pwalletMain->cs_wallet);

        pwalletMain->MarkDirty();

        pwalletMain->ScanForWalletTransactions(pindex, true);
        pwalletMain->ReacceptWalletTransactions();
    }

    result.push_back(Pair("result", "Scan complete."));

    return result;
}

Value scanforstealthtxns(const Array& params, bool fHelp)
{
    if (fHelp || params.size() > 1)
        throw std::runtime_error(
            "scanforstealthtxns [fromHeight]\n"
            "Scan blockchain for owned stealth transactions.");

    Object result;
    uint32_t nBlocks = 0;
    uint32_t nTransactions = 0;
    int32_t nFromHeight = 0;

    CBlockIndex *pindex = pindexGenesisBlock;


    if (!params.empty())
        nFromHeight = params[0].get_int();


    if (nFromHeight > 0)
    {
        pindex = mapBlockIndex[hashBestChain];
        while (pindex->nHeight > nFromHeight
            && pindex->pprev)
            pindex = pindex->pprev;
    };

    if (pindex == nullptr)
        throw std::runtime_error("Genesis Block is not set.");

    // -- locks in AddToWalletIfInvolvingMe

    bool fUpdate = true; // todo: option?

    pwalletMain->nStealth = 0;
    pwalletMain->nFoundStealth = 0;

    while (pindex)
    {
        nBlocks++;
        CBlock block;
        block.ReadFromDisk(pindex, true);

        for (CTransaction& tx : block.vtx)
        {
            if (!tx.IsStandard())
                continue; // leave out coinbase and others
            nTransactions++;

            pwalletMain->AddToWalletIfInvolvingMe(tx, &block, fUpdate);
        };

        pindex = pindex->pnext;
    };

    printf("Scanned %u blocks, %u transactions\n", nBlocks, nTransactions);
    printf("Found %u stealth transactions in blockchain.\n", pwalletMain->nStealth);
    printf("Found %u new owned stealth transactions.\n", pwalletMain->nFoundStealth);

    char cbuf[256];
    snprintf(cbuf, sizeof(cbuf), "%u new stealth transactions.", pwalletMain->nFoundStealth);

    result.push_back(Pair("result", "Scan complete."));
    result.push_back(Pair("found", std::string(cbuf)));

    return result;
}

// presstab HyperStake
Value setstakesplitthreshold(const Array& params, bool fHelp)
{

    if (fHelp || params.size() != 1)
        throw std::runtime_error(
            "setstakesplitthreshold <1 - 1000000>\n"
            "This will set the output size of your stakes to never be below this number\n"
            "Note: This function is depreciated in favor of splitthreshold [amount]\n");

    if (!params.empty())
    {
       if (params.size() == 1)
        {
           int64_t nAmount = AmountFromValue(params[0]);
           if (nAmount > 1000000)
               throw std::runtime_error("Cannot set combine threshold higher than 1000000 coins.\n");
           if (nAmount < nCombineThreshold * 2)
               throw std::runtime_error("Split threshold must be at least double the combined threshold.\n");
           nSplitThreshold = nAmount;
        }
    }

    Object result;
    result.push_back(Pair("split threshold", ValueFromAmount(nSplitThreshold)));
    return result;
}

// presstab HyperStake
Value getstakesplitthreshold(const Array& params, bool fHelp)
{
    if (fHelp || !params.empty())
        throw std::runtime_error(
            "getstakesplitthreshold\n"
            "Returns the set splitstakethreshold\n"
            "Note: This function is depreciated in favor of splitthreshold\n");

    Object result;
    result.push_back(Pair("split threshold", ValueFromAmount(nSplitThreshold)));
    return result;
}

Value addstakeout(const Array &params, bool fHelp)
{
    if (fHelp || params.size() != 3)
        throw std::runtime_error(
            "addstakeout <name> <address> <percentage>\n"
            "Creates a rule to send a portion of your stakes to another address.\n"
            "Usage: addstakeout <name> <address> <% of stake>\n");

    std::string name = params[0].get_str();

    if (name.length() > 100)
    {
        throw std::runtime_error("Please use a shorter name for this address");
    }

    std::string a = params[1].get_str();
    CBitcoinAddress address(a);
    if (!address.IsValid())
    {
        throw std::runtime_error("Please enter a valid Pinkcoin address.");
    }

    std::string sPercent = params[2].get_str();

    if (sPercent[0] == '-')
        throw std::runtime_error("Negative Percentages are not allowed");

    std::string sStack = "";

    // For Simplicity, we're going to assume any special characters are decimal points.
    for (unsigned int i = 0; i < sPercent.length(); i++) sStack += isdigit(sPercent[i]) ? sPercent[i] : '.';

    // We only allow one decimal point because we're converting to float.
    size_t dotCount = std::count(sStack.begin(), sStack.end(), '.');

    // Make sure there's only 1 decimal, if any, and that there is actually a number to convert as well.
    if (dotCount > 1 || sStack.length() == dotCount)
        throw std::runtime_error("Please only use real numbers with no special characters for Percentage\n");

    // Limit precision to 6 decimal places.
    size_t needle = sStack.find(".");
    if (needle != std::string::npos && sStack.substr(needle, sStack.length()).length() > 7)
    {
        sStack.replace(needle + 7, sStack.length(), "");
        //size_t sDiff = sStack.substr(needle, sStack.length()).length() - 7;
        //sStack = sStack.substr(0, sStack.length() - sDiff);
    }

    if (sStack[0] == '.')
        sStack = "0" + sStack;

    // Pass our checked stack to sPercent for conversion to double.
    sPercent = sStack;


    double nPercent = atof(sPercent.c_str());


    if (nPercent < 0 || nPercent > 100)
    {
        throw std::runtime_error("Please use a percentage between 0 and 100");
    }

    CStakeDB stakeDB(pstakeDB->strWalletFile);
    CTxDestination addr = address.Get();

    double percentAvailable = 100.000000;

    for (CWallet::mapAddress mapPercent : pstakeDB->mapAddressPercent)
    {
        CBitcoinAddress thisAddr(mapPercent.first);
        if (thisAddr.ToString() != address.ToString())
        {
            std::string percent = mapPercent.second;
            percentAvailable -= atof(percent.c_str());
        }
    }

    double updatingPercent = 0;
    //if (pstakeDB->mapAddressPercent.find(addr) != pstakeDB->mapAddressPercent.end())
    //    updatingPercent = atof(pstakeDB->mapAddressPercent[addr].c_str());


    if (nPercent > (percentAvailable + updatingPercent))
        throw std::runtime_error("Stake Percentage would increase total Stakeout over 100%\n"
                            "Please Reduce Stakeout Percentage or Delete another Stakeout to make room.\n"
                            "Current Available Stakeout Percentage: " + std::to_string(percentAvailable));

    if (!stakeDB.WriteStake(a, name, sPercent))
        throw std::runtime_error("Failed to save stake information, please debug.");

    pstakeDB->mapAddressBook[addr] = name;
    pstakeDB->mapAddressPercent[addr] = sPercent;

    std::string success = "\n Successfully written " + name + ", Pinkcoin Address:" + a + " side-stake " + sPercent + "% to stake.dat!";
    return success;

}

Value delstakeout(const Array &params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw std::runtime_error(
            "delstakeout <address>\n"
            "Deletes stakeout address from stake database.\n");

    std::string a = params[0].get_str();
    CBitcoinAddress address(a);

    if (!address.IsValid())
        throw std::runtime_error("Invalid address");

    if (pstakeDB->mapAddressBook.find(address.Get()) == pstakeDB->mapAddressBook.end())
        throw std::runtime_error("Address not in stake database");

    std::string xName = pstakeDB->mapAddressBook[address.Get()];
    std::string xPercent= pstakeDB->mapAddressPercent[address.Get()];

    CStakeDB stakeDB(pstakeDB->strWalletFile);

    if (!stakeDB.EraseStake(a))
        throw std::runtime_error("Failed to erase stake. Please debug.");

    pstakeDB->mapAddressBook.erase(address.Get());
    pstakeDB->mapAddressPercent.erase(address.Get());


    std::string success = "\n Successfully deleted " + xName + ", Pinkcoin Address:" + a + " side-stake " + xPercent + "% from stake.dat.";

    return success;
}

Value liststakeout(const Array &params, bool fHelp)
{
    if (fHelp || !params.empty())
        throw std::runtime_error(
            "liststakeout\n"
            "Returns the current Stakeout entries in stake database.\n");

    Array stakeOut;
    for (CWallet::mapAddress address : pstakeDB->mapAddressBook)
    {
            Object addressInfo;
            LOCK(pstakeDB->cs_wallet);
            {
                if(pstakeDB->mapAddressBook.find(CBitcoinAddress(address.first).Get()) != pstakeDB->mapAddressBook.end())
                {

                    std::string aName = pstakeDB->mapAddressBook[CBitcoinAddress(address.first).Get()];
                    std::string aPercent = pstakeDB->mapAddressPercent[CBitcoinAddress(address.first).Get()];

                    aPercent = aPercent + "%";

                    addressInfo.push_back(Pair("Name: ", aName));
                    addressInfo.push_back(Pair("Address: ", CBitcoinAddress(address.first).ToString()));
                    addressInfo.push_back(Pair("Percentage: ", aPercent));
                }
            }
            stakeOut.push_back(addressInfo);
    }

    return stakeOut;

}

Value getstakeoutinfo(const Array &params, bool fHelp)
{
    if (fHelp || !params.empty())
        throw std::runtime_error(
            "getstakeoutinfo\n"
            "Returns your aggregated Stakeout Data information\n");

    std::string success = "Currently under development.";
    return success;
}
