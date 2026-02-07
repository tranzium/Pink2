// Copyright (c) 2024 The Pinkcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <boost/test/unit_test.hpp>
#include "string_utils.h"

BOOST_AUTO_TEST_SUITE(string_utils_tests)

// ============================================================================
// trim tests
// ============================================================================

BOOST_AUTO_TEST_CASE(trim_whitespace)
{
    std::string s1 = "  hello  ";
    BOOST_CHECK_EQUAL(strutil::trim(s1), "hello");
    BOOST_CHECK_EQUAL(s1, "hello"); // in-place modification

    std::string s2 = "\t\n hello world \r\n";
    BOOST_CHECK_EQUAL(strutil::trim(s2), "hello world");

    std::string s3 = "no_whitespace";
    BOOST_CHECK_EQUAL(strutil::trim(s3), "no_whitespace");

    std::string s4 = "";
    BOOST_CHECK_EQUAL(strutil::trim(s4), "");

    std::string s5 = "   ";
    BOOST_CHECK_EQUAL(strutil::trim(s5), "");
}

BOOST_AUTO_TEST_CASE(trim_copy_test)
{
    const std::string original = "  hello  ";
    std::string result = strutil::trim_copy(original);
    BOOST_CHECK_EQUAL(result, "hello");
    BOOST_CHECK_EQUAL(original, "  hello  "); // original unchanged
}

BOOST_AUTO_TEST_CASE(ltrim_test)
{
    std::string s = "  hello  ";
    BOOST_CHECK_EQUAL(strutil::ltrim(s), "hello  ");
}

BOOST_AUTO_TEST_CASE(rtrim_test)
{
    std::string s = "  hello  ";
    BOOST_CHECK_EQUAL(strutil::rtrim(s), "  hello");
}

// ============================================================================
// to_lower tests
// ============================================================================

BOOST_AUTO_TEST_CASE(to_lower_basic)
{
    std::string s1 = "HELLO";
    BOOST_CHECK_EQUAL(strutil::to_lower(s1), "hello");
    BOOST_CHECK_EQUAL(s1, "hello"); // in-place modification

    std::string s2 = "Hello World";
    BOOST_CHECK_EQUAL(strutil::to_lower(s2), "hello world");

    std::string s3 = "already lowercase";
    BOOST_CHECK_EQUAL(strutil::to_lower(s3), "already lowercase");

    std::string s4 = "MiXeD123CaSe!@#";
    BOOST_CHECK_EQUAL(strutil::to_lower(s4), "mixed123case!@#");
}

BOOST_AUTO_TEST_CASE(to_lower_copy_test)
{
    const std::string original = "HELLO";
    std::string result = strutil::to_lower_copy(original);
    BOOST_CHECK_EQUAL(result, "hello");
    BOOST_CHECK_EQUAL(original, "HELLO"); // original unchanged
}

// ============================================================================
// split tests
// ============================================================================

BOOST_AUTO_TEST_CASE(split_single_delimiter)
{
    std::vector<std::string> result = strutil::split("a,b,c", ",");
    BOOST_CHECK_EQUAL(result.size(), 3u);
    BOOST_CHECK_EQUAL(result[0], "a");
    BOOST_CHECK_EQUAL(result[1], "b");
    BOOST_CHECK_EQUAL(result[2], "c");
}

BOOST_AUTO_TEST_CASE(split_space_delimiter)
{
    std::vector<std::string> result = strutil::split("GET /index.html HTTP/1.1", " ");
    BOOST_CHECK_EQUAL(result.size(), 3u);
    BOOST_CHECK_EQUAL(result[0], "GET");
    BOOST_CHECK_EQUAL(result[1], "/index.html");
    BOOST_CHECK_EQUAL(result[2], "HTTP/1.1");
}

BOOST_AUTO_TEST_CASE(split_multiple_delimiters)
{
    std::vector<std::string> result = strutil::split("a b\tc", " \t");
    BOOST_CHECK_EQUAL(result.size(), 3u);
    BOOST_CHECK_EQUAL(result[0], "a");
    BOOST_CHECK_EQUAL(result[1], "b");
    BOOST_CHECK_EQUAL(result[2], "c");
}

BOOST_AUTO_TEST_CASE(split_empty_parts)
{
    std::vector<std::string> result = strutil::split("a,,b", ",");
    BOOST_CHECK_EQUAL(result.size(), 3u);
    BOOST_CHECK_EQUAL(result[0], "a");
    BOOST_CHECK_EQUAL(result[1], "");
    BOOST_CHECK_EQUAL(result[2], "b");
}

BOOST_AUTO_TEST_CASE(split_no_delimiter_found)
{
    std::vector<std::string> result = strutil::split("hello", ",");
    BOOST_CHECK_EQUAL(result.size(), 1u);
    BOOST_CHECK_EQUAL(result[0], "hello");
}

BOOST_AUTO_TEST_CASE(split_empty_string)
{
    std::vector<std::string> result = strutil::split("", ",");
    BOOST_CHECK_EQUAL(result.size(), 1u);
    BOOST_CHECK_EQUAL(result[0], "");
}

BOOST_AUTO_TEST_CASE(split_char_overload)
{
    std::vector<std::string> result = strutil::split("a:b:c", ':');
    BOOST_CHECK_EQUAL(result.size(), 3u);
    BOOST_CHECK_EQUAL(result[0], "a");
    BOOST_CHECK_EQUAL(result[1], "b");
    BOOST_CHECK_EQUAL(result[2], "c");
}

// ============================================================================
// replace_all tests
// ============================================================================

BOOST_AUTO_TEST_CASE(replace_all_basic)
{
    std::string s = "hello world world";
    strutil::replace_all(s, "world", "there");
    BOOST_CHECK_EQUAL(s, "hello there there");
}

BOOST_AUTO_TEST_CASE(replace_all_no_match)
{
    std::string s = "hello world";
    strutil::replace_all(s, "foo", "bar");
    BOOST_CHECK_EQUAL(s, "hello world");
}

BOOST_AUTO_TEST_CASE(replace_all_empty_from)
{
    std::string s = "hello";
    strutil::replace_all(s, "", "x");
    BOOST_CHECK_EQUAL(s, "hello"); // no change when 'from' is empty
}

BOOST_AUTO_TEST_CASE(replace_all_to_shorter)
{
    std::string s = "aaa";
    strutil::replace_all(s, "aa", "b");
    BOOST_CHECK_EQUAL(s, "ba");
}

BOOST_AUTO_TEST_CASE(replace_all_to_longer)
{
    std::string s = "ab";
    strutil::replace_all(s, "b", "ccc");
    BOOST_CHECK_EQUAL(s, "accc");
}

BOOST_AUTO_TEST_CASE(replace_all_command_substitution)
{
    // Real use case: command string substitution
    std::string cmd = "/usr/bin/notify %s";
    strutil::replace_all(cmd, "%s", "0x1234abcd");
    BOOST_CHECK_EQUAL(cmd, "/usr/bin/notify 0x1234abcd");
}

// ============================================================================
// starts_with tests
// ============================================================================

BOOST_AUTO_TEST_CASE(starts_with_basic)
{
    BOOST_CHECK(strutil::starts_with("hello world", "hello"));
    BOOST_CHECK(strutil::starts_with("hello", "hello"));
    BOOST_CHECK(strutil::starts_with("hello", ""));
    BOOST_CHECK(!strutil::starts_with("hello", "world"));
    BOOST_CHECK(!strutil::starts_with("hello", "Hello")); // case sensitive
    BOOST_CHECK(!strutil::starts_with("hi", "hello")); // prefix longer than string
}

BOOST_AUTO_TEST_CASE(starts_with_special_chars)
{
    BOOST_CHECK(strutil::starts_with("/path/to/file", "/"));
    BOOST_CHECK(strutil::starts_with("#comment", "#"));
    BOOST_CHECK(strutil::starts_with("[section]", "["));
    BOOST_CHECK(strutil::starts_with("label=value", "label="));
}

// ============================================================================
// ends_with tests
// ============================================================================

BOOST_AUTO_TEST_CASE(ends_with_basic)
{
    BOOST_CHECK(strutil::ends_with("hello world", "world"));
    BOOST_CHECK(strutil::ends_with("hello", "hello"));
    BOOST_CHECK(strutil::ends_with("hello", ""));
    BOOST_CHECK(!strutil::ends_with("hello", "Hello")); // case sensitive
    BOOST_CHECK(!strutil::ends_with("hi", "hello")); // suffix longer than string
}

BOOST_AUTO_TEST_CASE(ends_with_file_extensions)
{
    BOOST_CHECK(strutil::ends_with("file.dat", ".dat"));
    BOOST_CHECK(strutil::ends_with("12345_wl.dat", "_wl.dat"));
    BOOST_CHECK(!strutil::ends_with("file.txt", ".dat"));
}

// ============================================================================
// istarts_with tests (case-insensitive)
// ============================================================================

BOOST_AUTO_TEST_CASE(istarts_with_basic)
{
    BOOST_CHECK(strutil::istarts_with("Hello World", "hello"));
    BOOST_CHECK(strutil::istarts_with("HELLO", "hello"));
    BOOST_CHECK(strutil::istarts_with("hello", "HELLO"));
    BOOST_CHECK(strutil::istarts_with("Pinkcoin:uri", "pinkcoin:"));
    BOOST_CHECK(strutil::istarts_with("PINKCOIN:uri", "pinkcoin:"));
    BOOST_CHECK(!strutil::istarts_with("hello", "world"));
    BOOST_CHECK(!strutil::istarts_with("hi", "hello"));
}

// ============================================================================
// iends_with tests (case-insensitive)
// ============================================================================

BOOST_AUTO_TEST_CASE(iends_with_basic)
{
    BOOST_CHECK(strutil::iends_with("file.DAT", ".dat"));
    BOOST_CHECK(strutil::iends_with("file.dat", ".DAT"));
    BOOST_CHECK(!strutil::iends_with("file.txt", ".dat"));
}

// ============================================================================
// join tests
// ============================================================================

BOOST_AUTO_TEST_CASE(join_basic)
{
    std::vector<std::string> v = {"a", "b", "c"};
    BOOST_CHECK_EQUAL(strutil::join(v, ", "), "a, b, c");
    BOOST_CHECK_EQUAL(strutil::join(v, ""), "abc");
    BOOST_CHECK_EQUAL(strutil::join(v, "-"), "a-b-c");
}

BOOST_AUTO_TEST_CASE(join_single_element)
{
    std::vector<std::string> v = {"only"};
    BOOST_CHECK_EQUAL(strutil::join(v, ", "), "only");
}

BOOST_AUTO_TEST_CASE(join_empty_vector)
{
    std::vector<std::string> v;
    BOOST_CHECK_EQUAL(strutil::join(v, ", "), "");
}

BOOST_AUTO_TEST_CASE(join_with_empty_strings)
{
    std::vector<std::string> v = {"a", "", "c"};
    BOOST_CHECK_EQUAL(strutil::join(v, ";"), "a;;c");
}

// ============================================================================
// split_compress tests
// ============================================================================

BOOST_AUTO_TEST_CASE(split_compress_basic)
{
    std::vector<std::string> result = strutil::split_compress("a  b   c", " ");
    BOOST_CHECK_EQUAL(result.size(), 3u);
    BOOST_CHECK_EQUAL(result[0], "a");
    BOOST_CHECK_EQUAL(result[1], "b");
    BOOST_CHECK_EQUAL(result[2], "c");
}

BOOST_AUTO_TEST_CASE(split_compress_whitespace)
{
    std::vector<std::string> result = strutil::split_compress("  hello   world  ", " \t\n");
    BOOST_CHECK_EQUAL(result.size(), 2u);
    BOOST_CHECK_EQUAL(result[0], "hello");
    BOOST_CHECK_EQUAL(result[1], "world");
}

BOOST_AUTO_TEST_CASE(split_compress_empty_string)
{
    std::vector<std::string> result = strutil::split_compress("", " ");
    BOOST_CHECK(result.empty());
}

BOOST_AUTO_TEST_CASE(split_compress_only_delimiters)
{
    std::vector<std::string> result = strutil::split_compress("   ", " ");
    BOOST_CHECK(result.empty());
}

BOOST_AUTO_TEST_CASE(split_compress_mixed_delimiters)
{
    std::vector<std::string> result = strutil::split_compress("a \t\n b", " \t\n");
    BOOST_CHECK_EQUAL(result.size(), 2u);
    BOOST_CHECK_EQUAL(result[0], "a");
    BOOST_CHECK_EQUAL(result[1], "b");
}

// ============================================================================
// replace_first tests
// ============================================================================

BOOST_AUTO_TEST_CASE(replace_first_basic)
{
    std::string s = "hello world world";
    strutil::replace_first(s, "world", "there");
    BOOST_CHECK_EQUAL(s, "hello there world");
}

BOOST_AUTO_TEST_CASE(replace_first_no_match)
{
    std::string s = "hello world";
    strutil::replace_first(s, "foo", "bar");
    BOOST_CHECK_EQUAL(s, "hello world");
}

BOOST_AUTO_TEST_CASE(replace_first_at_start)
{
    std::string s = "OP_ADD";
    strutil::replace_first(s, "OP_", "");
    BOOST_CHECK_EQUAL(s, "ADD");
}

BOOST_AUTO_TEST_CASE(replace_first_empty_from)
{
    std::string s = "hello";
    strutil::replace_first(s, "", "x");
    BOOST_CHECK_EQUAL(s, "hello");
}

// ============================================================================
// parse_config_file tests
// ============================================================================

BOOST_AUTO_TEST_CASE(parse_config_file_basic)
{
    std::istringstream config(
        "# This is a comment\n"
        "key1=value1\n"
        "key2=value2\n"
        "\n"
        "# Another comment\n"
        "key3=value with spaces\n"
    );

    std::map<std::string, std::string> result;
    strutil::parse_config_file(config, [&](const std::string& key, const std::string& value) {
        result[key] = value;
    });

    BOOST_CHECK_EQUAL(result.size(), 3u);
    BOOST_CHECK_EQUAL(result["key1"], "value1");
    BOOST_CHECK_EQUAL(result["key2"], "value2");
    BOOST_CHECK_EQUAL(result["key3"], "value with spaces");
}

BOOST_AUTO_TEST_CASE(parse_config_file_whitespace)
{
    std::istringstream config(
        "  key1  =  value1  \n"
        "key2=value2\n"
        "  # indented comment\n"
    );

    std::map<std::string, std::string> result;
    strutil::parse_config_file(config, [&](const std::string& key, const std::string& value) {
        result[key] = value;
    });

    BOOST_CHECK_EQUAL(result.size(), 2u);
    BOOST_CHECK_EQUAL(result["key1"], "value1");
    BOOST_CHECK_EQUAL(result["key2"], "value2");
}

BOOST_AUTO_TEST_CASE(parse_config_file_multivalue)
{
    std::istringstream config(
        "multi=value1\n"
        "multi=value2\n"
        "multi=value3\n"
    );

    std::vector<std::string> values;
    strutil::parse_config_file(config, [&](const std::string& key, const std::string& value) {
        if (key == "multi")
            values.push_back(value);
    });

    BOOST_CHECK_EQUAL(values.size(), 3u);
    BOOST_CHECK_EQUAL(values[0], "value1");
    BOOST_CHECK_EQUAL(values[1], "value2");
    BOOST_CHECK_EQUAL(values[2], "value3");
}

BOOST_AUTO_TEST_CASE(parse_config_file_empty_value)
{
    std::istringstream config(
        "empty=\n"
        "notempty=value\n"
    );

    std::map<std::string, std::string> result;
    strutil::parse_config_file(config, [&](const std::string& key, const std::string& value) {
        result[key] = value;
    });

    BOOST_CHECK_EQUAL(result.size(), 2u);
    BOOST_CHECK_EQUAL(result["empty"], "");
    BOOST_CHECK_EQUAL(result["notempty"], "value");
}

BOOST_AUTO_TEST_CASE(parse_config_file_equals_in_value)
{
    std::istringstream config(
        "url=http://example.com?foo=bar\n"
    );

    std::map<std::string, std::string> result;
    strutil::parse_config_file(config, [&](const std::string& key, const std::string& value) {
        result[key] = value;
    });

    BOOST_CHECK_EQUAL(result.size(), 1u);
    BOOST_CHECK_EQUAL(result["url"], "http://example.com?foo=bar");
}

BOOST_AUTO_TEST_SUITE_END()
