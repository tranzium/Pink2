// Copyright (c) 2024-2026 The Pinkcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Tests for HTTP parsing functions in bitcoinrpc.cpp:
// ReadHTTPRequestLine, ReadHTTPHeaders, ReadHTTPMessage, ClientAllowed.

#include <boost/test/unit_test.hpp>

#include <sstream>
#include <string>
#include <map>
#include <vector>

#include "bitcoinrpc.h"
#include "util.h"

#include <asio.hpp>

using namespace std;

// Extern declarations for functions in bitcoinrpc.cpp not exposed in header
extern bool ReadHTTPRequestLine(std::basic_istream<char>& stream, int &proto,
                                string& http_method, string& http_uri);
extern int ReadHTTPHeaders(std::basic_istream<char>& stream, map<string, string>& mapHeadersRet);
extern int ReadHTTPMessage(std::basic_istream<char>& stream, map<string, string>& mapHeadersRet,
                           string& strMessageRet, int nProto);
extern bool ClientAllowed(const asio::ip::address& address);

BOOST_AUTO_TEST_SUITE(http_tests)

// ============================================================================
// ReadHTTPRequestLine tests
// ============================================================================

BOOST_AUTO_TEST_CASE(read_http_request_line_get)
{
    istringstream ss("GET / HTTP/1.1\r\n");
    int proto = 0;
    string method, uri;

    BOOST_CHECK(ReadHTTPRequestLine(ss, proto, method, uri));
    BOOST_CHECK_EQUAL(method, "GET");
    BOOST_CHECK_EQUAL(uri, "/");
    BOOST_CHECK_EQUAL(proto, 1);
}

BOOST_AUTO_TEST_CASE(read_http_request_line_post)
{
    istringstream ss("POST / HTTP/1.0\r\n");
    int proto = 0;
    string method, uri;

    BOOST_CHECK(ReadHTTPRequestLine(ss, proto, method, uri));
    BOOST_CHECK_EQUAL(method, "POST");
    BOOST_CHECK_EQUAL(uri, "/");
    BOOST_CHECK_EQUAL(proto, 0);
}

BOOST_AUTO_TEST_CASE(read_http_request_line_malformed_single_word)
{
    istringstream ss("GARBAGE\r\n");
    int proto = 0;
    string method, uri;

    BOOST_CHECK(!ReadHTTPRequestLine(ss, proto, method, uri));
}

BOOST_AUTO_TEST_CASE(read_http_request_line_invalid_method)
{
    istringstream ss("DELETE / HTTP/1.1\r\n");
    int proto = 0;
    string method, uri;

    BOOST_CHECK(!ReadHTTPRequestLine(ss, proto, method, uri));
}

BOOST_AUTO_TEST_CASE(read_http_request_line_no_version)
{
    // Two words: method + URI, but no HTTP version string.
    // Note: getline doesn't strip \r, so we use \n only.
    istringstream ss("GET /\n");
    int proto = 0;
    string method, uri;

    BOOST_CHECK(ReadHTTPRequestLine(ss, proto, method, uri));
    BOOST_CHECK_EQUAL(method, "GET");
    BOOST_CHECK_EQUAL(uri, "/");
    BOOST_CHECK_EQUAL(proto, 0);  // no version → proto = 0
}

BOOST_AUTO_TEST_CASE(read_http_request_line_relative_uri_rejected)
{
    // URI not starting with '/' should be rejected
    istringstream ss("GET relative HTTP/1.1\r\n");
    int proto = 0;
    string method, uri;

    BOOST_CHECK(!ReadHTTPRequestLine(ss, proto, method, uri));
}

// ============================================================================
// ReadHTTPHeaders tests
// ============================================================================

BOOST_AUTO_TEST_CASE(read_http_headers_content_length)
{
    istringstream ss("Content-Length: 42\r\n\r\n");
    map<string, string> headers;

    int nLen = ReadHTTPHeaders(ss, headers);
    BOOST_CHECK_EQUAL(nLen, 42);
    BOOST_CHECK_EQUAL(headers["content-length"], "42");
}

BOOST_AUTO_TEST_CASE(read_http_headers_multiple)
{
    istringstream ss("Content-Type: application/json\r\nContent-Length: 100\r\nConnection: keep-alive\r\n\r\n");
    map<string, string> headers;

    int nLen = ReadHTTPHeaders(ss, headers);
    BOOST_CHECK_EQUAL(nLen, 100);
    BOOST_CHECK_EQUAL(headers["content-type"], "application/json");
    BOOST_CHECK_EQUAL(headers["connection"], "keep-alive");
}

BOOST_AUTO_TEST_CASE(read_http_headers_empty)
{
    istringstream ss("\r\n");
    map<string, string> headers;

    int nLen = ReadHTTPHeaders(ss, headers);
    BOOST_CHECK_EQUAL(nLen, 0);
    BOOST_CHECK(headers.empty());
}

// ============================================================================
// ReadHTTPMessage tests
// ============================================================================

BOOST_AUTO_TEST_CASE(read_http_message_basic)
{
    string raw = "Content-Length: 13\r\n\r\nHello, world!";
    istringstream ss(raw);
    map<string, string> headers;
    string body;

    int status = ReadHTTPMessage(ss, headers, body, 1);
    BOOST_CHECK_EQUAL(status, HTTP_OK);
    BOOST_CHECK_EQUAL(body, "Hello, world!");
    BOOST_CHECK_EQUAL(headers["content-length"], "13");
}

BOOST_AUTO_TEST_CASE(read_http_message_empty_body)
{
    string raw = "Content-Length: 0\r\n\r\n";
    istringstream ss(raw);
    map<string, string> headers;
    string body;

    int status = ReadHTTPMessage(ss, headers, body, 1);
    BOOST_CHECK_EQUAL(status, HTTP_OK);
    BOOST_CHECK(body.empty());
}

BOOST_AUTO_TEST_CASE(read_http_message_connection_keepalive_http11)
{
    // HTTP/1.1 (proto=1) with no Connection header → defaults to "keep-alive"
    string raw = "Content-Length: 0\r\n\r\n";
    istringstream ss(raw);
    map<string, string> headers;
    string body;

    int status = ReadHTTPMessage(ss, headers, body, 1);
    BOOST_CHECK_EQUAL(status, HTTP_OK);
    BOOST_CHECK_EQUAL(headers["connection"], "keep-alive");
}

BOOST_AUTO_TEST_CASE(read_http_message_connection_close_http10)
{
    // HTTP/1.0 (proto=0) with no Connection header → defaults to "close"
    string raw = "Content-Length: 0\r\n\r\n";
    istringstream ss(raw);
    map<string, string> headers;
    string body;

    int status = ReadHTTPMessage(ss, headers, body, 0);
    BOOST_CHECK_EQUAL(status, HTTP_OK);
    BOOST_CHECK_EQUAL(headers["connection"], "close");
}

// ============================================================================
// ClientAllowed tests
// ============================================================================

BOOST_AUTO_TEST_CASE(client_allowed_loopback_v4)
{
    auto addr = asio::ip::make_address("127.0.0.1");
    BOOST_CHECK(ClientAllowed(addr));
}

BOOST_AUTO_TEST_CASE(client_allowed_loopback_v4_subnet)
{
    // 127.x.x.x is all loopback
    auto addr = asio::ip::make_address("127.0.0.2");
    BOOST_CHECK(ClientAllowed(addr));
}

BOOST_AUTO_TEST_CASE(client_allowed_loopback_v6)
{
    auto addr = asio::ip::make_address("::1");
    BOOST_CHECK(ClientAllowed(addr));
}

BOOST_AUTO_TEST_CASE(client_allowed_external_denied)
{
    // Clear any -rpcallowip settings
    vector<string> saved = mapMultiArgs["-rpcallowip"];
    mapMultiArgs["-rpcallowip"].clear();

    auto addr = asio::ip::make_address("192.168.1.100");
    BOOST_CHECK(!ClientAllowed(addr));

    mapMultiArgs["-rpcallowip"] = saved;
}

BOOST_AUTO_TEST_CASE(client_allowed_configured_wildcard)
{
    vector<string> saved = mapMultiArgs["-rpcallowip"];
    mapMultiArgs["-rpcallowip"].clear();
    mapMultiArgs["-rpcallowip"].push_back("10.0.0.*");

    auto addr = asio::ip::make_address("10.0.0.5");
    BOOST_CHECK(ClientAllowed(addr));

    auto addr2 = asio::ip::make_address("10.0.1.5");
    BOOST_CHECK(!ClientAllowed(addr2));

    mapMultiArgs["-rpcallowip"] = saved;
}

BOOST_AUTO_TEST_SUITE_END()
