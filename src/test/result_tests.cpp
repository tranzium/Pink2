// Copyright (c) 2024 The Pinkcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <boost/test/unit_test.hpp>

#include "result.h"

#include <string>
#include <vector>

BOOST_AUTO_TEST_SUITE(result_tests)

// ============================================================================
// Result<T> — success paths
// ============================================================================

BOOST_AUTO_TEST_CASE(result_int_success)
{
    Result<int> r(42);
    BOOST_CHECK(r.ok());
    BOOST_CHECK(static_cast<bool>(r));
    BOOST_CHECK_EQUAL(r.value(), 42);
}

BOOST_AUTO_TEST_CASE(result_string_success)
{
    Result<std::string> r(std::string("hello"));
    BOOST_CHECK(r.ok());
    BOOST_CHECK_EQUAL(r.value(), "hello");
}

BOOST_AUTO_TEST_CASE(result_vector_success)
{
    std::vector<int> v = {1, 2, 3};
    Result<std::vector<int>> r(v);
    BOOST_CHECK(r.ok());
    BOOST_CHECK_EQUAL(r.value().size(), 3u);
    BOOST_CHECK_EQUAL(r.value()[0], 1);
}

BOOST_AUTO_TEST_CASE(result_value_or_on_success)
{
    Result<int> r(42);
    BOOST_CHECK_EQUAL(r.value_or(0), 42);
}

// ============================================================================
// Result<T> — error paths
// ============================================================================

BOOST_AUTO_TEST_CASE(result_int_error)
{
    auto r = Result<int>::Error("something went wrong");
    BOOST_CHECK(!r.ok());
    BOOST_CHECK(!static_cast<bool>(r));
    BOOST_CHECK_EQUAL(r.error(), "something went wrong");
}

BOOST_AUTO_TEST_CASE(result_value_throws_on_error)
{
    auto r = Result<int>::Error("bad");
    BOOST_CHECK_THROW(r.value(), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(result_error_throws_on_success)
{
    Result<int> r(42);
    BOOST_CHECK_THROW(r.error(), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(result_value_or_on_error)
{
    auto r = Result<int>::Error("fail");
    BOOST_CHECK_EQUAL(r.value_or(99), 99);
}

// ============================================================================
// Result<void> — success/failure without a value
// ============================================================================

BOOST_AUTO_TEST_CASE(result_void_success)
{
    Result<void> r = Result<void>::Success();
    BOOST_CHECK(r.ok());
    BOOST_CHECK(static_cast<bool>(r));
}

BOOST_AUTO_TEST_CASE(result_void_default_is_success)
{
    Result<void> r;
    BOOST_CHECK(r.ok());
}

BOOST_AUTO_TEST_CASE(result_void_error)
{
    auto r = Result<void>::Error("operation failed");
    BOOST_CHECK(!r.ok());
    BOOST_CHECK(!static_cast<bool>(r));
    BOOST_CHECK_EQUAL(r.error(), "operation failed");
}

BOOST_AUTO_TEST_CASE(result_void_error_throws_on_success)
{
    Result<void> r;
    BOOST_CHECK_THROW(r.error(), std::runtime_error);
}

// ============================================================================
// Result<T> — mutable value access
// ============================================================================

BOOST_AUTO_TEST_CASE(result_mutable_value)
{
    Result<int> r(10);
    r.value() = 20;
    BOOST_CHECK_EQUAL(r.value(), 20);
}

BOOST_AUTO_TEST_SUITE_END()
