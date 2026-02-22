// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2012 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "base58.h"
#include "bitcoinrpc.h"
#include "txdb.h"
#include "init.h"
#include "main.h"
#include "net.h"
#include "wallet.h"

using namespace std;

void ScriptPubKeyToJSON(const CScript& scriptPubKey, json& out, bool fIncludeHex)
{
    txnouttype type;
    vector<CTxDestination> addresses;
    int nRequired;

    out["asm"] = scriptPubKey.ToString();

    if (fIncludeHex)
        out["hex"] = HexStr(scriptPubKey.begin(), scriptPubKey.end());

    if (!ExtractDestinations(scriptPubKey, type, addresses, nRequired))
    {
        out["type"] = GetTxnOutputType(type);
        return;
    }

    out["reqSigs"] = nRequired;
    out["type"] = GetTxnOutputType(type);

    json a = json::array();
    for (const CTxDestination& addr : addresses)
        a.push_back(CBitcoinAddress(addr).ToString());
    out["addresses"] = a;
}

void TxToJSON(const CTransaction& tx, const uint256 hashBlock, json& entry)
{
    entry["txid"] = tx.GetHash().GetHex();
    entry["version"] = tx.nVersion;
    entry["time"] = static_cast<int64_t>(tx.nTime);
    entry["locktime"] = static_cast<int64_t>(tx.nLockTime);
    json vin = json::array();
    for (const CTxIn& txin : tx.vin)
    {
        json in;
        if (tx.IsCoinBase())
            in["coinbase"] = HexStr(txin.scriptSig.begin(), txin.scriptSig.end());
        else
        {
            in["txid"] = txin.prevout.hash.GetHex();
            in["vout"] = static_cast<int64_t>(txin.prevout.n);
            json o;
            o["asm"] = txin.scriptSig.ToString();
            o["hex"] = HexStr(txin.scriptSig.begin(), txin.scriptSig.end());
            in["scriptSig"] = o;
        }
        in["sequence"] = static_cast<int64_t>(txin.nSequence);
        vin.push_back(in);
    }
    entry["vin"] = vin;
    json vout = json::array();
    for (unsigned int i = 0; i < tx.vout.size(); i++)
    {
        const CTxOut& txout = tx.vout[i];
        json out;
        out["value"] = ValueFromAmount(txout.nValue);
        out["n"] = static_cast<int64_t>(i);
        json o;
        ScriptPubKeyToJSON(txout.scriptPubKey, o, false);
        out["scriptPubKey"] = o;
        vout.push_back(out);
    }
    entry["vout"] = vout;

    if (hashBlock != 0)
    {
        entry["blockhash"] = hashBlock.GetHex();
        auto mi = mapBlockIndex.find(hashBlock);
        if (mi != mapBlockIndex.end() && mi->second)
        {
            CBlockIndex* pindex = mi->second;
            if (pindex->IsInMainChain())
            {
                entry["confirmations"] = 1 + nBestHeight - pindex->nHeight;
                entry["time"] = static_cast<int64_t>(pindex->nTime);
                entry["blocktime"] = static_cast<int64_t>(pindex->nTime);
            }
            else
                entry["confirmations"] = 0;
        }
    }
}

json getrawtransaction(const json& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 2)
        throw runtime_error(
            "getrawtransaction <txid> [verbose=0]\n"
            "If verbose=0, returns a string that is\n"
            "serialized, hex-encoded data for <txid>.\n"
            "If verbose is non-zero, returns an Object\n"
            "with information about <txid>.");

    uint256 hash;
    hash.SetHex(params[0].get<std::string>());

    bool fVerbose = false;
    if (params.size() > 1)
        fVerbose = (params[1].get<int>() != 0);

    CTransaction tx;
    uint256 hashBlock = 0;
    if (!GetTransaction(hash, tx, hashBlock))
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "No information available about transaction");

    CDataStream ssTx(SER_NETWORK, PROTOCOL_VERSION);
    ssTx << tx;
    string strHex = HexStr(ssTx.begin(), ssTx.end());

    if (!fVerbose)
        return strHex;

    json result;
    result["hex"] = strHex;
    TxToJSON(tx, hashBlock, result);
    return result;
}

json listunspent(const json& params, bool fHelp)
{
    if (fHelp || params.size() > 3)
        throw runtime_error(
            "listunspent [minconf=1] [maxconf=9999999]  [\"address\",...]\n"
            "Returns array of unspent transaction outputs\n"
            "with between minconf and maxconf (inclusive) confirmations.\n"
            "Optionally filtered to only include txouts paid to specified addresses.\n"
            "Results are an array of Objects, each of which has:\n"
            "{txid, vout, scriptPubKey, amount, confirmations}");

    RPCTypeCheck(params, {json::value_t::number_integer, json::value_t::number_integer, json::value_t::array});

    int nMinDepth = 1;
    if (!params.empty())
        nMinDepth = params[0].get<int>();

    int nMaxDepth = 9999999;
    if (params.size() > 1)
        nMaxDepth = params[1].get<int>();

    set<CBitcoinAddress> setAddress;
    if (params.size() > 2)
    {
        const json& inputs = params[2];
        for (const json& input : inputs)
        {
            CBitcoinAddress address(input.get<std::string>());
            if (!address.IsValid())
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, string("Invalid Pinkcoin address: ")+input.get<std::string>());
            if (setAddress.count(address))
                throw JSONRPCError(RPC_INVALID_PARAMETER, string("Invalid parameter, duplicated address: ")+input.get<std::string>());
           setAddress.insert(address);
        }
    }

    json results = json::array();
    vector<COutput> vecOutputs;
    pwalletMain->AvailableCoins(vecOutputs, false);
    for (const COutput& out : vecOutputs)
    {
        if (out.nDepth < nMinDepth || out.nDepth > nMaxDepth)
            continue;

        if(setAddress.size())
        {
            CTxDestination address;
            if(!ExtractDestination(out.tx->vout[out.i].scriptPubKey, address))
                continue;

            if (!setAddress.count(address))
                continue;
        }

        int64_t nValue = out.tx->vout[out.i].nValue;
        const CScript& pk = out.tx->vout[out.i].scriptPubKey;
        json entry;
        entry["txid"] = out.tx->GetHash().GetHex();
        entry["vout"] = out.i;
        CTxDestination address;
        if (ExtractDestination(out.tx->vout[out.i].scriptPubKey, address))
        {
            entry["address"] = CBitcoinAddress(address).ToString();
            if (pwalletMain->mapAddressBook.count(address))
                entry["account"] = pwalletMain->mapAddressBook[address];
        }
        entry["scriptPubKey"] = HexStr(pk.begin(), pk.end());
        if (pk.IsPayToScriptHash())
        {
            CTxDestination address;
            if (ExtractDestination(pk, address))
            {
                const CScriptID& hash = std::get<CScriptID>(address);
                CScript redeemScript;
                if (pwalletMain->GetCScript(hash, redeemScript))
                    entry["redeemScript"] = HexStr(redeemScript.begin(), redeemScript.end());
            }
        }
        entry["amount"] = ValueFromAmount(nValue);
        entry["confirmations"] = out.nDepth;
        results.push_back(entry);
    }

    return results;
}

json createrawtransaction(const json& params, bool fHelp)
{
    if (fHelp || params.size() != 2)
        throw runtime_error(
            "createrawtransaction [{\"txid\":txid,\"vout\":n},...] {address:amount,...}\n"
            "Create a transaction spending given inputs\n"
            "(array of objects containing transaction id and output number),\n"
            "sending to given address(es).\n"
            "Returns hex-encoded raw transaction.\n"
            "Note that the transaction's inputs are not signed, and\n"
            "it is not stored in the wallet or transmitted to the network.");

    RPCTypeCheck(params, std::list<json::value_t>{json::value_t::array, json::value_t::object});

    const json& inputs = params[0];
    const json& sendTo = params[1];

    CTransaction rawTx;

    for (const json& input : inputs)
    {
        const json& o = input;

        json txid_v = o.contains("txid") ? o["txid"] : json(nullptr);
        if (!txid_v.is_string())
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, missing txid key");
        string txid = txid_v.get<std::string>();
        if (!IsHex(txid))
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, expected hex txid");

        json vout_v = o.contains("vout") ? o["vout"] : json(nullptr);
        if (!vout_v.is_number_integer())
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, missing vout key");
        int nOutput = vout_v.get<int>();
        if (nOutput < 0)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, vout must be positive");

        CTxIn in(COutPoint(uint256(txid), nOutput));
        rawTx.vin.push_back(in);
    }

    set<CBitcoinAddress> setAddress;
    for (const auto& [key, value] : sendTo.items())
    {
        CBitcoinAddress address(key);
        if (!address.IsValid())
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, string("Invalid Pinkcoin address: ")+key);

        if (setAddress.count(address))
            throw JSONRPCError(RPC_INVALID_PARAMETER, string("Invalid parameter, duplicated address: ")+key);
        setAddress.insert(address);

        CScript scriptPubKey;
        scriptPubKey.SetDestination(address.Get());
        int64_t nAmount = AmountFromValue(value);

        CTxOut out(nAmount, scriptPubKey);
        rawTx.vout.push_back(out);
    }

    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << rawTx;
    return HexStr(ss.begin(), ss.end());
}

json decoderawtransaction(const json& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
            "decoderawtransaction <hex string>\n"
            "Return a JSON object representing the serialized, hex-encoded transaction.");

    RPCTypeCheck(params, {json::value_t::string});

    vector<unsigned char> txData(ParseHex(params[0].get<std::string>()));
    CDataStream ssData(txData, SER_NETWORK, PROTOCOL_VERSION);
    CTransaction tx;
    try {
        ssData >> tx;
    }
    catch (std::exception &e) {
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "TX decode failed");
    }

    json result;
    TxToJSON(tx, 0, result);

    return result;
}

json decodescript(const json& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
            "decodescript <hex string>\n"
            "Decode a hex-encoded script.");

    RPCTypeCheck(params, {json::value_t::string});

    json r;
    CScript script;
    if (!params[0].get<std::string>().empty()){
        vector<unsigned char> scriptData(ParseHexV(params[0], "argument"));
        script = CScript(scriptData.begin(), scriptData.end());
    } else {
        // Empty scripts are valid
    }
    ScriptPubKeyToJSON(script, r, false);

    r["p2sh"] = CBitcoinAddress(script.GetID()).ToString();
    return r;
}

json signrawtransaction(const json& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 4)
        throw runtime_error(
            "signrawtransaction <hex string> [{\"txid\":txid,\"vout\":n,\"scriptPubKey\":hex,\"redeemScript\":hex},...] [<privatekey1>,...] [sighashtype=\"ALL\"]\n"
            "Sign inputs for raw transaction (serialized, hex-encoded).\n"
            "Second optional argument (may be null) is an array of previous transaction outputs that\n"
            "this transaction depends on but may not yet be in the blockchain.\n"
            "Third optional argument (may be null) is an array of base58-encoded private\n"
            "keys that, if given, will be the only keys used to sign the transaction.\n"
            "Fourth optional argument is a string that is one of six values; ALL, NONE, SINGLE or\n"
            "ALL|ANYONECANPAY, NONE|ANYONECANPAY, SINGLE|ANYONECANPAY.\n"
            "Returns json object with keys:\n"
            "  hex : raw transaction with signature(s) (hex-encoded string)\n"
            "  complete : 1 if transaction has a complete set of signature (0 if not)"
            + HelpRequiringPassphrase());

    RPCTypeCheck(params, {json::value_t::string, json::value_t::array, json::value_t::array, json::value_t::string}, true);

    vector<unsigned char> txData(ParseHex(params[0].get<std::string>()));
    CDataStream ssData(txData, SER_NETWORK, PROTOCOL_VERSION);
    vector<CTransaction> txVariants;
    while (!ssData.empty())
    {
        try {
            CTransaction tx;
            ssData >> tx;
            txVariants.push_back(tx);
        }
        catch (std::exception &e) {
            throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "TX decode failed");
        }
    }

    if (txVariants.empty())
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Missing transaction");

    // mergedTx will end up with all the signatures; it
    // starts as a clone of the rawtx:
    CTransaction mergedTx(txVariants[0]);
    bool fComplete = true;

    // Fetch previous transactions (inputs):
    map<COutPoint, CScript> mapPrevOut;
    for (unsigned int i = 0; i < mergedTx.vin.size(); i++)
    {
        CTransaction tempTx;
        MapPrevTx mapPrevTx;
        CTxDB txdb("r");
        map<uint256, CTxIndex> unused;
        bool fInvalid;

        // FetchInputs aborts on failure, so we go one at a time.
        tempTx.vin.push_back(mergedTx.vin[i]);
        tempTx.FetchInputs(txdb, unused, false, false, mapPrevTx, fInvalid);

        // Copy results into mapPrevOut:
        for (const CTxIn& txin : tempTx.vin)
        {
            const uint256& prevHash = txin.prevout.hash;
            if (mapPrevTx.count(prevHash) && mapPrevTx[prevHash].second.vout.size()>txin.prevout.n)
                mapPrevOut[txin.prevout] = mapPrevTx[prevHash].second.vout[txin.prevout.n].scriptPubKey;
        }
    }

    bool fGivenKeys = false;
    CBasicKeyStore tempKeystore;
    if (params.size() > 2 && !params[2].is_null())
    {
        fGivenKeys = true;
        const json& keys = params[2];
        for (const json& k : keys)
        {
            CBitcoinSecret vchSecret;
            bool fGood = vchSecret.SetString(k.get<std::string>());
            if (!fGood)
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid private key");
            CKey key;
            bool fCompressed;
            CSecret secret = vchSecret.GetSecret(fCompressed);
            key.SetSecret(secret, fCompressed);
            tempKeystore.AddKey(key);
        }
    }
    else
        EnsureWalletIsUnlocked();

    // Add previous txouts given in the RPC call:
    if (params.size() > 1 && !params[1].is_null())
    {
        const json& prevTxs = params[1];
        for (const json& p : prevTxs)
        {
            if (!p.is_object())
                throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "expected object with {\"txid'\",\"vout\",\"scriptPubKey\"}");

            const json& prevOut = p;

            RPCTypeCheck(prevOut, {{"txid", json::value_t::string}, {"vout", json::value_t::number_integer}, {"scriptPubKey", json::value_t::string}});

            string txidHex = prevOut["txid"].get<std::string>();
            if (!IsHex(txidHex))
                throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "txid must be hexadecimal");
            uint256 txid;
            txid.SetHex(txidHex);

            int nOut = prevOut["vout"].get<int>();
            if (nOut < 0)
                throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "vout must be positive");

            string pkHex = prevOut["scriptPubKey"].get<std::string>();
            if (!IsHex(pkHex))
                throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "scriptPubKey must be hexadecimal");
            vector<unsigned char> pkData(ParseHex(pkHex));
            CScript scriptPubKey(pkData.begin(), pkData.end());

            COutPoint outpoint(txid, nOut);
            if (mapPrevOut.count(outpoint))
            {
                // Complain if scriptPubKey doesn't match
                if (mapPrevOut[outpoint] != scriptPubKey)
                {
                    string err("Previous output scriptPubKey mismatch:\n");
                    err = err + mapPrevOut[outpoint].ToString() + "\nvs:\n"+
                        scriptPubKey.ToString();
                    throw JSONRPCError(RPC_DESERIALIZATION_ERROR, err);
                }
            }
            else
                mapPrevOut[outpoint] = scriptPubKey;

            // if redeemScript given and not using the local wallet (private keys
            // given), add redeemScript to the tempKeystore so it can be signed:
            if (fGivenKeys && scriptPubKey.IsPayToScriptHash())
            {
                RPCTypeCheck(prevOut, {{"txid", json::value_t::string}, {"vout", json::value_t::number_integer}, {"scriptPubKey", json::value_t::string}, {"redeemScript", json::value_t::string}});
                json v = prevOut.contains("redeemScript") ? prevOut["redeemScript"] : json(nullptr);
                if (!v.is_null())
                {
                    vector<unsigned char> rsData(ParseHexV(v, "redeemScript"));
                    CScript redeemScript(rsData.begin(), rsData.end());
                    tempKeystore.AddCScript(redeemScript);
                }
            }
        }
    }

    const CKeyStore& keystore = (fGivenKeys ? tempKeystore : *pwalletMain);

    int nHashType = SIGHASH_ALL;
    if (params.size() > 3 && !params[3].is_null())
    {
        static map<string, int> mapSigHashValues = {
            {"ALL", SIGHASH_ALL},
            {"ALL|ANYONECANPAY", SIGHASH_ALL|SIGHASH_ANYONECANPAY},
            {"NONE", SIGHASH_NONE},
            {"NONE|ANYONECANPAY", SIGHASH_NONE|SIGHASH_ANYONECANPAY},
            {"SINGLE", SIGHASH_SINGLE},
            {"SINGLE|ANYONECANPAY", SIGHASH_SINGLE|SIGHASH_ANYONECANPAY}
        };
        string strHashType = params[3].get<std::string>();
        if (mapSigHashValues.count(strHashType))
            nHashType = mapSigHashValues[strHashType];
        else
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid sighash param");
    }

    bool fHashSingle = ((nHashType & ~SIGHASH_ANYONECANPAY) == SIGHASH_SINGLE);

    // Sign what we can:
    for (unsigned int i = 0; i < mergedTx.vin.size(); i++)
    {
        CTxIn& txin = mergedTx.vin[i];
        if (mapPrevOut.count(txin.prevout) == 0)
        {
            fComplete = false;
            continue;
        }
        const CScript& prevPubKey = mapPrevOut[txin.prevout];

        txin.scriptSig.clear();
        // Only sign SIGHASH_SINGLE if there's a corresponding output:
        if (!fHashSingle || (i < mergedTx.vout.size()))
            SignSignature(keystore, prevPubKey, mergedTx, i, nHashType);

        // ... and merge in other signatures:
        for (const CTransaction& txv : txVariants)
        {
            txin.scriptSig = CombineSignatures(prevPubKey, mergedTx, i, txin.scriptSig, txv.vin[i].scriptSig);
        }
        if (!VerifyScript(txin.scriptSig, prevPubKey, mergedTx, i, 0))
            fComplete = false;
    }

    json result;
    CDataStream ssTx(SER_NETWORK, PROTOCOL_VERSION);
    ssTx << mergedTx;
    result["hex"] = HexStr(ssTx.begin(), ssTx.end());
    result["complete"] = fComplete;

    return result;
}

json sendrawtransaction(const json& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 1)
        throw runtime_error(
            "sendrawtransaction <hex string>\n"
            "Submits raw transaction (serialized, hex-encoded) to local node and network.");

    RPCTypeCheck(params, {json::value_t::string});

    // parse hex string from parameter
    vector<unsigned char> txData(ParseHex(params[0].get<std::string>()));
    CDataStream ssData(txData, SER_NETWORK, PROTOCOL_VERSION);
    CTransaction tx;

    // deserialize binary data stream
    try {
        ssData >> tx;
    }
    catch (std::exception &e) {
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "TX decode failed");
    }
    uint256 hashTx = tx.GetHash();

    // See if the transaction is already in a block
    // or in the memory pool:
    CTransaction existingTx;
    uint256 hashBlock = 0;
    if (GetTransaction(hashTx, existingTx, hashBlock))
    {
        if (hashBlock != 0)
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, string("transaction already in block ")+hashBlock.GetHex());
        // Not in block, but already in the memory pool; will drop
        // through to re-relay it.
    }
    else
    {
        // push to local node
        CTxDB txdb("r");
        if (!tx.AcceptToMemoryPool(txdb))
            throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "TX rejected");

        SyncWithWallets(tx, nullptr, true);
    }
    RelayTransaction(tx, hashTx);

    return hashTx.GetHex();
}
