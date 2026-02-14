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

using namespace std;
using namespace json_spirit;

// Extern declarations for functions in bitcoinrpc.cpp not declared in header
extern string HTTPPost(const string& strMsg, const map<string,string>& mapRequestHeaders);
extern string rfc1123Time();
extern bool ReadHTTPRequestLine(std::basic_istream<char>& stream, int &proto,
                                string& http_method, string& http_uri);
extern int ReadHTTPStatus(std::basic_istream<char>& stream, int &proto);
extern int ReadHTTPHeaders(std::basic_istream<char>& stream, map<string, string>& mapHeadersRet);
extern int ReadHTTPMessage(std::basic_istream<char>& stream, map<string, string>& mapHeadersRet,
                           string& strMessageRet, int nProto);
extern string JSONRPCRequest(const string& strMethod, const Array& params, const Value& id);
extern Object JSONRPCReplyObj(const Value& result, const Value& error, const Value& id);
extern string JSONRPCReply(const Value& result, const Value& error, const Value& id);
extern void ErrorReply(std::ostream& stream, const Object& objError, const Value& id);

BOOST_AUTO_TEST_SUITE(rpc_framework_tests)

// ============================================================================
// HTTP protocol functions
// ============================================================================

BOOST_AUTO_TEST_CASE(httppost_format)
{
    // Verify POST request formatting
    map<string,string> headers;
    string result = HTTPPost("test body", headers);

    BOOST_CHECK(result.find("POST / HTTP/1.1\r\n") != string::npos);
    BOOST_CHECK(result.find("Content-Type: application/json\r\n") != string::npos);
    BOOST_CHECK(result.find("Content-Length: 9\r\n") != string::npos);
    BOOST_CHECK(result.find("Connection: close\r\n") != string::npos);
    BOOST_CHECK(result.find("Accept: application/json\r\n") != string::npos);
    BOOST_CHECK(result.find("Host: 127.0.0.1\r\n") != string::npos);
    // Body appears after blank line
    BOOST_CHECK(result.find("\r\n\r\ntest body") != string::npos);
}

BOOST_AUTO_TEST_CASE(httppost_custom_headers)
{
    map<string,string> headers;
    headers["X-Custom"] = "value1";
    headers["Authorization"] = "Basic abc123";
    string result = HTTPPost("{}", headers);

    BOOST_CHECK(result.find("X-Custom: value1\r\n") != string::npos);
    BOOST_CHECK(result.find("Authorization: Basic abc123\r\n") != string::npos);
    BOOST_CHECK(result.find("Content-Length: 2\r\n") != string::npos);
}

BOOST_AUTO_TEST_CASE(httppost_empty_body)
{
    map<string,string> headers;
    string result = HTTPPost("", headers);
    BOOST_CHECK(result.find("Content-Length: 0\r\n") != string::npos);
}

BOOST_AUTO_TEST_CASE(rfc1123time_format)
{
    string t = rfc1123Time();

    // RFC 1123 format: "Thu, 13 Feb 2026 00:00:00 +0000" (31 chars)
    BOOST_CHECK_EQUAL(t.size(), 31u);

    // Ends with "+0000" (UTC)
    BOOST_CHECK_EQUAL(t.substr(t.size() - 5), "+0000");

    // Contains a comma after day-of-week
    BOOST_CHECK(t.find(',') != string::npos);
    BOOST_CHECK_EQUAL(t[3], ',');
}

BOOST_AUTO_TEST_CASE(readhttprequestline_valid_post)
{
    istringstream stream("POST / HTTP/1.1\r\n");
    int proto = 0;
    string method, uri;
    BOOST_CHECK(ReadHTTPRequestLine(stream, proto, method, uri));
    BOOST_CHECK_EQUAL(method, "POST");
    BOOST_CHECK_EQUAL(uri, "/");
    BOOST_CHECK_EQUAL(proto, 1);
}

BOOST_AUTO_TEST_CASE(readhttprequestline_valid_get)
{
    istringstream stream("GET /index HTTP/1.0\r\n");
    int proto = 0;
    string method, uri;
    BOOST_CHECK(ReadHTTPRequestLine(stream, proto, method, uri));
    BOOST_CHECK_EQUAL(method, "GET");
    BOOST_CHECK_EQUAL(uri, "/index");
    BOOST_CHECK_EQUAL(proto, 0);
}

BOOST_AUTO_TEST_CASE(readhttprequestline_reject_invalid_method)
{
    istringstream stream("PUT / HTTP/1.1\r\n");
    int proto = 0;
    string method, uri;
    BOOST_CHECK(!ReadHTTPRequestLine(stream, proto, method, uri));
}

BOOST_AUTO_TEST_CASE(readhttprequestline_reject_insufficient)
{
    // Only one word — not enough
    istringstream stream("POST\r\n");
    int proto = 0;
    string method, uri;
    BOOST_CHECK(!ReadHTTPRequestLine(stream, proto, method, uri));
}

BOOST_AUTO_TEST_CASE(readhttpstatus_valid)
{
    istringstream stream("HTTP/1.1 200 OK\r\n");
    int proto = 0;
    int status = ReadHTTPStatus(stream, proto);
    BOOST_CHECK_EQUAL(status, 200);
    BOOST_CHECK_EQUAL(proto, 1);
}

BOOST_AUTO_TEST_CASE(readhttpstatus_bad_input)
{
    istringstream stream("garbage\r\n");
    int proto = 0;
    int status = ReadHTTPStatus(stream, proto);
    BOOST_CHECK_EQUAL(status, HTTP_INTERNAL_SERVER_ERROR);
}

BOOST_AUTO_TEST_CASE(readhttpheaders_content_length)
{
    istringstream stream("Content-Length: 42\r\nHost: localhost\r\n\r\n");
    map<string,string> headers;
    int nLen = ReadHTTPHeaders(stream, headers);
    BOOST_CHECK_EQUAL(nLen, 42);
    BOOST_CHECK_EQUAL(headers["content-length"], "42");
    BOOST_CHECK_EQUAL(headers["host"], "localhost");
}

BOOST_AUTO_TEST_CASE(readhttpheaders_empty)
{
    istringstream stream("\r\n");
    map<string,string> headers;
    int nLen = ReadHTTPHeaders(stream, headers);
    BOOST_CHECK_EQUAL(nLen, 0);
    BOOST_CHECK(headers.empty());
}

BOOST_AUTO_TEST_CASE(readhttpmessage_full_parse)
{
    string raw = "Content-Length: 5\r\n\r\nhello";
    istringstream stream(raw);
    map<string,string> headers;
    string body;
    int status = ReadHTTPMessage(stream, headers, body, 1);
    BOOST_CHECK_EQUAL(status, HTTP_OK);
    BOOST_CHECK_EQUAL(body, "hello");
    BOOST_CHECK_EQUAL(headers["content-length"], "5");
    // HTTP/1.1 defaults to keep-alive
    BOOST_CHECK_EQUAL(headers["connection"], "keep-alive");
}

BOOST_AUTO_TEST_CASE(readhttpmessage_http10_close)
{
    string raw = "Content-Length: 3\r\n\r\nabc";
    istringstream stream(raw);
    map<string,string> headers;
    string body;
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
    Array params;
    params.push_back("arg1");
    params.push_back(42);
    string req = JSONRPCRequest("testmethod", params, Value(1));

    // Parse the result back
    Value v;
    BOOST_CHECK(read_string(req, v));
    Object obj = v.get_obj();
    BOOST_CHECK_EQUAL(find_value(obj, "method").get_str(), "testmethod");
    BOOST_CHECK_EQUAL(find_value(obj, "id").get_int(), 1);
    Array parsedParams = find_value(obj, "params").get_array();
    BOOST_CHECK_EQUAL(parsedParams.size(), 2u);
    BOOST_CHECK_EQUAL(parsedParams[0].get_str(), "arg1");
    BOOST_CHECK_EQUAL(parsedParams[1].get_int(), 42);
}

BOOST_AUTO_TEST_CASE(jsonrpc_replyobj_success)
{
    // No error → result present
    Object reply = JSONRPCReplyObj(Value("ok"), Value::null, Value(1));
    BOOST_CHECK_EQUAL(find_value(reply, "result").get_str(), "ok");
    BOOST_CHECK(find_value(reply, "error").type() == null_type);
    BOOST_CHECK_EQUAL(find_value(reply, "id").get_int(), 1);
}

BOOST_AUTO_TEST_CASE(jsonrpc_replyobj_error)
{
    // Error present → result is null
    Object error = JSONRPCError(RPC_METHOD_NOT_FOUND, "not found");
    Object reply = JSONRPCReplyObj(Value("ignored"), error, Value(1));
    BOOST_CHECK(find_value(reply, "result").type() == null_type);
    BOOST_CHECK(find_value(reply, "error").type() != null_type);
}

BOOST_AUTO_TEST_CASE(jsonrpc_reply_string)
{
    string reply = JSONRPCReply(Value("ok"), Value::null, Value(1));
    // Must be valid JSON ending with newline
    BOOST_CHECK(!reply.empty());
    BOOST_CHECK_EQUAL(reply.back(), '\n');
    Value v;
    BOOST_CHECK(read_string(reply, v));
    Object obj = v.get_obj();
    BOOST_CHECK_EQUAL(find_value(obj, "result").get_str(), "ok");
}

BOOST_AUTO_TEST_CASE(errorreply_invalid_request)
{
    // RPC_INVALID_REQUEST maps to HTTP 400
    Object error = JSONRPCError(RPC_INVALID_REQUEST, "bad request");
    ostringstream stream;
    ErrorReply(stream, error, Value(1));
    string output = stream.str();
    BOOST_CHECK(output.find("400 Bad Request") != string::npos);
}

BOOST_AUTO_TEST_CASE(errorreply_method_not_found)
{
    // RPC_METHOD_NOT_FOUND maps to HTTP 404
    Object error = JSONRPCError(RPC_METHOD_NOT_FOUND, "not found");
    ostringstream stream;
    ErrorReply(stream, error, Value(1));
    string output = stream.str();
    BOOST_CHECK(output.find("404 Not Found") != string::npos);
}

BOOST_AUTO_TEST_CASE(errorreply_other_maps_500)
{
    // Any other error code maps to HTTP 500
    Object error = JSONRPCError(RPC_INTERNAL_ERROR, "internal");
    ostringstream stream;
    ErrorReply(stream, error, Value(1));
    string output = stream.str();
    BOOST_CHECK(output.find("500 Internal Server Error") != string::npos);
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
    vector<string> expected = {
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

    for (const string& name : expected) {
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
    vector<string> cmds = {
        "help", "stop", "getbestblockhash", "getblockcount",
        "getdifficulty", "validateaddress", "verifymessage",
        "createrawtransaction", "decoderawtransaction", "decodescript",
        "makekeypair", "getinfo", "getbalance",
    };

    for (const string& name : cmds) {
        const CRPCCommand* cmd = tableRPC[name];
        BOOST_REQUIRE_MESSAGE(cmd != nullptr, "Command not found: " + name);
        Array emptyParams;
        BOOST_CHECK_THROW(cmd->actor(emptyParams, true), runtime_error);
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
