// Copyright (c) 2024-2026 The Pinkcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Phase 6D: Basic nlohmann/json integration tests.
// Verifies the vendored library compiles and works correctly.

#include <boost/test/unit_test.hpp>
#include "json/nlohmann/json.hpp"

using json = nlohmann::json;

BOOST_AUTO_TEST_SUITE(json_tests)

BOOST_AUTO_TEST_CASE(json_basic_types)
{
    json j;
    j["string"] = "hello";
    j["number"] = 42;
    j["float"] = 3.14;
    j["bool"] = true;
    j["null"] = nullptr;
    j["array"] = json::array({1, 2, 3});

    BOOST_CHECK(j["string"].is_string());
    BOOST_CHECK_EQUAL(j["string"].get<std::string>(), "hello");
    BOOST_CHECK(j["number"].is_number_integer());
    BOOST_CHECK_EQUAL(j["number"].get<int>(), 42);
    BOOST_CHECK(j["float"].is_number_float());
    BOOST_CHECK(j["bool"].is_boolean());
    BOOST_CHECK(j["null"].is_null());
    BOOST_CHECK(j["array"].is_array());
    BOOST_CHECK_EQUAL(j["array"].size(), 3u);
}

BOOST_AUTO_TEST_CASE(json_object_construction)
{
    json obj;
    obj["name"] = "pinkcoin";
    obj["version"] = 1;
    obj["active"] = true;

    BOOST_CHECK(obj.is_object());
    BOOST_CHECK_EQUAL(obj.size(), 3u);
    BOOST_CHECK(obj.contains("name"));
    BOOST_CHECK(!obj.contains("missing"));
}

BOOST_AUTO_TEST_CASE(json_array_construction)
{
    json arr = json::array();
    arr.push_back("item1");
    arr.push_back(42);
    arr.push_back(true);

    BOOST_CHECK(arr.is_array());
    BOOST_CHECK_EQUAL(arr.size(), 3u);
    BOOST_CHECK_EQUAL(arr[0].get<std::string>(), "item1");
    BOOST_CHECK_EQUAL(arr[1].get<int>(), 42);
}

BOOST_AUTO_TEST_CASE(json_parse_serialize)
{
    std::string input = R"({"key":"value","num":123})";
    json parsed = json::parse(input);

    BOOST_CHECK(parsed.is_object());
    BOOST_CHECK_EQUAL(parsed["key"].get<std::string>(), "value");
    BOOST_CHECK_EQUAL(parsed["num"].get<int>(), 123);

    std::string serialized = parsed.dump();
    json reparsed = json::parse(serialized);
    BOOST_CHECK(reparsed == parsed);
}

BOOST_AUTO_TEST_CASE(json_nested_access)
{
    json obj;
    obj["inner"]["deep"] = 42;

    BOOST_CHECK(obj["inner"].is_object());
    BOOST_CHECK_EQUAL(obj["inner"]["deep"].get<int>(), 42);
}

BOOST_AUTO_TEST_CASE(json_contains_and_value)
{
    json obj;
    obj["exists"] = "yes";

    BOOST_CHECK(obj.contains("exists"));
    BOOST_CHECK(!obj.contains("missing"));

    std::string val = obj.value("exists", std::string("default"));
    BOOST_CHECK_EQUAL(val, "yes");

    std::string def = obj.value("missing", std::string("default"));
    BOOST_CHECK_EQUAL(def, "default");
}

BOOST_AUTO_TEST_CASE(json_iteration)
{
    json arr = json::array({10, 20, 30});
    int sum = 0;
    for (const auto& v : arr)
        sum += v.get<int>();
    BOOST_CHECK_EQUAL(sum, 60);
}

BOOST_AUTO_TEST_CASE(json_type_checks)
{
    json s = "hello";
    json i = 42;
    json d = 3.14;
    json b = true;
    json n = nullptr;
    json a = json::array();
    json o = json::object();

    BOOST_CHECK(s.is_string());
    BOOST_CHECK(i.is_number_integer());
    BOOST_CHECK(d.is_number_float());
    BOOST_CHECK(b.is_boolean());
    BOOST_CHECK(n.is_null());
    BOOST_CHECK(a.is_array());
    BOOST_CHECK(o.is_object());
}

BOOST_AUTO_TEST_CASE(json_file_parse)
{
    // Verify we can parse from a string (same pattern as read_json)
    std::string jsonStr = R"([{"name":"test","value":1},{"name":"test2","value":2}])";
    json parsed = json::parse(jsonStr);

    BOOST_CHECK(parsed.is_array());
    BOOST_CHECK_EQUAL(parsed.size(), 2u);
    BOOST_CHECK_EQUAL(parsed[0]["name"].get<std::string>(), "test");
    BOOST_CHECK_EQUAL(parsed[1]["value"].get<int>(), 2);
}

BOOST_AUTO_TEST_CASE(json_int64_support)
{
    json j;
    j["max"] = INT64_MAX;
    j["min"] = INT64_MIN;

    BOOST_CHECK_EQUAL(j["max"].get<int64_t>(), INT64_MAX);
    BOOST_CHECK_EQUAL(j["min"].get<int64_t>(), INT64_MIN);
}

// ============================================================================
// Phase 6D Migration Correctness Tests
// Verify nlohmann/json matches expected RPC behavior patterns
// ============================================================================

BOOST_AUTO_TEST_CASE(json_rpc_response_pattern)
{
    // Verify the standard RPC response object pattern
    json result;
    result["version"] = "2.0.0.1";
    result["protocolversion"] = 60017;
    result["walletversion"] = 60000;
    result["balance"] = 1000.5;
    result["newmint"] = 0.0;
    result["stake"] = 0.0;
    result["blocks"] = 100;
    result["timeoffset"] = 0;
    result["connections"] = 8;
    result["errors"] = "";

    // Verify all fields serialize and round-trip
    std::string serialized = result.dump();
    json parsed = json::parse(serialized);
    BOOST_CHECK(parsed == result);

    // Verify field access patterns used throughout RPC code
    BOOST_CHECK(parsed["version"].is_string());
    BOOST_CHECK(parsed["protocolversion"].is_number_integer());
    BOOST_CHECK(parsed["balance"].is_number_float());
    BOOST_CHECK(parsed["errors"].is_string());
}

BOOST_AUTO_TEST_CASE(json_block_response_pattern)
{
    // Verify getblock response structure
    json block;
    block["hash"] = "00000000000000001";
    block["confirmations"] = 100;
    block["size"] = 285;
    block["height"] = 1;
    block["version"] = 1;
    block["merkleroot"] = "abc123";
    block["time"] = 1231006505;
    block["nonce"] = 2083236893;
    block["bits"] = "1d00ffff";
    block["difficulty"] = 1.0;
    block["tx"] = json::array({"tx1hash", "tx2hash"});

    std::string s = block.dump();
    json parsed = json::parse(s);
    BOOST_CHECK(parsed["tx"].is_array());
    BOOST_CHECK_EQUAL(parsed["tx"].size(), 2u);
    BOOST_CHECK_EQUAL(parsed["tx"][0].get<std::string>(), "tx1hash");
}

BOOST_AUTO_TEST_CASE(json_array_of_objects_pattern)
{
    // Pattern used by listtransactions, listunspent, etc.
    json arr = json::array();
    for (int i = 0; i < 3; i++) {
        json entry;
        entry["txid"] = "hash" + std::to_string(i);
        entry["amount"] = static_cast<double>(i) * 1.5;
        entry["confirmations"] = i * 10;
        arr.push_back(entry);
    }

    std::string s = arr.dump();
    json parsed = json::parse(s);
    BOOST_CHECK(parsed.is_array());
    BOOST_CHECK_EQUAL(parsed.size(), 3u);
    BOOST_CHECK_EQUAL(parsed[2]["txid"].get<std::string>(), "hash2");
}

BOOST_AUTO_TEST_CASE(json_nested_objects_pattern)
{
    // Pattern used by validateaddress, getstakinginfo, etc.
    json result;
    result["isvalid"] = true;
    result["address"] = "2abc123";

    json scriptPubKey;
    scriptPubKey["asm"] = "OP_DUP OP_HASH160 abc OP_EQUALVERIFY OP_CHECKSIG";
    scriptPubKey["hex"] = "76a914abc88ac";
    scriptPubKey["reqSigs"] = 1;
    scriptPubKey["type"] = "pubkeyhash";
    scriptPubKey["addresses"] = json::array({"2abc123"});
    result["scriptPubKey"] = scriptPubKey;

    std::string s = result.dump();
    json parsed = json::parse(s);
    BOOST_CHECK(parsed["scriptPubKey"].is_object());
    BOOST_CHECK_EQUAL(parsed["scriptPubKey"]["type"].get<std::string>(), "pubkeyhash");
    BOOST_CHECK(parsed["scriptPubKey"]["addresses"].is_array());
}

BOOST_AUTO_TEST_CASE(json_error_envelope)
{
    // JSONRPCError format: {"code": int, "message": string}
    json err;
    err["code"] = -32601;
    err["message"] = "Method not found";

    BOOST_CHECK(err["code"].is_number_integer());
    BOOST_CHECK_EQUAL(err["code"].get<int>(), -32601);
    BOOST_CHECK_EQUAL(err["message"].get<std::string>(), "Method not found");

    // Round-trip
    json parsed = json::parse(err.dump());
    BOOST_CHECK_EQUAL(parsed["code"].get<int>(), -32601);
}

BOOST_AUTO_TEST_CASE(json_roundtrip_empty)
{
    // Empty object
    json emptyObj = json::object();
    BOOST_CHECK_EQUAL(json::parse(emptyObj.dump()), emptyObj);

    // Empty array
    json emptyArr = json::array();
    BOOST_CHECK_EQUAL(json::parse(emptyArr.dump()), emptyArr);

    // Null
    json n = nullptr;
    BOOST_CHECK(json::parse(n.dump()).is_null());
}

BOOST_AUTO_TEST_CASE(json_large_integers)
{
    json j;
    j["max_i64"] = INT64_MAX;
    j["min_i64"] = INT64_MIN;
    j["max_u64"] = UINT64_MAX;
    j["zero"] = 0;

    json parsed = json::parse(j.dump());
    BOOST_CHECK_EQUAL(parsed["max_i64"].get<int64_t>(), INT64_MAX);
    BOOST_CHECK_EQUAL(parsed["min_i64"].get<int64_t>(), INT64_MIN);
    BOOST_CHECK_EQUAL(parsed["max_u64"].get<uint64_t>(), UINT64_MAX);
    BOOST_CHECK_EQUAL(parsed["zero"].get<int>(), 0);
}

BOOST_AUTO_TEST_CASE(json_unicode_strings)
{
    json j;
    j["ascii"] = "hello";
    j["utf8"] = "\xc3\xa9\xc3\xa0\xc3\xbc"; // e-acute, a-grave, u-umlaut
    j["emoji"] = "\xf0\x9f\x92\xb0"; // money bag emoji

    json parsed = json::parse(j.dump());
    BOOST_CHECK_EQUAL(parsed["utf8"].get<std::string>(), "\xc3\xa9\xc3\xa0\xc3\xbc");
    BOOST_CHECK_EQUAL(parsed["emoji"].get<std::string>(), "\xf0\x9f\x92\xb0");
}

BOOST_AUTO_TEST_CASE(json_null_in_objects)
{
    json obj;
    obj["present"] = "value";
    obj["absent"] = nullptr;

    BOOST_CHECK(!obj["present"].is_null());
    BOOST_CHECK(obj["absent"].is_null());
    BOOST_CHECK_EQUAL(obj.size(), 2u); // null IS a value

    // Non-existent key access creates null entry
    json accessed = obj["nonexistent"];
    BOOST_CHECK(accessed.is_null());
}

BOOST_AUTO_TEST_CASE(json_deeply_nested)
{
    json root;
    json* current = &root;
    for (int i = 0; i < 20; i++) {
        (*current)["level"] = i;
        (*current)["child"] = json::object();
        current = &(*current)["child"];
    }
    (*current)["level"] = 20;

    std::string s = root.dump();
    json parsed = json::parse(s);
    BOOST_CHECK_EQUAL(parsed["level"].get<int>(), 0);
    BOOST_CHECK_EQUAL(parsed["child"]["level"].get<int>(), 1);
}

BOOST_AUTO_TEST_CASE(json_pretty_vs_compact)
{
    json obj;
    obj["a"] = 1;
    obj["b"] = "two";

    std::string compact = obj.dump();
    std::string pretty = obj.dump(4);

    // Compact has no newlines
    BOOST_CHECK(compact.find('\n') == std::string::npos);
    // Pretty has newlines and indentation
    BOOST_CHECK(pretty.find('\n') != std::string::npos);
    BOOST_CHECK(pretty.find("    ") != std::string::npos);

    // Both parse to the same value
    BOOST_CHECK(json::parse(compact) == json::parse(pretty));
}

BOOST_AUTO_TEST_CASE(json_parse_error_handling)
{
    // Invalid JSON should throw parse_error
    BOOST_CHECK_THROW(json::parse("{invalid}"), json::parse_error);
    BOOST_CHECK_THROW(json::parse(""), json::parse_error);
    BOOST_CHECK_THROW(json::parse("[1,2,"), json::parse_error);

    // Non-throwing parse returns discarded
    json result = json::parse("{bad}", nullptr, false);
    BOOST_CHECK(result.is_discarded());
}

BOOST_AUTO_TEST_SUITE_END()
