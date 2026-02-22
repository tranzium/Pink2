// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2012 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// RPC wallet commands: balance queries, transaction listing, address info.

#include "rpcwallet_util.h"
#include "wallet.h"
#include "walletdb.h"
#include "bitcoinrpc.h"
#include "init.h"
#include "base58.h"


extern void TxToJSON(const CTransaction& tx, const uint256 hashBlock, json& entry);

json getinfo(const json& params, bool fHelp)
{
    if (fHelp || !params.empty())
        throw std::runtime_error(
            "getinfo\n"
            "Returns an object containing various state info.");

    proxyType proxy;
    GetProxy(NET_IPV4, proxy);

    json obj, diff;
    obj["version"] = FormatFullVersion();
    obj["protocolversion"] = static_cast<int>(PROTOCOL_VERSION);
    obj["walletversion"] = pwalletMain->GetVersion();
    obj["balance"] = ValueFromAmount(pwalletMain->GetBalance());
    obj["newmint"] = ValueFromAmount(pwalletMain->GetNewMint());
    obj["stake"] = ValueFromAmount(pwalletMain->GetStake());
    obj["blocks"] = static_cast<int>(nBestHeight);
    obj["timeoffset"] = static_cast<int64_t>(GetTimeOffset());
    obj["offsetfrom"] = fNTPSuccess ? std::string("NTP") : (GetBoolArg("-synctime", false) ? std::string("Peers") : std::string("Local"));
    obj["moneysupply"] = ValueFromAmount(pindexBest->nMoneySupply);
    obj["connections"] = static_cast<int>(vNodes.size());
    obj["proxy"] = (proxy.first.IsValid() ? proxy.first.ToStringIPPort() : std::string());
    obj["ip"] = addrSeenByPeer.ToStringIP();

    diff["proof-of-work"] = GetDifficulty();
    diff["proof-of-stake"] = GetDifficulty(GetLastBlockIndex2(GetLastBlockIndex(pindexBest, true), false));
    diff["proof-of-stake (flash)"] = GetDifficulty(GetLastBlockIndex2(pindexBest, true));
    obj["difficulty"] = diff;

    obj["testnet"] = fTestNet;
    obj["keypoololdest"] = static_cast<int64_t>(pwalletMain->GetOldestKeyPoolTime());
    obj["keypoolsize"] = static_cast<int>(pwalletMain->GetKeyPoolSize());
    obj["paytxfee"] = ValueFromAmount(nTransactionFee);
    obj["mininput"] = ValueFromAmount(nMinimumInputValue);
    if (pwalletMain->IsCrypted())
        obj["unlocked_until"] = static_cast<int64_t>(nWalletUnlockTime) / 1000;
    obj["errors"] = GetWarnings("statusbar");
    return obj;
}


CBitcoinAddress GetAccountAddress(std::string strAccount, bool bForceNew=false)
{
    CWalletDB walletdb(pwalletMain->strWalletFile);

    CAccount account;
    walletdb.ReadAccount(strAccount, account);

    bool bKeyUsed = false;

    // Check if the current key has been used
    if (account.vchPubKey.IsValid())
    {
        CScript scriptPubKey;
        scriptPubKey.SetDestination(account.vchPubKey.GetID());
        for (const auto& entry : pwalletMain->mapWallet)
        {
            if (!account.vchPubKey.IsValid())
                break;
            const CWalletTx& wtx = entry.second;
            for (const CTxOut& txout : wtx.vout)
                if (txout.scriptPubKey == scriptPubKey)
                    bKeyUsed = true;
        }
    }

    // Generate a new key
    if (!account.vchPubKey.IsValid() || bForceNew || bKeyUsed)
    {
        if (!pwalletMain->GetKeyFromPool(account.vchPubKey, false))
            throw JSONRPCError(RPC_WALLET_KEYPOOL_RAN_OUT, "Error: Keypool ran out, please call keypoolrefill first");

        pwalletMain->SetAddressBookName(account.vchPubKey.GetID(), strAccount);
        walletdb.WriteAccount(strAccount, account);
    }

    return CBitcoinAddress(account.vchPubKey.GetID());
}

json getaccountaddress(const json& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw std::runtime_error(
            "getaccountaddress <account>\n"
            "Returns the current Pinkcoin address for receiving payments to this account.");

    // Parse the account first so we don't generate a key if there's an error
    std::string strAccount = AccountFromValue(params[0]);

    json ret;

    ret = GetAccountAddress(strAccount).ToString();

    return ret;
}



json setaccount(const json& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 2)
        throw std::runtime_error(
            "setaccount <pinkcoinaddress> <account>\n"
            "Sets the account associated with the given address.");

    CBitcoinAddress address(params[0].get<std::string>());
    if (!address.IsValid())
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid Pinkcoin address");


    std::string strAccount;
    if (params.size() > 1)
        strAccount = AccountFromValue(params[1]);

    // Detect when changing the account of an address that is the 'unused current key' of another account:
    if (pwalletMain->mapAddressBook.count(address.Get()))
    {
        std::string strOldAccount = pwalletMain->mapAddressBook[address.Get()];
        if (address == GetAccountAddress(strOldAccount))
            GetAccountAddress(strOldAccount, true);
    }

    pwalletMain->SetAddressBookName(address.Get(), strAccount);

    return nullptr;
}


json getaccount(const json& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw std::runtime_error(
            "getaccount <pinkcoinaddress>\n"
            "Returns the account associated with the given address.");

    CBitcoinAddress address(params[0].get<std::string>());
    if (!address.IsValid())
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid Pinkcoin address");

    std::string strAccount;
    auto mi = pwalletMain->mapAddressBook.find(address.Get());
    if (mi != pwalletMain->mapAddressBook.end() && !mi->second.empty())
        strAccount = mi->second;
    return strAccount;
}


json getaddressesbyaccount(const json& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw std::runtime_error(
            "getaddressesbyaccount <account>\n"
            "Returns the list of addresses for the given account.");

    std::string strAccount = AccountFromValue(params[0]);

    // Find all addresses that have the given account
    json ret = json::array();
    for (const auto& item : pwalletMain->mapAddressBook)
    {
        const CBitcoinAddress& address = item.first;
        const std::string& strName = item.second;
        if (strName == strAccount)
            ret.push_back(address.ToString());
    }
    return ret;
}

json listaddressgroupings(const json& params, bool fHelp)
{
    if (fHelp)
        throw std::runtime_error(
            "listaddressgroupings\n"
            "Lists groups of addresses which have had their common ownership\n"
            "made public by common use as inputs or as the resulting change\n"
            "in past transactions");

    json jsonGroupings = json::array();
    std::map<CTxDestination, int64_t> balances = pwalletMain->GetAddressBalances();
    for (std::set<CTxDestination> grouping : pwalletMain->GetAddressGroupings())
    {
        json jsonGrouping = json::array();
        for (CTxDestination address : grouping)
        {
            json addressInfo = json::array();
            addressInfo.push_back(CBitcoinAddress(address).ToString());
            addressInfo.push_back(ValueFromAmount(balances[address]));
            {
                LOCK(pwalletMain->cs_wallet);
                if (pwalletMain->mapAddressBook.find(CBitcoinAddress(address).Get()) != pwalletMain->mapAddressBook.end())
                    addressInfo.push_back(pwalletMain->mapAddressBook.find(CBitcoinAddress(address).Get())->second);
            }
            jsonGrouping.push_back(addressInfo);
        }
        jsonGroupings.push_back(jsonGrouping);
    }
    return jsonGroupings;
}

json getreceivedbyaddress(const json& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 2)
        throw std::runtime_error(
            "getreceivedbyaddress <pinkcoinaddress> [minconf=1]\n"
            "Returns the total amount received by <pinkcoinaddress> in transactions with at least [minconf] confirmations.");

    // Bitcoin address
    CBitcoinAddress address = CBitcoinAddress(params[0].get<std::string>());
    CScript scriptPubKey;
    if (!address.IsValid())
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid Pinkcoin address");
    scriptPubKey.SetDestination(address.Get());
    if (!IsMine(*pwalletMain,scriptPubKey))
        return 0.0;

    // Minimum confirmations
    int nMinDepth = 1;
    if (params.size() > 1)
        nMinDepth = params[1].get<int>();

    // Tally
    int64_t nAmount = 0;
    for (const auto& entry : pwalletMain->mapWallet)
    {
        const CWalletTx& wtx = entry.second;
        if (wtx.IsCoinBase() || wtx.IsCoinStake() || !wtx.IsFinal())
            continue;

        for (const CTxOut& txout : wtx.vout)
            if (txout.scriptPubKey == scriptPubKey)
                if (wtx.GetDepthInMainChain() >= nMinDepth)
                    nAmount += txout.nValue;
    }

    return  ValueFromAmount(nAmount);
}


void GetAccountAddresses(std::string strAccount, std::set<CTxDestination>& setAddress)
{
    for (const auto& item : pwalletMain->mapAddressBook)
    {
        const CTxDestination& address = item.first;
        const std::string& strName = item.second;
        if (strName == strAccount)
            setAddress.insert(address);
    }
}

json getreceivedbyaccount(const json& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 2)
        throw std::runtime_error(
            "getreceivedbyaccount <account> [minconf=1]\n"
            "Returns the total amount received by addresses with <account> in transactions with at least [minconf] confirmations.");

    accountingDeprecationCheck();

    // Minimum confirmations
    int nMinDepth = 1;
    if (params.size() > 1)
        nMinDepth = params[1].get<int>();

    // Get the set of pub keys assigned to account
    std::string strAccount = AccountFromValue(params[0]);
    std::set<CTxDestination> setAddress;
    GetAccountAddresses(strAccount, setAddress);

    // Tally
    int64_t nAmount = 0;
    for (const auto& entry : pwalletMain->mapWallet)
    {
        const CWalletTx& wtx = entry.second;
        if (wtx.IsCoinBase() || wtx.IsCoinStake() || !wtx.IsFinal())
            continue;

        for (const CTxOut& txout : wtx.vout)
        {
            CTxDestination address;
            if (ExtractDestination(txout.scriptPubKey, address) && IsMine(*pwalletMain, address) && setAddress.count(address))
                if (wtx.GetDepthInMainChain() >= nMinDepth)
                    nAmount += txout.nValue;
        }
    }

    return static_cast<double>(nAmount) / static_cast<double>(COIN);
}


json getbalance(const json& params, bool fHelp)
{
    if (fHelp || params.size() > 2)
        throw std::runtime_error(
            "getbalance [account] [minconf=1]\n"
            "If [account] is not specified, returns the server's total available balance.\n"
            "If [account] is specified, returns the balance in the account.");

    if (params.empty())
        return  ValueFromAmount(pwalletMain->GetBalance());

    int nMinDepth = 1;
    if (params.size() > 1)
        nMinDepth = params[1].get<int>();

    if (params[0].get<std::string>() == "*") {
        // Calculate total balance a different way from GetBalance()
        // (GetBalance() sums up all unspent TxOuts)
        // getbalance and getbalance '*' 0 should return the same number.
        int64_t nBalance = 0;
        for (const auto& entry : pwalletMain->mapWallet)
        {
            const CWalletTx& wtx = entry.second;
            if (!wtx.IsTrusted())
                continue;

            int64_t allFee;
            std::string strSentAccount;
            std::list<std::pair<CTxDestination, int64_t> > listReceived;
            std::list<std::pair<CTxDestination, int64_t> > listSent;
            wtx.GetAmounts(listReceived, listSent, allFee, strSentAccount);
            if (wtx.GetDepthInMainChain() >= nMinDepth && wtx.GetBlocksToMaturity() == 0)
            {
                for (const auto& r : listReceived)
                    nBalance += r.second;
            }
            for (const auto& r : listSent)
                nBalance -= r.second;
            nBalance -= allFee;
        }
        return  ValueFromAmount(nBalance);
    }

    accountingDeprecationCheck();

    std::string strAccount = AccountFromValue(params[0]);

    int64_t nBalance = GetAccountBalance(strAccount, nMinDepth);

    return ValueFromAmount(nBalance);
}


struct tallyitem
{
    int64_t nAmount;
    int nConf;
    tallyitem()
    {
        nAmount = 0;
        nConf = std::numeric_limits<int>::max();
    }
};

json ListReceived(const json& params, bool fByAccounts)
{
    // Minimum confirmations
    int nMinDepth = 1;
    if (!params.empty())
        nMinDepth = params[0].get<int>();

    // Whether to include empty accounts
    bool fIncludeEmpty = false;
    if (params.size() > 1)
        fIncludeEmpty = params[1].get<bool>();

    // Tally
    std::map<CBitcoinAddress, tallyitem> mapTally;
    for (const auto& entry : pwalletMain->mapWallet)
    {
        const CWalletTx& wtx = entry.second;

        if (wtx.IsCoinBase() || wtx.IsCoinStake() || !wtx.IsFinal())
            continue;

        int nDepth = wtx.GetDepthInMainChain();
        if (nDepth < nMinDepth)
            continue;

        for (const CTxOut& txout : wtx.vout)
        {
            CTxDestination address;
            if (!ExtractDestination(txout.scriptPubKey, address) || !IsMine(*pwalletMain, address))
                continue;

            tallyitem& item = mapTally[address];
            item.nAmount += txout.nValue;
            item.nConf = std::min(item.nConf, nDepth);
        }
    }

    // Reply
    json ret = json::array();
    std::map<std::string, tallyitem> mapAccountTally;
    for (const auto& item : pwalletMain->mapAddressBook)
    {
        const CBitcoinAddress& address = item.first;
        const std::string& strAccount = item.second;
        auto it = mapTally.find(address);
        if (it == mapTally.end() && !fIncludeEmpty)
            continue;

        int64_t nAmount = 0;
        int nConf = std::numeric_limits<int>::max();
        if (it != mapTally.end())
        {
            nAmount = it->second.nAmount;
            nConf = it->second.nConf;
        }

        if (fByAccounts)
        {
            tallyitem& item = mapAccountTally[strAccount];
            item.nAmount += nAmount;
            item.nConf = std::min(item.nConf, nConf);
        }
        else
        {
            json obj;
            obj["address"] = address.ToString();
            obj["account"] = strAccount;
            obj["amount"] = ValueFromAmount(nAmount);
            obj["confirmations"] = (nConf == std::numeric_limits<int>::max() ? 0 : nConf);
            ret.push_back(obj);
        }
    }

    if (fByAccounts)
    {
        for (const auto& entry : mapAccountTally)
        {
            int64_t nAmount = entry.second.nAmount;
            int nConf = entry.second.nConf;
            json obj;
            obj["account"] = entry.first;
            obj["amount"] = ValueFromAmount(nAmount);
            obj["confirmations"] = (nConf == std::numeric_limits<int>::max() ? 0 : nConf);
            ret.push_back(obj);
        }
    }

    return ret;
}

json listreceivedbyaddress(const json& params, bool fHelp)
{
    if (fHelp || params.size() > 2)
        throw std::runtime_error(
            "listreceivedbyaddress [minconf=1] [includeempty=false]\n"
            "[minconf] is the minimum number of confirmations before payments are included.\n"
            "[includeempty] whether to include addresses that haven't received any payments.\n"
            "Returns an array of objects containing:\n"
            "  \"address\" : receiving address\n"
            "  \"account\" : the account of the receiving address\n"
            "  \"amount\" : total amount received by the address\n"
            "  \"confirmations\" : number of confirmations of the most recent transaction included");

    return ListReceived(params, false);
}

json listreceivedbyaccount(const json& params, bool fHelp)
{
    if (fHelp || params.size() > 2)
        throw std::runtime_error(
            "listreceivedbyaccount [minconf=1] [includeempty=false]\n"
            "[minconf] is the minimum number of confirmations before payments are included.\n"
            "[includeempty] whether to include accounts that haven't received any payments.\n"
            "Returns an array of objects containing:\n"
            "  \"account\" : the account of the receiving addresses\n"
            "  \"amount\" : total amount received by addresses with this account\n"
            "  \"confirmations\" : number of confirmations of the most recent transaction included");

    accountingDeprecationCheck();

    return ListReceived(params, true);
}

static void MaybePushAddress(json & entry, const CTxDestination &dest)
{
    CBitcoinAddress addr;
    if (addr.Set(dest))
        entry["address"] = addr.ToString();
}

void ListTransactions(const CWalletTx& wtx, const std::string& strAccount, int nMinDepth, bool fLong, json& ret)
{
    int64_t nFee;
    std::string strSentAccount;
    std::list<std::pair<CTxDestination, int64_t> > listReceived;
    std::list<std::pair<CTxDestination, int64_t> > listSent;

    wtx.GetAmounts(listReceived, listSent, nFee, strSentAccount);

    bool fAllAccounts = (strAccount == std::string("*"));

    // Sent
    if ((!wtx.IsCoinStake()) && (!listSent.empty() || nFee != 0) && (fAllAccounts || strAccount == strSentAccount))
    {
        for (const auto& s : listSent)
        {
            json entry;
            entry["account"] = strSentAccount;
            MaybePushAddress(entry, s.first);
            entry["category"] = "send";
            entry["amount"] = ValueFromAmount(-s.second);
            entry["fee"] = ValueFromAmount(-nFee);
            if (fLong)
                WalletTxToJSON(wtx, entry);
            ret.push_back(entry);
        }
    }

    // Received
    if (!listReceived.empty() && wtx.GetDepthInMainChain() >= nMinDepth)
    {
        bool stop = false;
        for (const auto& r : listReceived)
        {
            std::string account;
            if (pwalletMain->mapAddressBook.count(r.first))
                account = pwalletMain->mapAddressBook[r.first];
            if (fAllAccounts || (account == strAccount))
            {
                json entry;
                entry["account"] = account;
                MaybePushAddress(entry, r.first);
                if (wtx.IsCoinBase() || wtx.IsCoinStake())
                {
                    if (wtx.GetDepthInMainChain() < 1)
                        entry["category"] = "orphan";
                    else if (wtx.GetBlocksToMaturity() > 0)
                        entry["category"] = "immature";
                    else
                        entry["category"] = "generate";
                }
                else
                {
                    entry["category"] = "receive";
                }
                if (!wtx.IsCoinStake())
                    entry["amount"] = ValueFromAmount(r.second);
                else
                {
                    entry["amount"] = ValueFromAmount(-nFee);
                    stop = true; // only one coinstake output
                }
                if (fLong)
                    WalletTxToJSON(wtx, entry);
                ret.push_back(entry);
            }
            if (stop)
                break;
        }
    }
}

void AcentryToJSON(const CAccountingentry& acentry, const std::string& strAccount, json& ret)
{
    bool fAllAccounts = (strAccount == std::string("*"));

    if (fAllAccounts || acentry.strAccount == strAccount)
    {
        json entry;
        entry["account"] = acentry.strAccount;
        entry["category"] = "move";
        entry["time"] = static_cast<int64_t>(acentry.nTime);
        entry["amount"] = ValueFromAmount(acentry.nCreditDebit);
        entry["otheraccount"] = acentry.strOtherAccount;
        entry["comment"] = acentry.strComment;
        ret.push_back(entry);
    }
}

json listtransactions(const json& params, bool fHelp)
{
    if (fHelp || params.size() > 3)
        throw std::runtime_error(
            "listtransactions [account] [count=10] [from=0]\n"
            "Returns up to [count] most recent transactions skipping the first [from] transactions for account [account].");

    std::string strAccount = "*";
    if (!params.empty())
        strAccount = params[0].get<std::string>();
    int nCount = 10;
    if (params.size() > 1)
        nCount = params[1].get<int>();
    int nFrom = 0;
    if (params.size() > 2)
        nFrom = params[2].get<int>();

    if (nCount < 0)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Negative count");
    if (nFrom < 0)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Negative from");

    json ret = json::array();

    std::list<CAccountingentry> acentries;
    CWallet::TxItems txOrdered = pwalletMain->OrderedTxItems(acentries, strAccount);

    // iterate backwards until we have nCount items to return:
    for (CWallet::TxItems::reverse_iterator it = txOrdered.rbegin(); it != txOrdered.rend(); ++it)
    {
        CWalletTx *const pwtx = (*it).second.first;
        if (pwtx != 0)
            ListTransactions(*pwtx, strAccount, 0, true, ret);
        CAccountingentry *const pacentry = (*it).second.second;
        if (pacentry != 0)
            AcentryToJSON(*pacentry, strAccount, ret);

        if (static_cast<int>(ret.size()) >= (nCount+nFrom)) break;
    }
    // ret is newest to oldest

    if (nFrom > static_cast<int>(ret.size()))
        nFrom = ret.size();
    if ((nFrom + nCount) > static_cast<int>(ret.size()))
        nCount = ret.size() - nFrom;

    if (nFrom + nCount < static_cast<int>(ret.size()))
        ret.erase(ret.begin() + (nFrom + nCount), ret.end());
    if (nFrom > 0)
        ret.erase(ret.begin(), ret.begin() + nFrom);

    // Reverse - Return oldest to newest
    json reversed = json::array();
    for (auto it = ret.rbegin(); it != ret.rend(); ++it)
        reversed.push_back(*it);
    ret = reversed;

    return ret;
}

json listaccounts(const json& params, bool fHelp)
{
    if (fHelp || !params.empty())
        throw std::runtime_error(
            "listaccounts [minconf=1]\n"
            "Returns Object that has account names as keys, account balances as values.");

//    accountingDeprecationCheck();

//    int nMinDepth = 1;
//   if (!params.empty())
//        nMinDepth = params[0].get<int>();

    std::map<std::string, CBitcoinAddress> mapAccountAddresses;
    for (const auto& entry : pwalletMain->mapAddressBook) {
        if (IsMine(*pwalletMain, entry.first)) // This address belongs to me
            mapAccountAddresses[entry.second] = CBitcoinAddress(entry.first);
    }

    json ret;
    for (const auto& accountAddress : mapAccountAddresses) {
        ret[accountAddress.first] = accountAddress.second.ToString();
    }
    return ret;
}

json listsinceblock(const json& params, bool fHelp)
{
    if (fHelp)
        throw std::runtime_error(
            "listsinceblock [blockhash] [target-confirmations]\n"
            "Get all transactions in blocks since block [blockhash], or all transactions if omitted");

    CBlockIndex *pindex = nullptr;
    int target_confirms = 1;

    if (!params.empty())
    {
        uint256 blockId = 0;

        blockId.SetHex(params[0].get<std::string>());
        pindex = CBlockLocator(blockId).GetBlockIndex();
    }

    if (params.size() > 1)
    {
        target_confirms = params[1].get<int>();

        if (target_confirms < 1)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter");
    }

    int depth = pindex ? (1 + nBestHeight - pindex->nHeight) : -1;

    json transactions = json::array();

    for (const auto& entry : pwalletMain->mapWallet)
    {
        CWalletTx tx = entry.second;

        if (depth == -1 || tx.GetDepthInMainChain() < depth)
            ListTransactions(tx, "*", 0, true, transactions);
    }

    uint256 lastblock;

    if (target_confirms == 1)
    {
        lastblock = hashBestChain;
    }
    else
    {
        int target_height = pindexBest->nHeight + 1 - target_confirms;

        CBlockIndex *block;
        for (block = pindexBest;
             block && block->nHeight > target_height;
             block = block->pprev)  { }

        lastblock = block ? block->GetBlockHash() : 0;
    }

    json ret;
    ret["transactions"] = transactions;
    ret["lastblock"] = lastblock.GetHex();

    return ret;
}

json gettransaction(const json& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw std::runtime_error(
            "gettransaction <txid>\n"
            "Get detailed information about <txid>");

    uint256 hash;
    hash.SetHex(params[0].get<std::string>());

    json entry;

    if (pwalletMain->mapWallet.count(hash))
    {
        const CWalletTx& wtx = pwalletMain->mapWallet[hash];

        TxToJSON(wtx, 0, entry);

        int64_t nCredit = wtx.GetCredit();
        int64_t nDebit = wtx.GetDebit();
        int64_t nNet = nCredit - nDebit;
        int64_t nFee = (wtx.IsFromMe() ? wtx.GetValueOut() - nDebit : 0);

        entry["amount"] = ValueFromAmount(nNet - nFee);
        if (wtx.IsFromMe())
            entry["fee"] = ValueFromAmount(nFee);

        WalletTxToJSON(wtx, entry);

        json details = json::array();
        ListTransactions(pwalletMain->mapWallet[hash], "*", 0, false, details);
        entry["details"] = details;
    }
    else
    {
        CTransaction tx;
        uint256 hashBlock = 0;
        if (GetTransaction(hash, tx, hashBlock))
        {
            TxToJSON(tx, 0, entry);
            if (hashBlock == 0)
                entry["confirmations"] = 0;
            else
            {
                entry["blockhash"] = hashBlock.GetHex();
                auto mi = mapBlockIndex.find(hashBlock);
                if (mi != mapBlockIndex.end() && mi->second)
                {
                    CBlockIndex* pindex = mi->second;
                    if (pindex->IsInMainChain())
                        entry["confirmations"] = 1 + nBestHeight - pindex->nHeight;
                    else
                        entry["confirmations"] = 0;
                }
            }
        }
        else
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "No information available about transaction");
    }

    return entry;
}

json getwalletinfo(const json& params, bool fHelp)
{
    if (fHelp || !params.empty())
        throw std::runtime_error(
            "getwalletinfo\n"
            "Returns an object containing various wallet state info.\n"
            "\nResult:\n"
            "{\n"
            " \"walletversion\": xxxxx, (numeric) the wallet version\n"
            " \"balance\": xxxxxxx, (numeric) the total pinkcoin balance of the wallet\n"
            " \"txcount\": xxxxxxx, (numeric) the total number of transactions in the wallet\n"
            " \"keypoololdest\": xxxxxx, (numeric) the timestamp (seconds since GMT epoch) of the oldest pre-generated key in the key pool\n"
            " \"keypoolsize\": xxxx, (numeric) how many new keys are pre-generated\n"
            " \"unlocked_until\": ttt, (numeric) the timestamp in seconds since epoch (midnight Jan 1 1970 GMT) that the wallet is unlocked for transfers, or 0 if the wallet is locked\n"
            "}\n"
        );

    json obj;
    obj["walletversion"] = pwalletMain->GetVersion();
    obj["balance"] = ValueFromAmount(pwalletMain->GetBalance());
    obj["txcount"] = static_cast<int>(pwalletMain->mapWallet.size());
    obj["keypoololdest"] = static_cast<int64_t>(pwalletMain->GetOldestKeyPoolTime());
    obj["keypoolsize"] = static_cast<int>(pwalletMain->GetKeyPoolSize());
    if (pwalletMain->IsCrypted())
        obj["unlocked_until"] = static_cast<int64_t>(nWalletUnlockTime);
    return obj;
}
