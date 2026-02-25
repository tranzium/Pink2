// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2012 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "init.h"
#include "util.h"
#include "sync.h"
#include "thread_guard.h"
#include "ui_interface.h"
#include "base58.h"
#include "bitcoinrpc.h"
#include "db.h"

#undef printf              // undo the global #define printf OutputDebugStringF
#include <asio.hpp>
#include <asio/ssl.hpp>
#include <filesystem>
#include "asio_stream.h"
#include "string_utils.h"
#include <memory>
#include <list>
#include <sstream>

// Standalone Asio compatibility macros
#define GetIOService(s)        ((asio::io_context&)(s).get_executor().context())
#define GetIOServiceFromPtr(s) ((asio::io_context&)(s->get_executor().context()))
using ioContext = asio::io_context;

#include "logging.h"


static std::string strRPCUserColonPass;

class AcceptedConnection;
void ThreadRPCServer3(AcceptedConnection* conn);

static inline unsigned short GetDefaultRPCPort()
{
    return GetBoolArg("-testnet", false) ? 19135 : 9135;
}

json JSONRPCError(int code, const std::string& message)
{
    json error;
    error["code"] = code;
    error["message"] = message;
    return error;
}

// Helper to get a type name string for error messages
static const char* JsonTypeName(json::value_t t)
{
    switch (t) {
        case json::value_t::null:             return "null";
        case json::value_t::object:           return "object";
        case json::value_t::array:            return "array";
        case json::value_t::string:           return "string";
        case json::value_t::boolean:          return "boolean";
        case json::value_t::number_integer:   return "integer";
        case json::value_t::number_unsigned:  return "integer";
        case json::value_t::number_float:     return "float";
        default:                              return "unknown";
    }
}

// Check if a json value matches the expected type.
// Treats number_integer and number_unsigned as equivalent (both are "integer").
static bool JsonTypeMatches(json::value_t actual, json::value_t expected)
{
    if (actual == expected) return true;
    // Accept both signed and unsigned integers when integer is expected
    if (expected == json::value_t::number_integer && actual == json::value_t::number_unsigned) return true;
    if (expected == json::value_t::number_unsigned && actual == json::value_t::number_integer) return true;
    // Accept integer types when float is expected (implicit numeric promotion)
    if (expected == json::value_t::number_float &&
        (actual == json::value_t::number_integer || actual == json::value_t::number_unsigned)) return true;
    return false;
}

void RPCTypeCheck(const json& params,
                  const std::list<json::value_t>& typesExpected,
                  bool fAllowNull)
{
    unsigned int i = 0;
    for (json::value_t t : typesExpected)
    {
        if (params.size() <= i)
            break;

        const json& v = params[i];
        if (!(JsonTypeMatches(v.type(), t) || (fAllowNull && v.is_null())))
        {
            std::string err = strprintf("Expected type %s, got %s",
                                   JsonTypeName(t), JsonTypeName(v.type()));
            throw JSONRPCError(RPC_TYPE_ERROR, err);
        }
        i++;
    }
}

void RPCTypeCheck(const json& o,
                  const std::map<std::string, json::value_t>& typesExpected,
                  bool fAllowNull)
{
    for (const auto& t : typesExpected)
    {
        if (!o.contains(t.first)) {
            if (!fAllowNull)
                throw JSONRPCError(RPC_TYPE_ERROR, strprintf("Missing %s", t.first.c_str()));
            continue;
        }
        const json& v = o[t.first];
        if (!(JsonTypeMatches(v.type(), t.second) || (fAllowNull && v.is_null())))
        {
            std::string err = strprintf("Expected type %s for %s, got %s",
                                   JsonTypeName(t.second), t.first.c_str(), JsonTypeName(v.type()));
            throw JSONRPCError(RPC_TYPE_ERROR, err);
        }
    }
}

int64_t AmountFromValue(const json& value)
{
    double dAmount = value.get<double>();
    if (dAmount <= 0.0 || dAmount > MAX_MONEY)
        throw JSONRPCError(RPC_TYPE_ERROR, "Invalid amount");
    int64_t nAmount = roundint64(dAmount * COIN);
    if (!MoneyRange(nAmount))
        throw JSONRPCError(RPC_TYPE_ERROR, "Invalid amount");
    return nAmount;
}

json ValueFromAmount(int64_t amount)
{
    return static_cast<double>(amount) / static_cast<double>(COIN);
}

std::string HexBits(unsigned int nBits)
{
    union {
        int32_t nBits;
        char cBits[4];
    } uBits;
    uBits.nBits = htonl(static_cast<int32_t>(nBits));
    return HexStr(CharCast(uBits.cBits), CharEnd(uBits.cBits));
}


//
// Utilities: convert hex-encoded Values
// (throws error if not hex).
//
uint256 ParseHashV(const json& v, std::string strName)
{
    std::string strHex;
    if (v.is_string())
        strHex = v.get<std::string>();
    if (!IsHex(strHex)) // Note: IsHex("") is false
        throw JSONRPCError(RPC_INVALID_PARAMETER, strName+" must be hexadecimal string (not '"+strHex+"')");
    uint256 result;
    result.SetHex(strHex);
    return result;
}

uint256 ParseHashO(const json& o, std::string strKey)
{
    return ParseHashV(o[strKey], strKey);
}

std::vector<unsigned char> ParseHexV(const json& v, std::string strName)
{
    std::string strHex;
    if (v.is_string())
        strHex = v.get<std::string>();
    if (!IsHex(strHex))
        throw JSONRPCError(RPC_INVALID_PARAMETER, strName+" must be hexadecimal string (not '"+strHex+"')");
    return ParseHex(strHex);
}

std::vector<unsigned char> ParseHexO(const json& o, std::string strKey)
{
    return ParseHexV(o[strKey], strKey);
}


///
/// Note: This interface may still be subject to change.
///

std::string CRPCTable::help(std::string strCommand) const
{
    std::string strRet;
    std::set<rpcfn_type> setDone;
    for (const auto& mi : mapCommands)
    {
        const CRPCCommand *pcmd = mi.second;
        std::string strMethod = mi.first;
        // We already filter duplicates, but these deprecated screw up the sort order
        if (strMethod.find("label") != std::string::npos)
            continue;
        if (strCommand != "" && strMethod != strCommand)
            continue;
        try
        {
            json params = json::array();
            rpcfn_type pfn = pcmd->actor;
            if (setDone.insert(pfn).second)
                (*pfn)(params, true);
        }
        catch (std::exception& e)
        {
            // Help text is returned in an exception
            std::string strHelp = std::string(e.what());
            if (strCommand == "")
                if (strHelp.find('\n') != std::string::npos)
                    strHelp = strHelp.substr(0, strHelp.find('\n'));
            strRet += strHelp + "\n";
        }
    }
    if (strRet == "")
        strRet = strprintf("help: unknown command: %s\n", strCommand.c_str());
    strRet = strRet.substr(0,strRet.size()-1);
    return strRet;
}

json help(const json& params, bool fHelp)
{
    if (fHelp || params.size() > 1)
        throw std::runtime_error(
            "help [command]\n"
            "List commands, or get help for a command.");

    std::string strCommand;
    if (!params.empty())
        strCommand = params[0].get<std::string>();

    return tableRPC.help(strCommand);
}


json stop(const json& params, bool fHelp)
{
    if (fHelp || params.size() > 1)
        throw std::runtime_error(
            "stop <detach>\n"
            "<detach> is true or false to detach the database or not for this stop only\n"
            "Stop Pinkcoin server (and possibly override the detachdb config value).");
    // Shutdown will take long enough that the response should get back
    if (!params.empty())
        bitdb.SetDetach(params[0].get<bool>());
    StartShutdown();
    return "Pinkcoin server stopping";
}

json setloglevel(const json& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 2)
        throw std::runtime_error(
            "setloglevel <level> [categories]\n"
            "Set the logging level and optionally enable categories.\n"
            "<level> is one of: none, error, warn, info, debug\n"
            "[categories] is comma-separated list of: net, wallet, stake, rpc, consensus, smsg, mempool, db, all");

    Logger& logger = Logger::GetInstance();

    std::string levelStr = params[0].get<std::string>();
    LogLevel level = Logger::LevelFromString(levelStr);
    logger.SetLogLevel(level);

    if (params.size() > 1) {
        std::string catStr = params[1].get<std::string>();
        uint32_t cats = 0;
        std::istringstream ss(catStr);
        std::string token;
        while (std::getline(ss, token, ',')) {
            while (!token.empty() && std::isspace(static_cast<unsigned char>(token.front()))) token.erase(token.begin());
            while (!token.empty() && std::isspace(static_cast<unsigned char>(token.back()))) token.pop_back();
            BCLog::Category cat = Logger::CategoryFromString(token);
            cats |= static_cast<uint32_t>(cat);
        }
        logger.SetCategories(cats);
    }

    json result;
    result["level"] = Logger::LevelToString(logger.GetLogLevel());

    uint32_t cats = logger.GetCategories();
    json catArray = json::array();
    if (cats & BCLog::NET)       catArray.push_back("net");
    if (cats & BCLog::WALLET)    catArray.push_back("wallet");
    if (cats & BCLog::STAKE)     catArray.push_back("stake");
    if (cats & BCLog::RPC)       catArray.push_back("rpc");
    if (cats & BCLog::CONSENSUS) catArray.push_back("consensus");
    if (cats & BCLog::SMSG)      catArray.push_back("smsg");
    if (cats & BCLog::MEMPOOL)   catArray.push_back("mempool");
    if (cats & BCLog::DB)        catArray.push_back("db");
    result["categories"] = catArray;
    return result;
}

json getloglevel(const json& params, bool fHelp)
{
    if (fHelp || params.size() > 0)
        throw std::runtime_error(
            "getloglevel\n"
            "Returns the current logging level and enabled categories.");

    Logger& logger = Logger::GetInstance();

    json result;
    result["level"] = Logger::LevelToString(logger.GetLogLevel());

    uint32_t cats = logger.GetCategories();
    json catArray = json::array();
    if (cats & BCLog::NET)       catArray.push_back("net");
    if (cats & BCLog::WALLET)    catArray.push_back("wallet");
    if (cats & BCLog::STAKE)     catArray.push_back("stake");
    if (cats & BCLog::RPC)       catArray.push_back("rpc");
    if (cats & BCLog::CONSENSUS) catArray.push_back("consensus");
    if (cats & BCLog::SMSG)      catArray.push_back("smsg");
    if (cats & BCLog::MEMPOOL)   catArray.push_back("mempool");
    if (cats & BCLog::DB)        catArray.push_back("db");
    result["categories"] = catArray;

    return result;
}


//
// Call Table
//


static const CRPCCommand vRPCCommands[] =
{ //  name                      function                 safemd  unlocked
  //  ------------------------  -----------------------  ------  --------
    { "help",                   &help,                   true,   true },
    { "stop",                   &stop,                   true,   true },
    { "setloglevel",            &setloglevel,            true,   true },
    { "getloglevel",            &getloglevel,            true,   true },
    { "getbestblockhash",       &getbestblockhash,       true,   false },
    { "getblockcount",          &getblockcount,          true,   false },
    { "getconnectioncount",     &getconnectioncount,     true,   false },
    { "getnodes",               &getnodes,               true,   false },
    { "getpeerinfo",            &getpeerinfo,            true,   false },
    { "getdifficulty",          &getdifficulty,          true,   false },
    { "getinfo",                &getinfo,                true,   false },
    { "getsubsidy",             &getsubsidy,             true,   false },
    { "getmininginfo",          &getmininginfo,          true,   false },
    { "getstakinginfo",         &getstakinginfo,         true,   false },
    { "getnewaddress",          &getnewaddress,          true,   false },
    { "getnewpubkey",           &getnewpubkey,           true,   false },
    { "getaccountaddress",      &getaccountaddress,      true,   false },
    { "setaccount",             &setaccount,             true,   false },
    { "getaccount",             &getaccount,             false,  false },
    { "getaddressesbyaccount",  &getaddressesbyaccount,  true,   false },
    { "sendtoaddress",          &sendtoaddress,          false,  false },
    { "getreceivedbyaddress",   &getreceivedbyaddress,   false,  false },
    { "getreceivedbyaccount",   &getreceivedbyaccount,   false,  false },
    { "listreceivedbyaddress",  &listreceivedbyaddress,  false,  false },
    { "listreceivedbyaccount",  &listreceivedbyaccount,  false,  false },
    { "backupwallet",           &backupwallet,           true,   false },
    { "keypoolrefill",          &keypoolrefill,          true,   false },
    { "walletpassphrase",       &walletpassphrase,       true,   false },
    { "walletpassphrasechange", &walletpassphrasechange, false,  false },
    { "walletlock",             &walletlock,             true,   false },
    { "encryptwallet",          &encryptwallet,          false,  false },
    { "validateaddress",        &validateaddress,        true,   false },
    { "validatepubkey",         &validatepubkey,         true,   false },
    { "getbalance",             &getbalance,             false,  false },
    { "move",                   &movecmd,                false,  false },
    { "sendfrom",               &sendfrom,               false,  false },
    { "sendmany",               &sendmany,               false,  false },
    { "addmultisigaddress",     &addmultisigaddress,     false,  false },
    { "addredeemscript",        &addredeemscript,        false,  false },
    { "getrawmempool",          &getrawmempool,          true,   false },
    { "getblock",               &getblock,               false,  false },
    { "getblockbynumber",       &getblockbynumber,       false,  false },
    { "getblockhash",           &getblockhash,           false,  false },
    { "gettransaction",         &gettransaction,         false,  false },
    { "listtransactions",       &listtransactions,       false,  false },
    { "listaddressgroupings",   &listaddressgroupings,   false,  false },
    { "signmessage",            &signmessage,            false,  false },
    { "verifymessage",          &verifymessage,          false,  false },
    { "getwork",                &getwork,                true,   false },
    { "getworkex",              &getworkex,              true,   false },
    { "listaccounts",           &listaccounts,           false,  false },
    { "settxfee",               &settxfee,               false,  false },
    { "getblocktemplate",       &getblocktemplate,       true,   false },
    { "submitblock",            &submitblock,            false,  false },
    { "listsinceblock",         &listsinceblock,         false,  false },
    { "dumpprivkey",            &dumpprivkey,            false,  false },
    { "dumpwallet",             &dumpwallet,             true,   false },
    { "importwallet",           &importwallet,           false,  false },
    { "importprivkey",          &importprivkey,          false,  false },
    { "listunspent",            &listunspent,            false,  false },
    { "getrawtransaction",      &getrawtransaction,      false,  false },
    { "createrawtransaction",   &createrawtransaction,   false,  false },
    { "decoderawtransaction",   &decoderawtransaction,   false,  false },
    { "decodescript",           &decodescript,           false,  false },
    { "signrawtransaction",     &signrawtransaction,     false,  false },
    { "sendrawtransaction",     &sendrawtransaction,     false,  false },
    { "getcheckpoint",          &getcheckpoint,          true,   false },
    { "reservebalance",         &reservebalance,         false,  true},
    { "combinethreshold",       &combinethreshold,         false,  true},
    { "splitthreshold",         &splitthreshold,         false,  true},
    { "checkwallet",            &checkwallet,            false,  true},
    { "repairwallet",           &repairwallet,           false,  true},
    { "resendtx",               &resendtx,               false,  true},
    { "makekeypair",            &makekeypair,            false,  true},
    { "sendalert",              &sendalert,              false,  false},
	{ "setstakesplitthreshold",  &setstakesplitthreshold,  false,  false},
	{ "getstakesplitthreshold",  &getstakesplitthreshold,  false,  false},

    { "getnewstealthaddress",   &getnewstealthaddress,   false,  false},
    { "liststealthaddresses",   &liststealthaddresses,   false,  false},
    { "importstealthaddress",   &importstealthaddress,   false,  false},
    { "sendtostealthaddress",   &sendtostealthaddress,   false,  false},
    { "clearwallettransactions",&clearwallettransactions,false,  false},
    { "scanforalltxns",         &scanforalltxns,         false,  false},
    { "scanforstealthtxns",     &scanforstealthtxns,     false,  false},
    { "getwalletinfo",          &getwalletinfo,          true,   false},

    { "addstakeout",            &addstakeout,            false,  false},
    { "delstakeout",            &delstakeout,            false,  false},
    { "liststakeout",           &liststakeout,           false,  false},
    { "getstakeoutinfo",        &getstakeoutinfo,        false,  false},

    { "smsgenable",             &smsgenable,             false,  false},
    { "smsgdisable",            &smsgdisable,            false,  false},
    { "smsglocalkeys",          &smsglocalkeys,          false,  false},
    { "smsgoptions",            &smsgoptions,            false,  false},
    { "smsgscanchain",          &smsgscanchain,          false,  false},
    { "smsgscanbuckets",        &smsgscanbuckets,        false,  false},
    { "smsgaddkey",             &smsgaddkey,             false,  false},
    { "smsggetpubkey",          &smsggetpubkey,          false,  false},
    { "smsgsend",               &smsgsend,               false,  false},
    { "smsgsendanon",           &smsgsendanon,           false,  false},
    { "smsginbox",              &smsginbox,              false,  false},
    { "smsgoutbox",             &smsgoutbox,             false,  false},
    { "smsgbuckets",            &smsgbuckets,            false,  false},
};

CRPCTable::CRPCTable()
{
    unsigned int vcidx;
    for (vcidx = 0; vcidx < (sizeof(vRPCCommands) / sizeof(vRPCCommands[0])); vcidx++)
    {
        const CRPCCommand *pcmd;

        pcmd = &vRPCCommands[vcidx];
        mapCommands[pcmd->name] = pcmd;
    }
}

const CRPCCommand *CRPCTable::operator[](std::string name) const
{
    auto it = mapCommands.find(name);
    if (it == mapCommands.end())
        return nullptr;
    return it->second;
}

//
// HTTP protocol
//
// This ain't Apache.  We're just using HTTP header for the length field
// and to be compatible with other JSON-RPC implementations.
//

std::string HTTPPost(const std::string& strMsg, const std::map<std::string,std::string>& mapRequestHeaders)
{
    std::ostringstream s;
    s << "POST / HTTP/1.1\r\n"
      << "User-Agent: pinkcoin-json-rpc/" << FormatFullVersion() << "\r\n"
      << "Host: 127.0.0.1\r\n"
      << "Content-Type: application/json\r\n"
      << "Content-Length: " << strMsg.size() << "\r\n"
      << "Connection: close\r\n"
      << "Accept: application/json\r\n";
    for (const auto& item : mapRequestHeaders)
        s << item.first << ": " << item.second << "\r\n";
    s << "\r\n" << strMsg;

    return s.str();
}

std::string rfc1123Time()
{
    char buffer[64];
    time_t now;
    time(&now);
    struct tm* now_gmt = gmtime(&now);
    std::string locale(setlocale(LC_TIME, nullptr));
    setlocale(LC_TIME, "C"); // we want POSIX (aka "C") weekday/month strings
    strftime(buffer, sizeof(buffer), "%a, %d %b %Y %H:%M:%S +0000", now_gmt);
    setlocale(LC_TIME, locale.c_str());
    return std::string(buffer);
}

static std::string HTTPReply(int nStatus, const std::string& strMsg, bool keepalive)
{
    if (nStatus == HTTP_UNAUTHORIZED)
        return strprintf("HTTP/1.0 401 Authorization Required\r\n"
            "Date: %s\r\n"
            "Server: pinkcoin-json-rpc/%s\r\n"
            "WWW-Authenticate: Basic realm=\"jsonrpc\"\r\n"
            "Content-Type: text/html\r\n"
            "Content-Length: 296\r\n"
            "\r\n"
            "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.01 Transitional//EN\"\r\n"
            "\"http://www.w3.org/TR/1999/REC-html401-19991224/loose.dtd\">\r\n"
            "<HTML>\r\n"
            "<HEAD>\r\n"
            "<TITLE>Error</TITLE>\r\n"
            "<META HTTP-EQUIV='Content-Type' CONTENT='text/html; charset=ISO-8859-1'>\r\n"
            "</HEAD>\r\n"
            "<BODY><H1>401 Unauthorized.</H1></BODY>\r\n"
            "</HTML>\r\n", rfc1123Time().c_str(), FormatFullVersion().c_str());
    const char *cStatus;
         if (nStatus == HTTP_OK) cStatus = "OK";
    else if (nStatus == HTTP_BAD_REQUEST) cStatus = "Bad Request";
    else if (nStatus == HTTP_FORBIDDEN) cStatus = "Forbidden";
    else if (nStatus == HTTP_NOT_FOUND) cStatus = "Not Found";
    else if (nStatus == HTTP_INTERNAL_SERVER_ERROR) cStatus = "Internal Server Error";
    else cStatus = "";
    return strprintf(
            "HTTP/1.1 %d %s\r\n"
            "Date: %s\r\n"
            "Connection: %s\r\n"
            "Content-Length: %" PRIszu "\r\n"
            "Content-Type: application/json\r\n"
            "Server: pinkcoin-json-rpc/%s\r\n"
            "\r\n"
            "%s",
        nStatus,
        cStatus,
        rfc1123Time().c_str(),
        keepalive ? "keep-alive" : "close",
        strMsg.size(),
        FormatFullVersion().c_str(),
        strMsg.c_str());
}

bool ReadHTTPRequestLine(std::basic_istream<char>& stream, int &proto,
                         std::string& http_method, std::string& http_uri)
{
    std::string str;
    std::getline(stream, str);

    // HTTP request line is space-delimited
    std::vector<std::string> vWords = strutil::split(str, " ");
    if (vWords.size() < 2)
        return false;

    // HTTP methods permitted: GET, POST
    http_method = vWords[0];
    if (http_method != "GET" && http_method != "POST")
        return false;

    // HTTP URI must be an absolute path, relative to current host
    http_uri = vWords[1];
    if (http_uri.empty() || http_uri[0] != '/')
        return false;

    // parse proto, if present
    std::string strProto = "";
    if (vWords.size() > 2)
        strProto = vWords[2];

    proto = 0;
    const char *ver = strstr(strProto.c_str(), "HTTP/1.");
    if (ver != nullptr)
        proto = atoi(ver+7);

    return true;
}

int ReadHTTPStatus(std::basic_istream<char>& stream, int &proto)
{
    std::string str;
    std::getline(stream, str);
    std::vector<std::string> vWords = strutil::split(str, " ");
    if (vWords.size() < 2)
        return HTTP_INTERNAL_SERVER_ERROR;
    proto = 0;
    const char *ver = strstr(str.c_str(), "HTTP/1.");
    if (ver != nullptr)
        proto = atoi(ver+7);
    return atoi(vWords[1].c_str());
}

int ReadHTTPHeaders(std::basic_istream<char>& stream, std::map<std::string, std::string>& mapHeadersRet)
{
    int nLen = 0;
    while (true)
    {
        std::string str;
        std::getline(stream, str);
        if (str.empty() || str == "\r")
            break;
        std::string::size_type nColon = str.find(":");
        if (nColon != std::string::npos)
        {
            std::string strHeader = str.substr(0, nColon);
            strutil::trim(strHeader);
            strutil::to_lower(strHeader);
            std::string strValue = str.substr(nColon+1);
            strutil::trim(strValue);
            mapHeadersRet[strHeader] = strValue;
            if (strHeader == "content-length")
                nLen = atoi(strValue.c_str());
        }
    }
    return nLen;
}

int ReadHTTPMessage(std::basic_istream<char>& stream, std::map<std::string,
                    std::string>& mapHeadersRet, std::string& strMessageRet,
                    int nProto)
{
    mapHeadersRet.clear();
    strMessageRet = "";

    // Read header
    int nLen = ReadHTTPHeaders(stream, mapHeadersRet);
    if (nLen < 0 || nLen > static_cast<int>(MAX_SIZE))
        return HTTP_INTERNAL_SERVER_ERROR;

    // Read message
    if (nLen > 0)
    {
        std::vector<char> vch(nLen);
        stream.read(&vch[0], nLen);
        strMessageRet = std::string(vch.begin(), vch.end());
    }

    std::string sConHdr = mapHeadersRet["connection"];

    if ((sConHdr != "close") && (sConHdr != "keep-alive"))
    {
        if (nProto >= 1)
            mapHeadersRet["connection"] = "keep-alive";
        else
            mapHeadersRet["connection"] = "close";
    }

    return HTTP_OK;
}

bool HTTPAuthorized(std::map<std::string, std::string>& mapHeaders)
{
    std::string strAuth = mapHeaders["authorization"];
    if (strAuth.substr(0,6) != "Basic ")
        return false;
    std::string strUserPass64 = strAuth.substr(6); strutil::trim(strUserPass64);
    std::string strUserPass = DecodeBase64(strUserPass64);
    return TimingResistantEqual(strUserPass, strRPCUserColonPass);
}

//
// JSON-RPC protocol.  Bitcoin speaks version 1.0 for maximum compatibility,
// but uses JSON-RPC 1.1/2.0 standards for parts of the 1.0 standard that were
// unspecified (HTTP errors and contents of 'error').
//
// 1.0 spec: http://json-rpc.org/wiki/specification
// 1.2 spec: http://groups.google.com/group/json-rpc/web/json-rpc-over-http
//

std::string JSONRPCRequest(const std::string& strMethod, const json& params, const json& id)
{
    json request;
    request["method"] = strMethod;
    request["params"] = params;
    request["id"] = id;
    return request.dump() + "\n";
}

json JSONRPCReplyObj(const json& result, const json& error, const json& id)
{
    json reply;
    if (!error.is_null())
        reply["result"] = nullptr;
    else
        reply["result"] = result;
    reply["error"] = error;
    reply["id"] = id;
    return reply;
}

std::string JSONRPCReply(const json& result, const json& error, const json& id)
{
    json reply = JSONRPCReplyObj(result, error, id);
    return reply.dump() + "\n";
}

void ErrorReply(std::ostream& stream, const json& objError, const json& id)
{
    // Send error reply from json-rpc error object
    int nStatus = HTTP_INTERNAL_SERVER_ERROR;
    int code = objError["code"].get<int>();
    if (code == RPC_INVALID_REQUEST) nStatus = HTTP_BAD_REQUEST;
    else if (code == RPC_METHOD_NOT_FOUND) nStatus = HTTP_NOT_FOUND;
    std::string strReply = JSONRPCReply(nullptr, objError, id);
    stream << HTTPReply(nStatus, strReply, false) << std::flush;
}

bool ClientAllowed(const asio::ip::address& address)
{
    // Make sure that IPv4-compatible and IPv4-mapped IPv6 addresses are treated as IPv4 addresses
    if (address.is_v6()
     && address.to_v6().is_v4_mapped())
        return ClientAllowed(make_address_v4(asio::ip::v4_mapped, address.to_v6()));

    if (address == asio::ip::address_v4::loopback()
     || address == asio::ip::address_v6::loopback()
     || (address.is_v4()
         // Check whether IPv4 addresses match 127.0.0.0/8 (loopback subnet)
      && (address.to_v4().to_uint() & 0xff000000) == 0x7f000000))
        return true;

    const std::string strAddress = address.to_string();
    const std::vector<std::string>& vAllow = mapMultiArgs["-rpcallowip"];
    for (const std::string& strAllow : vAllow)
        if (WildcardMatch(strAddress, strAllow))
            return true;
    return false;
}

class AcceptedConnection
{
public:
    virtual ~AcceptedConnection() {}

    virtual std::iostream& stream() = 0;
    virtual std::string peer_address_to_string() const = 0;
    virtual void close() = 0;
};

template <typename Protocol>
class AcceptedConnectionImpl : public AcceptedConnection
{
public:
    AcceptedConnectionImpl(
            ioContext& io_ctx,
            asio::ssl::context &context,
            bool fUseSSL) :
        sslStream(io_ctx, context),
        _streambuf(sslStream, fUseSSL),
        _stream(&_streambuf)
    {
    }

    std::iostream& stream() override
    {
        return _stream;
    }

    std::string peer_address_to_string() const override
    {
        return peer.address().to_string();
    }

    void close() override
    {
        _streambuf.close();
    }

    typename Protocol::endpoint peer;
    asio::ssl::stream<typename Protocol::socket> sslStream;

private:
    AsioSSLStreamBuf<Protocol> _streambuf;
    std::iostream _stream;
};


// Forward declaration required for RPCListen
template <typename Protocol>
static void RPCAcceptHandler(std::shared_ptr< asio::basic_socket_acceptor<Protocol> > acceptor,
                             asio::ssl::context& context,
                             bool fUseSSL,
                             AcceptedConnection* conn,
                             const asio::error_code& error);

/**
 * Sets up I/O resources to accept and handle a new connection.
 */
template <typename Protocol>
static void RPCListen(std::shared_ptr< asio::basic_socket_acceptor<Protocol> > acceptor,
                   asio::ssl::context& context,
                   const bool fUseSSL)
{
    // Accept connection
    AcceptedConnectionImpl<Protocol>* conn = new AcceptedConnectionImpl<Protocol>(GetIOServiceFromPtr(acceptor), context, fUseSSL);

    acceptor->async_accept(
            conn->sslStream.lowest_layer(),
            conn->peer,
            [acceptor, &context, fUseSSL, conn](const asio::error_code& error) {
                RPCAcceptHandler<Protocol>(acceptor, context, fUseSSL, conn, error);
            });
}

/**
 * Accept and handle incoming connection.
 */
template <typename Protocol>
static void RPCAcceptHandler(std::shared_ptr< asio::basic_socket_acceptor<Protocol> > acceptor,
                             asio::ssl::context& context,
                             const bool fUseSSL,
                             AcceptedConnection* conn,
                             const asio::error_code& error)
{
    vnThreadsRunning[THREAD_RPCLISTENER]++;

    // Immediately start accepting new connections, except when we're cancelled or our socket is closed.
    if (error != asio::error::operation_aborted
     && acceptor->is_open())
        RPCListen(acceptor, context, fUseSSL);

    // RAII guard: automatically deletes conn on error/rejection paths.
    // On the success path (NewThread), we release() to transfer ownership.
    std::unique_ptr<AcceptedConnection> guard(conn);

    AcceptedConnectionImpl<asio::ip::tcp>* tcp_conn = dynamic_cast< AcceptedConnectionImpl<asio::ip::tcp>* >(conn);

    // TODO: Actually handle errors
    if (error)
    {
        // guard destructor will delete conn
    }

    // Restrict callers by IP.  It is important to
    // do this before starting client thread, to filter out
    // certain DoS and misbehaving clients.
    else if (tcp_conn
          && !ClientAllowed(tcp_conn->peer.address()))
    {
        // Only send a 403 if we're not using SSL to prevent a DoS during the SSL handshake.
        if (!fUseSSL)
            conn->stream() << HTTPReply(HTTP_FORBIDDEN, "", false) << std::flush;
        // guard destructor will delete conn
    }

    // start HTTP client thread
    else if (!NewThread(ThreadRPCServer3, conn)) {
        LogPrintf("Failed to create RPC server client thread\n");
        // guard destructor will delete conn
    }
    else {
        // ThreadRPCServer3 now owns conn (wraps in its own unique_ptr)
        guard.release();
    }

    vnThreadsRunning[THREAD_RPCLISTENER]--;
}

void ThreadRPCServer()
{
    // Make this thread recognisable as the RPC listener
    RenameThread("pinkcoin-rpclist");
    ThreadCountGuard guard(THREAD_RPCLISTENER);
    LogPrintf("ThreadRPCServer started\n");

    strRPCUserColonPass = mapArgs["-rpcuser"] + ":" + mapArgs["-rpcpassword"];
    if ((mapArgs["-rpcpassword"] == "") ||
        (mapArgs["-rpcuser"] == mapArgs["-rpcpassword"]))
    {
        unsigned char rand_pwd[32];
        RAND_bytes(rand_pwd, 32);
        std::string strWhatAmI = "To use pinkcoind";
        if (mapArgs.count("-server"))
            strWhatAmI = strprintf(_("To use the %s option"), "\"-server\"");
        else if (mapArgs.count("-daemon"))
            strWhatAmI = strprintf(_("To use the %s option"), "\"-daemon\"");
        uiInterface.ThreadSafeMessageBox(strprintf(
            _("%s, you must set a rpcpassword in the configuration file:\n %s\n"
              "It is recommended you use the following random password:\n"
              "rpcuser=pinkcoinrpc\n"
              "rpcpassword=%s\n"
              "(you do not need to remember this password)\n"
              "The username and password MUST NOT be the same.\n"
              "If the file does not exist, create it with owner-readable-only file permissions.\n"
              "It is also recommended to set alertnotify so you are notified of problems;\n"
              "for example: alertnotify=echo %%s | mail -s \"Pinkcoin Alert\" admin@foo.com\n"),
                strWhatAmI.c_str(),
                GetConfigFile().string().c_str(),
                EncodeBase58(&rand_pwd[0],&rand_pwd[0]+32).c_str()),
            _("Error"), CClientUIInterface::OK | CClientUIInterface::MODAL);
        StartShutdown();
        return;
    }

    const bool fUseSSL = GetBoolArg("-rpcssl");

    ioContext io_service;

    asio::ssl::context context(asio::ssl::context::sslv23);
    if (fUseSSL)
    {
        context.set_options(asio::ssl::context::no_sslv2);

        std::filesystem::path pathCertFile(GetArg("-rpcsslcertificatechainfile", "server.cert"));
        if (!pathCertFile.is_absolute()) pathCertFile = std::filesystem::path(GetDataDir()) / pathCertFile;
        if (std::filesystem::exists(pathCertFile)) context.use_certificate_chain_file(pathCertFile.string());
        else LogPrintf("ThreadRPCServer ERROR: missing server certificate file %s\n", pathCertFile.string().c_str());

        std::filesystem::path pathPKFile(GetArg("-rpcsslprivatekeyfile", "server.pem"));
        if (!pathPKFile.is_absolute()) pathPKFile = std::filesystem::path(GetDataDir()) / pathPKFile;
        if (std::filesystem::exists(pathPKFile)) context.use_private_key_file(pathPKFile.string(), asio::ssl::context::pem);
        else LogPrintf("ThreadRPCServer ERROR: missing server private key file %s\n", pathPKFile.string().c_str());

        std::string strCiphers = GetArg("-rpcsslciphers", "TLSv1+HIGH:!SSLv2:!aNULL:!eNULL:!AH:!3DES:@STRENGTH");
        SSL_CTX_set_cipher_list(context.native_handle(), strCiphers.c_str());
    }

    // Try a dual IPv6/IPv4 socket, falling back to separate IPv4 and IPv6 sockets
    const bool loopback = !mapArgs.count("-rpcallowip");
    asio::ip::address bindAddress = loopback ? asio::ip::address_v6::loopback() : asio::ip::address_v6::any();
    asio::ip::tcp::endpoint endpoint(bindAddress, GetArg("-rpcport", GetDefaultRPCPort()));
    asio::error_code v6_only_error;
    auto acceptor = std::make_shared<asio::ip::tcp::acceptor>(io_service);

    Signal<> StopRequests;

    bool fListening = false;
    std::string strerr;
    try
    {
        acceptor->open(endpoint.protocol());
        acceptor->set_option(asio::ip::tcp::acceptor::reuse_address(true));

        // Try making the socket dual IPv6/IPv4 (if listening on the "any" address)
        acceptor->set_option(asio::ip::v6_only(loopback), v6_only_error);

        acceptor->bind(endpoint);
        acceptor->listen(asio::socket_base::max_listen_connections);

        RPCListen(acceptor, context, fUseSSL);
        // Cancel outstanding listen-requests for this acceptor when shutting down
        {
            auto acc = acceptor;
            StopRequests.connect([acc]() { acc->close(); });
        }

        fListening = true;
    }
    catch (asio::system_error &e)
    {
        strerr = strprintf(_("An error occurred while setting up the RPC port %u for listening on IPv6, falling back to IPv4: %s"), endpoint.port(), e.what());
    }

    try {
        // If dual IPv6/IPv4 failed (or we're opening loopback interfaces only), open IPv4 separately
        if (!fListening || loopback || v6_only_error)
        {
            bindAddress = loopback ? asio::ip::address_v4::loopback() : asio::ip::address_v4::any();
            endpoint.address(bindAddress);

            acceptor = std::make_shared<asio::ip::tcp::acceptor>(io_service);
            acceptor->open(endpoint.protocol());
            acceptor->set_option(asio::ip::tcp::acceptor::reuse_address(true));
            acceptor->bind(endpoint);
            acceptor->listen(asio::socket_base::max_listen_connections);

            RPCListen(acceptor, context, fUseSSL);
            // Cancel outstanding listen-requests for this acceptor when shutting down
            {
                auto acc = acceptor;
                StopRequests.connect([acc]() { acc->close(); });
            }

            fListening = true;
        }
    }
    catch (asio::system_error &e)
    {
        strerr = strprintf(_("An error occurred while setting up the RPC port %u for listening on IPv4: %s"), endpoint.port(), e.what());
    }

    if (!fListening) {
        uiInterface.ThreadSafeMessageBox(strerr, _("Error"), CClientUIInterface::OK | CClientUIInterface::MODAL);
        StartShutdown();
        return;
    }

    vnThreadsRunning[THREAD_RPCLISTENER]--;
    while (!fShutdown)
        io_service.run_one();
    vnThreadsRunning[THREAD_RPCLISTENER]++;
    StopRequests();
}

class JSONRequest
{
public:
    json id;
    std::string strMethod;
    json params;

    JSONRequest() { id = nullptr; }
    void parse(const json& valRequest);
};

void JSONRequest::parse(const json& valRequest)
{
    // Parse request
    if (!valRequest.is_object())
        throw JSONRPCError(RPC_INVALID_REQUEST, "Invalid Request object");

    // Parse id now so errors from here on will have the id
    id = valRequest.value("id", json(nullptr));

    // Parse method
    if (!valRequest.contains("method"))
        throw JSONRPCError(RPC_INVALID_REQUEST, "Missing method");
    const json& valMethod = valRequest["method"];
    if (!valMethod.is_string())
        throw JSONRPCError(RPC_INVALID_REQUEST, "Method must be a string");
    strMethod = valMethod.get<std::string>();
    if (strMethod != "getwork" && strMethod != "getblocktemplate")
        LogPrintf("ThreadRPCServer method=%s\n", strMethod.c_str());

    // Parse params
    if (valRequest.contains("params")) {
        const json& valParams = valRequest["params"];
        if (valParams.is_array())
            params = valParams;
        else if (valParams.is_null())
            params = json::array();
        else
            throw JSONRPCError(RPC_INVALID_REQUEST, "Params must be an array");
    } else {
        params = json::array();
    }
}

static json JSONRPCExecOne(const json& req)
{
    json rpc_result;

    JSONRequest jreq;
    try {
        jreq.parse(req);

        json result = tableRPC.execute(jreq.strMethod, jreq.params);
        rpc_result = JSONRPCReplyObj(result, nullptr, jreq.id);
    }
    catch (json& objError)
    {
        rpc_result = JSONRPCReplyObj(nullptr, objError, jreq.id);
    }
    catch (std::exception& e)
    {
        rpc_result = JSONRPCReplyObj(nullptr,
                                     JSONRPCError(RPC_PARSE_ERROR, e.what()), jreq.id);
    }

    return rpc_result;
}

static std::string JSONRPCExecBatch(const json& vReq)
{
    json ret = json::array();
    for (unsigned int reqIdx = 0; reqIdx < vReq.size(); reqIdx++)
        ret.push_back(JSONRPCExecOne(vReq[reqIdx]));

    return ret.dump() + "\n";
}

static CCriticalSection cs_THREAD_RPCHANDLER;

void ThreadRPCServer3(AcceptedConnection* pconn)
{
    // Make this thread recognisable as the RPC handler
    RenameThread("pinkcoin-rpchand");

    {
        LOCK(cs_THREAD_RPCHANDLER);
        vnThreadsRunning[THREAD_RPCHANDLER]++;
    }
    std::unique_ptr<AcceptedConnection> conn(pconn);

    bool fRun = true;
    while (true)
    {
        if (fShutdown || !fRun)
        {
            conn->close();
            conn.reset();
            {
                LOCK(cs_THREAD_RPCHANDLER);
                --vnThreadsRunning[THREAD_RPCHANDLER];
            }
            return;
        }

        int nProto = 0;
        std::map<std::string, std::string> mapHeaders;
        std::string strRequest, strMethod, strURI;

        // Read HTTP request line
        if (!ReadHTTPRequestLine(conn->stream(), nProto, strMethod, strURI))
            break;

        // Read HTTP message headers and body
        ReadHTTPMessage(conn->stream(), mapHeaders, strRequest, nProto);

        // Check authorization
        if (mapHeaders.count("authorization") == 0)
        {
            conn->stream() << HTTPReply(HTTP_UNAUTHORIZED, "", false) << std::flush;
            break;
        }
        if (!HTTPAuthorized(mapHeaders))
        {
            LogPrintf("ThreadRPCServer incorrect password attempt from %s\n", conn->peer_address_to_string().c_str());
            /* Deter brute-forcing short passwords.
               If this results in a DOS the user really
               shouldn't have their RPC port exposed.*/
            if (mapArgs["-rpcpassword"].size() < 20)
                MilliSleep(250);

            conn->stream() << HTTPReply(HTTP_UNAUTHORIZED, "", false) << std::flush;
            break;
        }
        if (mapHeaders["connection"] == "close")
            fRun = false;

        JSONRequest jreq;
        try
        {
            // Parse request
            json valRequest;
            try {
                valRequest = json::parse(strRequest);
            } catch (const json::parse_error&) {
                throw JSONRPCError(RPC_PARSE_ERROR, "Parse error");
            }

            std::string strReply;

            // singleton request
            if (valRequest.is_object()) {
                jreq.parse(valRequest);

                json result = tableRPC.execute(jreq.strMethod, jreq.params);

                // Send reply
                strReply = JSONRPCReply(result, nullptr, jreq.id);

            // array of requests
            } else if (valRequest.is_array())
                strReply = JSONRPCExecBatch(valRequest);
            else
                throw JSONRPCError(RPC_PARSE_ERROR, "Top-level object parse error");

            conn->stream() << HTTPReply(HTTP_OK, strReply, fRun) << std::flush;
        }
        catch (json& objError)
        {
            ErrorReply(conn->stream(), objError, jreq.id);
            break;
        }
        catch (std::exception& e)
        {
            ErrorReply(conn->stream(), JSONRPCError(RPC_PARSE_ERROR, e.what()), jreq.id);
            break;
        }
    }

    {
        LOCK(cs_THREAD_RPCHANDLER);
        vnThreadsRunning[THREAD_RPCHANDLER]--;
    }
}

json CRPCTable::execute(const std::string &strMethod, const json &params) const
{
    // Find method
    const CRPCCommand *pcmd = tableRPC[strMethod];
    if (!pcmd)
        throw JSONRPCError(RPC_METHOD_NOT_FOUND, "Method not found");

    // Observe safe mode
    std::string strWarning = GetWarnings("rpc");
    if (strWarning != "" && !GetBoolArg("-disablesafemode") &&
        !pcmd->okSafeMode)
        throw JSONRPCError(RPC_FORBIDDEN_BY_SAFE_MODE, std::string("Safe mode: ") + strWarning);

    try
    {
        // Execute
        json result;
        {
            if (pcmd->unlocked)
                result = pcmd->actor(params, false);
            else {
                LOCK2(cs_main, pwalletMain->cs_wallet);
                result = pcmd->actor(params, false);
            }
        }
        return result;
    }
    catch (std::exception& e)
    {
        throw JSONRPCError(RPC_MISC_ERROR, e.what());
    }
}


json CallRPC(const std::string& strMethod, const json& params)
{
    if (mapArgs["-rpcuser"] == "" && mapArgs["-rpcpassword"] == "")
        throw std::runtime_error(strprintf(
            _("You must set rpcpassword=<password> in the configuration file:\n%s\n"
              "If the file does not exist, create it with owner-readable-only file permissions."),
                GetConfigFile().string().c_str()));

    // Connect to localhost
    bool fUseSSL = GetBoolArg("-rpcssl");
    ioContext io_service;
    asio::ssl::context context(asio::ssl::context::sslv23);
    context.set_options(asio::ssl::context::no_sslv2);
    asio::ssl::stream<asio::ip::tcp::socket> sslStream(io_service, context);
    AsioSSLStreamBuf<asio::ip::tcp> streambuf(sslStream, fUseSSL);
    std::iostream stream(&streambuf);
    if (!streambuf.connect(GetArg("-rpcconnect", "127.0.0.1"), GetArg("-rpcport", itostr(GetDefaultRPCPort()))))
        throw std::runtime_error("couldn't connect to server");

    // HTTP basic authentication
    std::string strUserPass64 = EncodeBase64(mapArgs["-rpcuser"] + ":" + mapArgs["-rpcpassword"]);
    std::map<std::string, std::string> mapRequestHeaders;
    mapRequestHeaders["Authorization"] = std::string("Basic ") + strUserPass64;

    // Send request
    std::string strRequest = JSONRPCRequest(strMethod, params, 1);
    std::string strPost = HTTPPost(strRequest, mapRequestHeaders);
    stream << strPost << std::flush;

    // Receive HTTP reply status
    int nProto = 0;
    int nStatus = ReadHTTPStatus(stream, nProto);

    // Receive HTTP reply message headers and body
    std::map<std::string, std::string> mapHeaders;
    std::string strReply;
    ReadHTTPMessage(stream, mapHeaders, strReply, nProto);

    if (nStatus == HTTP_UNAUTHORIZED)
        throw std::runtime_error("incorrect rpcuser or rpcpassword (authorization failed)");
    else if (nStatus >= 400 && nStatus != HTTP_BAD_REQUEST && nStatus != HTTP_NOT_FOUND && nStatus != HTTP_INTERNAL_SERVER_ERROR)
        throw std::runtime_error(strprintf("server returned HTTP error %d", nStatus));
    else if (strReply.empty())
        throw std::runtime_error("no response from server");

    // Parse reply
    json valReply;
    try {
        valReply = json::parse(strReply);
    } catch (const json::parse_error&) {
        throw std::runtime_error("couldn't parse reply from server");
    }
    if (!valReply.is_object() || valReply.empty())
        throw std::runtime_error("expected reply to have result, error and id properties");

    return valReply;
}




// Convert string parameters to their JSON-typed equivalents.
// For each command, certain positional params must be re-parsed as JSON.
static void ConvertParam(json& value, bool fAllowNull=false)
{
    if (fAllowNull && value.is_null())
        return;
    if (value.is_string())
    {
        // reinterpret string as unquoted json value
        std::string strJSON = value.get<std::string>();
        try {
            value = json::parse(strJSON);
        } catch (const json::parse_error&) {
            throw std::runtime_error(std::string("Error parsing JSON:") + strJSON);
        }
    }
}

// Convert strings to command-specific RPC representation
json RPCConvertValues(const std::string &strMethod, const std::vector<std::string> &strParams)
{
    json params = json::array();
    for (const std::string &param : strParams)
        params.push_back(param);

    int n = params.size();

    //
    // Special case non-string parameter types
    //
    if (strMethod == "stop"                   && n > 0) ConvertParam(params[0]);
    if (strMethod == "sendtoaddress"          && n > 1) ConvertParam(params[1]);
    if (strMethod == "settxfee"               && n > 0) ConvertParam(params[0]);
    if (strMethod == "getreceivedbyaddress"   && n > 1) ConvertParam(params[1]);
    if (strMethod == "getreceivedbyaccount"   && n > 1) ConvertParam(params[1]);
    if (strMethod == "listreceivedbyaddress"  && n > 0) ConvertParam(params[0]);
    if (strMethod == "listreceivedbyaddress"  && n > 1) ConvertParam(params[1]);
    if (strMethod == "listreceivedbyaccount"  && n > 0) ConvertParam(params[0]);
    if (strMethod == "listreceivedbyaccount"  && n > 1) ConvertParam(params[1]);
    if (strMethod == "getbalance"             && n > 1) ConvertParam(params[1]);
    if (strMethod == "getblock"               && n > 1) ConvertParam(params[1]);
    if (strMethod == "getblockbynumber"       && n > 0) ConvertParam(params[0]);
    if (strMethod == "getblockbynumber"       && n > 1) ConvertParam(params[1]);
    if (strMethod == "getblockhash"           && n > 0) ConvertParam(params[0]);
    if (strMethod == "move"                   && n > 2) ConvertParam(params[2]);
    if (strMethod == "move"                   && n > 3) ConvertParam(params[3]);
    if (strMethod == "sendfrom"               && n > 2) ConvertParam(params[2]);
    if (strMethod == "sendfrom"               && n > 3) ConvertParam(params[3]);
    if (strMethod == "listtransactions"       && n > 1) ConvertParam(params[1]);
    if (strMethod == "listtransactions"       && n > 2) ConvertParam(params[2]);
    if (strMethod == "listaccounts"           && n > 0) ConvertParam(params[0]);
    if (strMethod == "walletpassphrase"       && n > 1) ConvertParam(params[1]);
    if (strMethod == "walletpassphrase"       && n > 2) ConvertParam(params[2]);
    if (strMethod == "getblocktemplate"       && n > 0) ConvertParam(params[0]);
    if (strMethod == "listsinceblock"         && n > 1) ConvertParam(params[1]);

    if (strMethod == "sendalert"              && n > 2) ConvertParam(params[2]);
    if (strMethod == "sendalert"              && n > 3) ConvertParam(params[3]);
    if (strMethod == "sendalert"              && n > 4) ConvertParam(params[4]);
    if (strMethod == "sendalert"              && n > 5) ConvertParam(params[5]);
    if (strMethod == "sendalert"              && n > 6) ConvertParam(params[6]);

    if (strMethod == "sendmany"               && n > 1) ConvertParam(params[1]);
    if (strMethod == "sendmany"               && n > 2) ConvertParam(params[2]);
    if (strMethod == "reservebalance"         && n > 0) ConvertParam(params[0]);
    if (strMethod == "reservebalance"         && n > 1) ConvertParam(params[1]);
    if (strMethod == "combinethreshold"       && n > 0) ConvertParam(params[0]);
    if (strMethod == "splitthreshold"         && n > 0) ConvertParam(params[0]);
    if (strMethod == "addmultisigaddress"     && n > 0) ConvertParam(params[0]);
    if (strMethod == "addmultisigaddress"     && n > 1) ConvertParam(params[1]);
    if (strMethod == "listunspent"            && n > 0) ConvertParam(params[0]);
    if (strMethod == "listunspent"            && n > 1) ConvertParam(params[1]);
    if (strMethod == "listunspent"            && n > 2) ConvertParam(params[2]);
    if (strMethod == "getrawtransaction"      && n > 1) ConvertParam(params[1]);
    if (strMethod == "createrawtransaction"   && n > 0) ConvertParam(params[0]);
    if (strMethod == "createrawtransaction"   && n > 1) ConvertParam(params[1]);
    if (strMethod == "signrawtransaction"     && n > 1) ConvertParam(params[1], true);
    if (strMethod == "signrawtransaction"     && n > 2) ConvertParam(params[2], true);
    if (strMethod == "keypoolrefill"          && n > 0) ConvertParam(params[0]);

    if (strMethod == "sendtostealthaddress"   && n > 1) ConvertParam(params[1]);

    return params;
}

int CommandLineRPC(int argc, char *argv[])
{
    std::string strPrint;
    int nRet = 0;
    try
    {
        // Skip switches
        while (argc > 1 && IsSwitchChar(argv[1][0]))
        {
            argc--;
            argv++;
        }

        // Method
        if (argc < 2)
            throw std::runtime_error("too few parameters");
        std::string strMethod = argv[1];

        // Parameters default to strings
        std::vector<std::string> strParams(&argv[2], &argv[argc]);
        json params = RPCConvertValues(strMethod, strParams);

        // Execute
        json reply = CallRPC(strMethod, params);

        // Parse reply
        const json& result = reply.contains("result") ? reply["result"] : json(nullptr);
        const json& error  = reply.contains("error") ? reply["error"] : json(nullptr);

        if (!error.is_null())
        {
            // Error
            strPrint = "error: " + error.dump();
            int code = error["code"].get<int>();
            nRet = abs(code);
        }
        else
        {
            // Result
            if (result.is_null())
                strPrint = "";
            else if (result.is_string())
                strPrint = result.get<std::string>();
            else
                strPrint = result.dump(4);
        }
    }
    catch (std::exception& e)
    {
        strPrint = std::string("error: ") + e.what();
        nRet = 87;
    }
    catch (...)
    {
        PrintException(nullptr, "CommandLineRPC()");
    }

    if (strPrint != "")
    {
        fprintf((nRet == 0 ? stdout : stderr), "%s\n", strPrint.c_str());
    }
    return nRet;
}

#ifdef TEST
int main(int argc, char *argv[])
{
#ifdef _MSC_VER
    // Turn off Microsoft heap dump noise
    _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_WARN, CreateFile("NUL", GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, 0));
#endif
    setbuf(stdin, nullptr);
    setbuf(stdout, nullptr);
    setbuf(stderr, nullptr);

    try
    {
        if (argc >= 2 && std::string(argv[1]) == "-server")
        {
            LogPrintf("server ready\n");
            ThreadRPCServer(nullptr);
        }
        else
        {
            return CommandLineRPC(argc, argv);
        }
    }
    catch (std::exception& e) {
        PrintException(&e, "main()");
    } catch (...) {
        PrintException(nullptr, "main()");
    }
    return 0;
}
#endif

const CRPCTable tableRPC;
