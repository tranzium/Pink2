// Copyright (c) 2024 The Pinkcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <boost/test/unit_test.hpp>

#include <sstream>
#include <string>
#include <map>

#include "bitcoinrpc.h"
#include "util.h"
#include "version.h"

// Extern declarations for functions in bitcoinrpc.cpp not declared in header
extern std::string HTTPPost(const std::string& strMsg, const std::map<std::string,std::string>& mapRequestHeaders);
extern std::string rfc1123Time();
extern bool ReadHTTPRequestLine(std::basic_istream<char>& stream, int &proto,
                                std::string& http_method, std::string& http_uri);
extern int ReadHTTPStatus(std::basic_istream<char>& stream, int &proto);
extern int ReadHTTPHeaders(std::basic_istream<char>& stream, std::map<std::string, std::string>& mapHeadersRet);
extern int ReadHTTPMessage(std::basic_istream<char>& stream, std::map<std::string, std::string>& mapHeadersRet,
                           std::string& strMessageRet, int nProto);
extern std::string JSONRPCRequest(const std::string& strMethod, const json& params, const json& id);
extern json JSONRPCReplyObj(const json& result, const json& error, const json& id);
extern std::string JSONRPCReply(const json& result, const json& error, const json& id);
extern void ErrorReply(std::ostream& stream, const json& objError, const json& id);

BOOST_AUTO_TEST_SUITE(rpc_framework_tests)

// ============================================================================
// HTTP protocol functions
// ============================================================================

BOOST_AUTO_TEST_CASE(httppost_format)
{
    // Verify POST request formatting
    std::map<std::string,std::string> headers;
    std::string result = HTTPPost("test body", headers);

    BOOST_CHECK(result.find("POST / HTTP/1.1\r\n") != std::string::npos);
    BOOST_CHECK(result.find("Content-Type: application/json\r\n") != std::string::npos);
    BOOST_CHECK(result.find("Content-Length: 9\r\n") != std::string::npos);
    BOOST_CHECK(result.find("Connection: close\r\n") != std::string::npos);
    BOOST_CHECK(result.find("Accept: application/json\r\n") != std::string::npos);
    BOOST_CHECK(result.find("Host: 127.0.0.1\r\n") != std::string::npos);
    // Body appears after blank line
    BOOST_CHECK(result.find("\r\n\r\ntest body") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(httppost_custom_headers)
{
    std::map<std::string,std::string> headers;
    headers["X-Custom"] = "value1";
    headers["Authorization"] = "Basic abc123";
    std::string result = HTTPPost("{}", headers);

    BOOST_CHECK(result.find("X-Custom: value1\r\n") != std::string::npos);
    BOOST_CHECK(result.find("Authorization: Basic abc123\r\n") != std::string::npos);
    BOOST_CHECK(result.find("Content-Length: 2\r\n") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(httppost_empty_body)
{
    std::map<std::string,std::string> headers;
    std::string result = HTTPPost("", headers);
    BOOST_CHECK(result.find("Content-Length: 0\r\n") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(rfc1123time_format)
{
    std::string t = rfc1123Time();

    // RFC 1123 format: "Thu, 13 Feb 2026 00:00:00 +0000" (31 chars)
    BOOST_CHECK_EQUAL(t.size(), 31u);

    // Ends with "+0000" (UTC)
    BOOST_CHECK_EQUAL(t.substr(t.size() - 5), "+0000");

    // Contains a comma after day-of-week
    BOOST_CHECK(t.find(',') != std::string::npos);
    BOOST_CHECK_EQUAL(t[3], ',');
}

BOOST_AUTO_TEST_CASE(readhttprequestline_valid_post)
{
    std::istringstream stream("POST / HTTP/1.1\r\n");
    int proto = 0;
    std::string method, uri;
    BOOST_CHECK(ReadHTTPRequestLine(stream, proto, method, uri));
    BOOST_CHECK_EQUAL(method, "POST");
    BOOST_CHECK_EQUAL(uri, "/");
    BOOST_CHECK_EQUAL(proto, 1);
}

BOOST_AUTO_TEST_CASE(readhttprequestline_valid_get)
{
    std::istringstream stream("GET /index HTTP/1.0\r\n");
    int proto = 0;
    std::string method, uri;
    BOOST_CHECK(ReadHTTPRequestLine(stream, proto, method, uri));
    BOOST_CHECK_EQUAL(method, "GET");
    BOOST_CHECK_EQUAL(uri, "/index");
    BOOST_CHECK_EQUAL(proto, 0);
}

BOOST_AUTO_TEST_CASE(readhttprequestline_reject_invalid_method)
{
    std::istringstream stream("PUT / HTTP/1.1\r\n");
    int proto = 0;
    std::string method, uri;
    BOOST_CHECK(!ReadHTTPRequestLine(stream, proto, method, uri));
}

BOOST_AUTO_TEST_CASE(readhttprequestline_reject_insufficient)
{
    // Only one word — not enough
    std::istringstream stream("POST\r\n");
    int proto = 0;
    std::string method, uri;
    BOOST_CHECK(!ReadHTTPRequestLine(stream, proto, method, uri));
}

BOOST_AUTO_TEST_CASE(readhttpstatus_valid)
{
    std::istringstream stream("HTTP/1.1 200 OK\r\n");
    int proto = 0;
    int status = ReadHTTPStatus(stream, proto);
    BOOST_CHECK_EQUAL(status, 200);
    BOOST_CHECK_EQUAL(proto, 1);
}

BOOST_AUTO_TEST_CASE(readhttpstatus_bad_input)
{
    std::istringstream stream("garbage\r\n");
    int proto = 0;
    int status = ReadHTTPStatus(stream, proto);
    BOOST_CHECK_EQUAL(status, HTTP_INTERNAL_SERVER_ERROR);
}

BOOST_AUTO_TEST_CASE(readhttpheaders_content_length)
{
    std::istringstream stream("Content-Length: 42\r\nHost: localhost\r\n\r\n");
    std::map<std::string,std::string> headers;
    int nLen = ReadHTTPHeaders(stream, headers);
    BOOST_CHECK_EQUAL(nLen, 42);
    BOOST_CHECK_EQUAL(headers["content-length"], "42");
    BOOST_CHECK_EQUAL(headers["host"], "localhost");
}

BOOST_AUTO_TEST_CASE(readhttpheaders_empty)
{
    std::istringstream stream("\r\n");
    std::map<std::string,std::string> headers;
    int nLen = ReadHTTPHeaders(stream, headers);
    BOOST_CHECK_EQUAL(nLen, 0);
    BOOST_CHECK(headers.empty());
}

BOOST_AUTO_TEST_CASE(readhttpmessage_full_parse)
{
    std::string raw = "Content-Length: 5\r\n\r\nhello";
    std::istringstream stream(raw);
    std::map<std::string,std::string> headers;
    std::string body;
    int status = ReadHTTPMessage(stream, headers, body, 1);
    BOOST_CHECK_EQUAL(status, HTTP_OK);
    BOOST_CHECK_EQUAL(body, "hello");
    BOOST_CHECK_EQUAL(headers["content-length"], "5");
    // HTTP/1.1 defaults to keep-alive
    BOOST_CHECK_EQUAL(headers["connection"], "keep-alive");
}

BOOST_AUTO_TEST_CASE(readhttpmessage_http10_close)
{
    std::string raw = "Content-Length: 3\r\n\r\nabc";
    std::istringstream stream(raw);
    std::map<std::string,std::string> headers;
    std::string body;
    int status = ReadHTTPMessage(stream, headers, body, 0);
    BOOST_CHECK_EQUAL(status, HTTP_OK);
    BOOST_CHECK_EQUAL(body, "abc");
    // HTTP/1.0 defaults to close
    BOOST_CHECK_EQUAL(headers["connection"], "close");
}

// ============================================================================
// JSON-RPC protocol functions
// ============================================================================

BOOST_AUTO_TEST_CASE(jsonrpc_request_format)
{
    json params = json::array();
    params.push_back("arg1");
    params.push_back(42);
    std::string req = JSONRPCRequest("testmethod", params, json(1));

    // Parse the result back
    json v = json::parse(req);
    BOOST_CHECK_EQUAL(v["method"].get<std::string>(), "testmethod");
    BOOST_CHECK_EQUAL(v["id"].get<int>(), 1);
    json parsedParams = v["params"];
    BOOST_CHECK_EQUAL(parsedParams.size(), 2u);
    BOOST_CHECK_EQUAL(parsedParams[0].get<std::string>(), "arg1");
    BOOST_CHECK_EQUAL(parsedParams[1].get<int>(), 42);
}

BOOST_AUTO_TEST_CASE(jsonrpc_replyobj_success)
{
    // No error → result present
    json reply = JSONRPCReplyObj(json("ok"), nullptr, json(1));
    BOOST_CHECK_EQUAL(reply["result"].get<std::string>(), "ok");
    BOOST_CHECK(reply["error"].is_null());
    BOOST_CHECK_EQUAL(reply["id"].get<int>(), 1);
}

BOOST_AUTO_TEST_CASE(jsonrpc_replyobj_error)
{
    // Error present → result is null
    json error = JSONRPCError(RPC_METHOD_NOT_FOUND, "not found");
    json reply = JSONRPCReplyObj(json("ignored"), error, json(1));
    BOOST_CHECK(reply["result"].is_null());
    BOOST_CHECK(!reply["error"].is_null());
}

BOOST_AUTO_TEST_CASE(jsonrpc_reply_string)
{
    std::string reply = JSONRPCReply(json("ok"), nullptr, json(1));
    // Must be valid JSON ending with newline
    BOOST_CHECK(!reply.empty());
    BOOST_CHECK_EQUAL(reply.back(), '\n');
    json v = json::parse(reply);
    BOOST_CHECK_EQUAL(v["result"].get<std::string>(), "ok");
}

BOOST_AUTO_TEST_CASE(errorreply_invalid_request)
{
    // RPC_INVALID_REQUEST maps to HTTP 400
    json error = JSONRPCError(RPC_INVALID_REQUEST, "bad request");
    std::ostringstream stream;
    ErrorReply(stream, error, json(1));
    std::string output = stream.str();
    BOOST_CHECK(output.find("400 Bad Request") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(errorreply_method_not_found)
{
    // RPC_METHOD_NOT_FOUND maps to HTTP 404
    json error = JSONRPCError(RPC_METHOD_NOT_FOUND, "not found");
    std::ostringstream stream;
    ErrorReply(stream, error, json(1));
    std::string output = stream.str();
    BOOST_CHECK(output.find("404 Not Found") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(errorreply_other_maps_500)
{
    // Any other error code maps to HTTP 500
    json error = JSONRPCError(RPC_INTERNAL_ERROR, "internal");
    std::ostringstream stream;
    ErrorReply(stream, error, json(1));
    std::string output = stream.str();
    BOOST_CHECK(output.find("500 Internal Server Error") != std::string::npos);
}

// ============================================================================
// Command table & dispatch
// ============================================================================

BOOST_AUTO_TEST_CASE(command_table_lookup_known)
{
    const CRPCCommand* cmd = tableRPC["help"];
    BOOST_CHECK(cmd != nullptr);
    BOOST_CHECK_EQUAL(cmd->name, "help");
}

BOOST_AUTO_TEST_CASE(command_table_lookup_unknown)
{
    const CRPCCommand* cmd = tableRPC["nonexistent_command_xyz"];
    BOOST_CHECK(cmd == nullptr);
}

BOOST_AUTO_TEST_CASE(command_table_completeness)
{
    // All expected commands must be registered
    std::vector<std::string> expected = {
        "help", "stop", "getbestblockhash", "getblockcount",
        "getconnectioncount", "getnodes", "getpeerinfo", "getdifficulty",
        "getinfo", "getsubsidy", "getmininginfo", "getstakinginfo",
        "getnewaddress", "getnewpubkey", "getaccountaddress", "setaccount",
        "getaccount", "getaddressesbyaccount", "sendtoaddress",
        "getreceivedbyaddress", "getreceivedbyaccount",
        "listreceivedbyaddress", "listreceivedbyaccount",
        "backupwallet", "keypoolrefill", "walletpassphrase",
        "walletpassphrasechange", "walletlock", "encryptwallet",
        "validateaddress", "validatepubkey", "getbalance", "move",
        "sendfrom", "sendmany", "addmultisigaddress", "addredeemscript",
        "getrawmempool", "getblock", "getblockbynumber", "getblockhash",
        "gettransaction", "listtransactions", "listaddressgroupings",
        "signmessage", "verifymessage", "getwork", "getworkex",
        "listaccounts", "settxfee", "getblocktemplate", "submitblock",
        "listsinceblock", "dumpprivkey", "dumpwallet", "importwallet",
        "importprivkey", "listunspent", "getrawtransaction",
        "createrawtransaction", "decoderawtransaction", "decodescript",
        "signrawtransaction", "sendrawtransaction", "getcheckpoint",
        "reservebalance", "combinethreshold", "splitthreshold",
        "checkwallet", "repairwallet", "resendtx", "makekeypair",
        "sendalert", "setstakesplitthreshold", "getstakesplitthreshold",
        "getnewstealthaddress", "liststealthaddresses",
        "importstealthaddress", "sendtostealthaddress",
        "clearwallettransactions", "scanforalltxns", "scanforstealthtxns",
        "getwalletinfo", "addstakeout", "delstakeout", "liststakeout",
        "getstakeoutinfo",
        "smsgenable", "smsgdisable", "smsglocalkeys", "smsgoptions",
        "smsgscanchain", "smsgscanbuckets", "smsgaddkey", "smsggetpubkey",
        "smsgsend", "smsgsendanon", "smsginbox", "smsgoutbox", "smsgbuckets",
    };

    for (const std::string& name : expected) {
        const CRPCCommand* cmd = tableRPC[name];
        BOOST_CHECK_MESSAGE(cmd != nullptr, "Missing RPC command: " + name);
    }
}

BOOST_AUTO_TEST_CASE(command_table_properties)
{
    // help: okSafeMode=true, unlocked=true
    const CRPCCommand* helpCmd = tableRPC["help"];
    BOOST_REQUIRE(helpCmd != nullptr);
    BOOST_CHECK(helpCmd->okSafeMode);
    BOOST_CHECK(helpCmd->unlocked);

    // stop: okSafeMode=true, unlocked=true
    const CRPCCommand* stopCmd = tableRPC["stop"];
    BOOST_REQUIRE(stopCmd != nullptr);
    BOOST_CHECK(stopCmd->okSafeMode);
    BOOST_CHECK(stopCmd->unlocked);

    // makekeypair: okSafeMode=false, unlocked=true
    const CRPCCommand* mkCmd = tableRPC["makekeypair"];
    BOOST_REQUIRE(mkCmd != nullptr);
    BOOST_CHECK(!mkCmd->okSafeMode);
    BOOST_CHECK(mkCmd->unlocked);

    // verifymessage: okSafeMode=false, unlocked=false
    const CRPCCommand* vmCmd = tableRPC["verifymessage"];
    BOOST_REQUIRE(vmCmd != nullptr);
    BOOST_CHECK(!vmCmd->okSafeMode);
    BOOST_CHECK(!vmCmd->unlocked);
}

BOOST_AUTO_TEST_CASE(command_help_text)
{
    // Every RPC command should return help text when called with fHelp=true
    // The convention is to throw runtime_error containing the help string
    std::vector<std::string> cmds = {
        "help", "stop", "getbestblockhash", "getblockcount",
        "getdifficulty", "validateaddress", "verifymessage",
        "createrawtransaction", "decoderawtransaction", "decodescript",
        "makekeypair", "getinfo", "getbalance",
    };

    for (const std::string& name : cmds) {
        const CRPCCommand* cmd = tableRPC[name];
        BOOST_REQUIRE_MESSAGE(cmd != nullptr, "Command not found: " + name);
        json emptyParams = json::array();
        BOOST_CHECK_THROW(cmd->actor(emptyParams, true), std::runtime_error);
    }
}

// ============================================================================
// Enum pinning
// ============================================================================

BOOST_AUTO_TEST_CASE(http_status_code_values)
{
    BOOST_CHECK_EQUAL(HTTP_OK, 200);
    BOOST_CHECK_EQUAL(HTTP_BAD_REQUEST, 400);
    BOOST_CHECK_EQUAL(HTTP_UNAUTHORIZED, 401);
    BOOST_CHECK_EQUAL(HTTP_FORBIDDEN, 403);
    BOOST_CHECK_EQUAL(HTTP_NOT_FOUND, 404);
    BOOST_CHECK_EQUAL(HTTP_INTERNAL_SERVER_ERROR, 500);
}

BOOST_AUTO_TEST_CASE(rpc_error_code_values_complete)
{
    // JSON-RPC 2.0 standard errors
    BOOST_CHECK_EQUAL(RPC_INVALID_REQUEST, -32600);
    BOOST_CHECK_EQUAL(RPC_METHOD_NOT_FOUND, -32601);
    BOOST_CHECK_EQUAL(RPC_INVALID_PARAMS, -32602);
    BOOST_CHECK_EQUAL(RPC_INTERNAL_ERROR, -32603);
    BOOST_CHECK_EQUAL(RPC_PARSE_ERROR, -32700);

    // General application errors
    BOOST_CHECK_EQUAL(RPC_MISC_ERROR, -1);
    BOOST_CHECK_EQUAL(RPC_FORBIDDEN_BY_SAFE_MODE, -2);
    BOOST_CHECK_EQUAL(RPC_TYPE_ERROR, -3);
    BOOST_CHECK_EQUAL(RPC_INVALID_ADDRESS_OR_KEY, -5);
    BOOST_CHECK_EQUAL(RPC_OUT_OF_MEMORY, -7);
    BOOST_CHECK_EQUAL(RPC_INVALID_PARAMETER, -8);
    BOOST_CHECK_EQUAL(RPC_DATABASE_ERROR, -20);
    BOOST_CHECK_EQUAL(RPC_DESERIALIZATION_ERROR, -22);

    // P2P client errors
    BOOST_CHECK_EQUAL(RPC_CLIENT_NOT_CONNECTED, -9);
    BOOST_CHECK_EQUAL(RPC_CLIENT_IN_INITIAL_DOWNLOAD, -10);

    // Wallet errors
    BOOST_CHECK_EQUAL(RPC_WALLET_ERROR, -4);
    BOOST_CHECK_EQUAL(RPC_WALLET_INSUFFICIENT_FUNDS, -6);
    BOOST_CHECK_EQUAL(RPC_WALLET_INVALID_ACCOUNT_NAME, -11);
    BOOST_CHECK_EQUAL(RPC_WALLET_KEYPOOL_RAN_OUT, -12);
    BOOST_CHECK_EQUAL(RPC_WALLET_UNLOCK_NEEDED, -13);
    BOOST_CHECK_EQUAL(RPC_WALLET_PASSPHRASE_INCORRECT, -14);
    BOOST_CHECK_EQUAL(RPC_WALLET_WRONG_ENC_STATE, -15);
    BOOST_CHECK_EQUAL(RPC_WALLET_ENCRYPTION_FAILED, -16);
    BOOST_CHECK_EQUAL(RPC_WALLET_ALREADY_UNLOCKED, -17);
}

BOOST_AUTO_TEST_SUITE_END()
