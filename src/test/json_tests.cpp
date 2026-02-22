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

BOOST_AUTO_TEST_SUITE_END()
