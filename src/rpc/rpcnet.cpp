// Copyright (c) 2009-2012 Bitcoin Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "net.h"
#include "bitcoinrpc.h"
#include "alert.h"
#include "wallet.h"
#include "db.h"
#include "walletdb.h"


json getconnectioncount(const json& params, bool fHelp)
{
    if (fHelp || !params.empty())
        throw std::runtime_error(
            "getconnectioncount\n"
            "Returns the number of connections to other nodes.");

    LOCK(cs_vNodes);
    return static_cast<int>(vNodes.size());
}

static void CopyNodeStats(std::vector<CNodeStats>& vstats)
{
    vstats.clear();

    LOCK(cs_vNodes);
    vstats.reserve(vNodes.size());
    for (CNode* pnode : vNodes) {
        CNodeStats stats;
        pnode->copyStats(stats);
        vstats.push_back(stats);
    }
}

json getpeerinfo(const json& params, bool fHelp)
{
    if (fHelp || !params.empty())
        throw std::runtime_error(
            "getpeerinfo\n"
            "Returns data about each connected network node.");

    std::vector<CNodeStats> vstats;
    CopyNodeStats(vstats);

    json ret = json::array();

    for (const CNodeStats& stats : vstats) {
        json obj;

        obj["addr"] = stats.addrName;
        obj["services"] = strprintf("%08" PRIx64, stats.nServices);
        obj["lastsend"] = static_cast<int64_t>(stats.nLastSend);
        obj["lastrecv"] = static_cast<int64_t>(stats.nLastRecv);
        obj["conntime"] = static_cast<int64_t>(stats.nTimeConnected);
        obj["version"] = stats.nVersion;
        obj["subver"] = stats.strSubVer;
        obj["inbound"] = stats.fInbound;
        obj["startingheight"] = stats.nStartingHeight;
        obj["banscore"] = stats.nMisbehavior;

        ret.push_back(obj);
    }

    return ret;
}

json getnodes(const json& params, bool fHelp)
{
    if (fHelp || !params.empty())
        throw std::runtime_error(
            "getnodes\n"
            "Returns each connected network node as addnodes in conf friendly format.");

    std::vector<CNodeStats> vstats;
    CopyNodeStats(vstats);
    std::string pNode = "";

    for (const CNodeStats& stats : vstats) {
        if (stats.addrName.rfind(":9134") != std::string::npos)
        {
            std::string ipAddress = stats.addrName.substr(0, stats.addrName.length() - 5);
            pNode += "addnode=" + ipAddress + "\n";
        }
    }

    return json(pNode);
}

// ppcoin: send alert.
// There is a known deadlock situation with ThreadMessageHandler
// ThreadMessageHandler: holds cs_vSend and acquiring cs_main in SendMessages()
// ThreadRPCServer: holds cs_main and acquiring cs_vSend in alert.RelayTo()/PushMessage()/BeginMessage()
json sendalert(const json& params, bool fHelp)
{
    if (fHelp || params.size() < 6)
        throw std::runtime_error(
            "sendalert <message> <privatekey> <minver> <maxver> <priority> <id> [cancelupto]\n"
            "<message> is the alert text message\n"
            "<privatekey> is hex string of alert master private key\n"
            "<minver> is the minimum applicable internal client version\n"
            "<maxver> is the maximum applicable internal client version\n"
            "<priority> is integer priority number\n"
            "<id> is the alert id\n"
            "[cancelupto] cancels all alert id's up to this number\n"
            "Returns true or false.");

    CAlert alert;
    CKey key;

    alert.strStatusBar = params[0].get<std::string>();
    alert.nMinVer = params[2].get<int>();
    alert.nMaxVer = params[3].get<int>();
    alert.nPriority = params[4].get<int>();
    alert.nID = params[5].get<int>();
    if (params.size() > 6)
        alert.nCancel = params[6].get<int>();
    alert.nVersion = PROTOCOL_VERSION;
    alert.nRelayUntil = GetAdjustedTime() + 365*24*60*60;
    alert.nExpiration = GetAdjustedTime() + 365*24*60*60;

    CDataStream sMsg(SER_NETWORK, PROTOCOL_VERSION);
    sMsg << (CUnsignedAlert)alert;
    alert.vchMsg = std::vector<unsigned char>(sMsg.begin(), sMsg.end());

    std::vector<unsigned char> vchPrivKey = ParseHex(params[1].get<std::string>());
    key.SetPrivKey(CPrivKey(vchPrivKey.begin(), vchPrivKey.end())); // if key is not correct openssl may crash
    if (!key.Sign(Hash(alert.vchMsg.begin(), alert.vchMsg.end()), alert.vchSig))
        throw std::runtime_error(
            "Unable to sign alert, check private key?\n");
    if(!alert.ProcessAlert())
        throw std::runtime_error(
            "Failed to process alert.\n");
    // Relay alert
    {
        LOCK(cs_vNodes);
        for (CNode* pnode : vNodes)
            alert.RelayTo(pnode);
    }

    json result;
    result["strStatusBar"] = alert.strStatusBar;
    result["nVersion"] = alert.nVersion;
    result["nMinVer"] = alert.nMinVer;
    result["nMaxVer"] = alert.nMaxVer;
    result["nPriority"] = alert.nPriority;
    result["nID"] = alert.nID;
    if (alert.nCancel > 0)
        result["nCancel"] = alert.nCancel;
    return result;
}
