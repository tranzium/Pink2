// Copyright (c) 2024 The Pinkcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
//
// Unit tests for rpcdump.cpp — dump time/string encoding and decoding.

#include <boost/test/unit_test.hpp>

#include <string>

// Extern declarations for functions in rpcdump.cpp
extern int64_t DecodeDumpTime(const std::string& s);
extern std::string DecodeDumpString(const std::string& str);
extern std::string EncodeDumpTime(int64_t nTime);
extern std::string EncodeDumpString(const std::string& str);

BOOST_AUTO_TEST_SUITE(rpcdump_tests)

// ============================================================================
// DecodeDumpTime tests
// ============================================================================

BOOST_AUTO_TEST_CASE(decode_dump_time_iso8601)
{
    // "2024-01-15T12:30:45Z" → known epoch
    int64_t t = DecodeDumpTime("2024-01-15T12:30:45Z");
    BOOST_CHECK(t > 0);
    // Jan 15, 2024 12:30:45 UTC = 1705321845
    BOOST_CHECK_EQUAL(t, 1705321845);
}

BOOST_AUTO_TEST_CASE(decode_dump_time_space)
{
    // "2024-01-15 12:30:45" format
    int64_t t = DecodeDumpTime("2024-01-15 12:30:45");
    BOOST_CHECK(t > 0);
    BOOST_CHECK_EQUAL(t, 1705321845);
}

BOOST_AUTO_TEST_CASE(decode_dump_time_slash)
{
    // "2024/01/15 12:30:45" format
    int64_t t = DecodeDumpTime("2024/01/15 12:30:45");
    BOOST_CHECK(t > 0);
    BOOST_CHECK_EQUAL(t, 1705321845);
}

BOOST_AUTO_TEST_CASE(decode_dump_time_dot)
{
    // "15.01.2024 12:30:45" format (dd.mm.yyyy)
    int64_t t = DecodeDumpTime("15.01.2024 12:30:45");
    BOOST_CHECK(t > 0);
    BOOST_CHECK_EQUAL(t, 1705321845);
}

BOOST_AUTO_TEST_CASE(decode_dump_time_date_only)
{
    // "2024-01-15" — date only, time defaults to 00:00:00
    int64_t t = DecodeDumpTime("2024-01-15");
    BOOST_CHECK(t > 0);
    // Jan 15, 2024 00:00:00 UTC = 1705276800
    BOOST_CHECK_EQUAL(t, 1705276800);
}

BOOST_AUTO_TEST_CASE(decode_dump_time_invalid)
{
    // Invalid string → 0
    BOOST_CHECK_EQUAL(DecodeDumpTime("not_a_date"), 0);
    BOOST_CHECK_EQUAL(DecodeDumpTime(""), 0);
}

BOOST_AUTO_TEST_CASE(encode_decode_time_roundtrip)
{
    // EncodeDumpTime → DecodeDumpTime should round-trip
    int64_t original = 1705321845;
    std::string encoded = EncodeDumpTime(original);
    int64_t decoded = DecodeDumpTime(encoded);
    BOOST_CHECK_EQUAL(decoded, original);
}

// ============================================================================
// EncodeDumpString / DecodeDumpString tests
// ============================================================================

BOOST_AUTO_TEST_CASE(encode_dump_string_plain)
{
    // Plain ASCII — no encoding needed
    BOOST_CHECK_EQUAL(EncodeDumpString("hello"), "hello");
    BOOST_CHECK_EQUAL(EncodeDumpString("abc123"), "abc123");
}

BOOST_AUTO_TEST_CASE(encode_dump_string_special)
{
    // Space (char 32) should be encoded as %20
    std::string encoded = EncodeDumpString("hello world");
    BOOST_CHECK(encoded.find("%20") != std::string::npos);
    BOOST_CHECK(encoded != "hello world");

    // Percent sign should be encoded
    std::string enc2 = EncodeDumpString("100%");
    BOOST_CHECK(enc2.find("%25") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(encode_dump_string_control)
{
    // Control characters (< 32) should be encoded
    std::string input(1, '\n'); // newline = 0x0a
    std::string encoded = EncodeDumpString(input);
    BOOST_CHECK(encoded.find("%0a") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(decode_dump_string_roundtrip)
{
    // EncodeDumpString → DecodeDumpString round-trip
    std::string original = "hello world! 100% test\nnewline";
    std::string encoded = EncodeDumpString(original);
    std::string decoded = DecodeDumpString(encoded);
    BOOST_CHECK_EQUAL(decoded, original);
}

BOOST_AUTO_TEST_CASE(decode_dump_string_passthrough)
{
    // Plain string without encoding → passes through unchanged
    BOOST_CHECK_EQUAL(DecodeDumpString("hello"), "hello");
    BOOST_CHECK_EQUAL(DecodeDumpString("abc123"), "abc123");
}

BOOST_AUTO_TEST_SUITE_END()
