// Copyright (c) 2026 The Pinkcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Phase 6C: Tests for structured logging framework.

#include <boost/test/unit_test.hpp>

#include "logging.h"
#include "util.h"
#include "bitcoinrpc.h"

#include <thread>
#include <vector>

using json_spirit::Array;
using json_spirit::Value;
using json_spirit::Object;
using json_spirit::obj_type;
using json_spirit::str_type;
using json_spirit::array_type;
using json_spirit::int_type;

// Helper to save/restore Logger state across tests
struct LoggerStateGuard {
    LogLevel savedLevel;
    uint32_t savedCats;

    LoggerStateGuard()
        : savedLevel(Logger::GetInstance().GetLogLevel()),
          savedCats(Logger::GetInstance().GetCategories())
    {}

    ~LoggerStateGuard()
    {
        Logger::GetInstance().SetLogLevel(savedLevel);
        Logger::GetInstance().SetCategories(savedCats);
    }
};

// ============================================================================
// Suite 1: Log level tests
// ============================================================================
BOOST_AUTO_TEST_SUITE(log_level_tests)

BOOST_AUTO_TEST_CASE(set_and_get_level)
{
    LoggerStateGuard guard;
    Logger& logger = Logger::GetInstance();

    logger.SetLogLevel(LogLevel::ERR);
    BOOST_CHECK(logger.GetLogLevel() == LogLevel::ERR);

    logger.SetLogLevel(LogLevel::DEBUG);
    BOOST_CHECK(logger.GetLogLevel() == LogLevel::DEBUG);

    logger.SetLogLevel(LogLevel::NONE);
    BOOST_CHECK(logger.GetLogLevel() == LogLevel::NONE);
}

BOOST_AUTO_TEST_CASE(will_log_respects_level)
{
    LoggerStateGuard guard;
    Logger& logger = Logger::GetInstance();

    logger.SetLogLevel(LogLevel::WARN);
    BOOST_CHECK(logger.WillLog(LogLevel::ERR));
    BOOST_CHECK(logger.WillLog(LogLevel::WARN));
    BOOST_CHECK(!logger.WillLog(LogLevel::INFO));
    BOOST_CHECK(!logger.WillLog(LogLevel::DEBUG));
}

BOOST_AUTO_TEST_CASE(debug_level_allows_all)
{
    LoggerStateGuard guard;
    Logger& logger = Logger::GetInstance();

    logger.SetLogLevel(LogLevel::DEBUG);
    BOOST_CHECK(logger.WillLog(LogLevel::ERR));
    BOOST_CHECK(logger.WillLog(LogLevel::WARN));
    BOOST_CHECK(logger.WillLog(LogLevel::INFO));
    BOOST_CHECK(logger.WillLog(LogLevel::DEBUG));
}

BOOST_AUTO_TEST_CASE(level_none_blocks_all)
{
    LoggerStateGuard guard;
    Logger& logger = Logger::GetInstance();

    logger.SetLogLevel(LogLevel::NONE);
    BOOST_CHECK(!logger.WillLog(LogLevel::ERR));
    BOOST_CHECK(!logger.WillLog(LogLevel::WARN));
    BOOST_CHECK(!logger.WillLog(LogLevel::INFO));
    BOOST_CHECK(!logger.WillLog(LogLevel::DEBUG));
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// Suite 2: Log category tests
// ============================================================================
BOOST_AUTO_TEST_SUITE(log_category_tests)

BOOST_AUTO_TEST_CASE(default_categories_from_init)
{
    // Just verify we can query categories (value depends on test init config)
    uint32_t cats = Logger::GetInstance().GetCategories();
    BOOST_CHECK(cats == cats); // no-op, just ensure no crash
}

BOOST_AUTO_TEST_CASE(enable_and_disable_category)
{
    LoggerStateGuard guard;
    Logger& logger = Logger::GetInstance();

    logger.SetCategories(0);
    BOOST_CHECK_EQUAL(logger.GetCategories(), 0u);

    logger.EnableCategory(BCLog::NET);
    BOOST_CHECK_EQUAL(logger.GetCategories(), static_cast<uint32_t>(BCLog::NET));

    logger.EnableCategory(BCLog::WALLET);
    BOOST_CHECK_EQUAL(logger.GetCategories(),
        static_cast<uint32_t>(BCLog::NET) | static_cast<uint32_t>(BCLog::WALLET));

    logger.DisableCategory(BCLog::NET);
    BOOST_CHECK_EQUAL(logger.GetCategories(), static_cast<uint32_t>(BCLog::WALLET));
}

BOOST_AUTO_TEST_CASE(will_log_checks_category)
{
    LoggerStateGuard guard;
    Logger& logger = Logger::GetInstance();

    logger.SetLogLevel(LogLevel::DEBUG);
    logger.SetCategories(BCLog::NET);

    BOOST_CHECK(logger.WillLog(LogLevel::DEBUG, BCLog::NET));
    BOOST_CHECK(!logger.WillLog(LogLevel::DEBUG, BCLog::WALLET));
    BOOST_CHECK(!logger.WillLog(LogLevel::DEBUG, BCLog::CONSENSUS));
}

BOOST_AUTO_TEST_CASE(category_level_interaction)
{
    LoggerStateGuard guard;
    Logger& logger = Logger::GetInstance();

    logger.SetLogLevel(LogLevel::WARN);
    logger.SetCategories(BCLog::NET);

    // Category enabled but level too low for DEBUG
    BOOST_CHECK(!logger.WillLog(LogLevel::DEBUG, BCLog::NET));

    // Category enabled and level sufficient for WARN
    BOOST_CHECK(logger.WillLog(LogLevel::WARN, BCLog::NET));
}

BOOST_AUTO_TEST_CASE(category_all_enables_everything)
{
    LoggerStateGuard guard;
    Logger& logger = Logger::GetInstance();

    logger.SetLogLevel(LogLevel::DEBUG);
    logger.SetCategories(BCLog::ALL);

    BOOST_CHECK(logger.WillLog(LogLevel::DEBUG, BCLog::NET));
    BOOST_CHECK(logger.WillLog(LogLevel::DEBUG, BCLog::WALLET));
    BOOST_CHECK(logger.WillLog(LogLevel::DEBUG, BCLog::CONSENSUS));
    BOOST_CHECK(logger.WillLog(LogLevel::DEBUG, BCLog::SMSG));
    BOOST_CHECK(logger.WillLog(LogLevel::DEBUG, BCLog::STAKE));
    BOOST_CHECK(logger.WillLog(LogLevel::DEBUG, BCLog::RPC));
    BOOST_CHECK(logger.WillLog(LogLevel::DEBUG, BCLog::MEMPOOL));
    BOOST_CHECK(logger.WillLog(LogLevel::DEBUG, BCLog::DB));
}

BOOST_AUTO_TEST_CASE(category_none_disables_all)
{
    LoggerStateGuard guard;
    Logger& logger = Logger::GetInstance();

    logger.SetLogLevel(LogLevel::DEBUG);
    logger.SetCategories(BCLog::NONE);

    BOOST_CHECK(!logger.WillLog(LogLevel::DEBUG, BCLog::NET));
    BOOST_CHECK(!logger.WillLog(LogLevel::DEBUG, BCLog::WALLET));
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// Suite 3: String conversion tests
// ============================================================================
BOOST_AUTO_TEST_SUITE(log_string_conversion_tests)

BOOST_AUTO_TEST_CASE(category_from_string_lowercase)
{
    BOOST_CHECK(Logger::CategoryFromString("net") == BCLog::NET);
    BOOST_CHECK(Logger::CategoryFromString("wallet") == BCLog::WALLET);
    BOOST_CHECK(Logger::CategoryFromString("stake") == BCLog::STAKE);
    BOOST_CHECK(Logger::CategoryFromString("rpc") == BCLog::RPC);
    BOOST_CHECK(Logger::CategoryFromString("consensus") == BCLog::CONSENSUS);
    BOOST_CHECK(Logger::CategoryFromString("smsg") == BCLog::SMSG);
    BOOST_CHECK(Logger::CategoryFromString("mempool") == BCLog::MEMPOOL);
    BOOST_CHECK(Logger::CategoryFromString("db") == BCLog::DB);
    BOOST_CHECK(Logger::CategoryFromString("all") == BCLog::ALL);
}

BOOST_AUTO_TEST_CASE(category_from_string_case_insensitive)
{
    BOOST_CHECK(Logger::CategoryFromString("NET") == BCLog::NET);
    BOOST_CHECK(Logger::CategoryFromString("Wallet") == BCLog::WALLET);
    BOOST_CHECK(Logger::CategoryFromString("ALL") == BCLog::ALL);
}

BOOST_AUTO_TEST_CASE(category_from_string_unknown)
{
    BOOST_CHECK(Logger::CategoryFromString("unknown") == BCLog::NONE);
    BOOST_CHECK(Logger::CategoryFromString("") == BCLog::NONE);
}

BOOST_AUTO_TEST_CASE(category_from_string_one_means_all)
{
    BOOST_CHECK(Logger::CategoryFromString("1") == BCLog::ALL);
}

BOOST_AUTO_TEST_CASE(category_to_string)
{
    BOOST_CHECK_EQUAL(Logger::CategoryToString(BCLog::NET), "net");
    BOOST_CHECK_EQUAL(Logger::CategoryToString(BCLog::WALLET), "wallet");
    BOOST_CHECK_EQUAL(Logger::CategoryToString(BCLog::STAKE), "stake");
    BOOST_CHECK_EQUAL(Logger::CategoryToString(BCLog::ALL), "all");
    BOOST_CHECK_EQUAL(Logger::CategoryToString(BCLog::NONE), "none");
}

BOOST_AUTO_TEST_CASE(level_from_string)
{
    BOOST_CHECK(Logger::LevelFromString("error") == LogLevel::ERR);
    BOOST_CHECK(Logger::LevelFromString("warn") == LogLevel::WARN);
    BOOST_CHECK(Logger::LevelFromString("info") == LogLevel::INFO);
    BOOST_CHECK(Logger::LevelFromString("debug") == LogLevel::DEBUG);
    BOOST_CHECK(Logger::LevelFromString("none") == LogLevel::NONE);
}

BOOST_AUTO_TEST_CASE(level_from_string_case_insensitive)
{
    BOOST_CHECK(Logger::LevelFromString("ERROR") == LogLevel::ERR);
    BOOST_CHECK(Logger::LevelFromString("Debug") == LogLevel::DEBUG);
}

BOOST_AUTO_TEST_CASE(level_from_string_unknown_defaults_to_info)
{
    BOOST_CHECK(Logger::LevelFromString("bogus") == LogLevel::INFO);
    BOOST_CHECK(Logger::LevelFromString("") == LogLevel::INFO);
}

BOOST_AUTO_TEST_CASE(level_to_string)
{
    BOOST_CHECK_EQUAL(Logger::LevelToString(LogLevel::NONE), "none");
    BOOST_CHECK_EQUAL(Logger::LevelToString(LogLevel::ERR), "error");
    BOOST_CHECK_EQUAL(Logger::LevelToString(LogLevel::WARN), "warn");
    BOOST_CHECK_EQUAL(Logger::LevelToString(LogLevel::INFO), "info");
    BOOST_CHECK_EQUAL(Logger::LevelToString(LogLevel::DEBUG), "debug");
}

BOOST_AUTO_TEST_CASE(level_roundtrip)
{
    for (int i = 0; i <= 4; i++) {
        auto level = static_cast<LogLevel>(i);
        std::string name = Logger::LevelToString(level);
        BOOST_CHECK(Logger::LevelFromString(name) == level);
    }
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// Suite 4: Log output and macro tests
// ============================================================================
BOOST_AUTO_TEST_SUITE(log_output_tests)

BOOST_AUTO_TEST_CASE(log_print_str_no_crash)
{
    BOOST_CHECK_NO_THROW(Logger::GetInstance().LogPrintStr("logging test message\n"));
}

BOOST_AUTO_TEST_CASE(log_printf_macro_no_crash)
{
    LoggerStateGuard guard;
    Logger::GetInstance().SetLogLevel(LogLevel::DEBUG);
    BOOST_CHECK_NO_THROW(LogPrintf("test LogPrintf %s %d\n", "hello", 42));
}

BOOST_AUTO_TEST_CASE(log_print_category_macro_no_crash)
{
    LoggerStateGuard guard;
    Logger& logger = Logger::GetInstance();
    logger.SetLogLevel(LogLevel::DEBUG);
    logger.SetCategories(BCLog::ALL);
    BOOST_CHECK_NO_THROW(LogPrint(BCLog::NET, "test LogPrint NET %s\n", "data"));
}

BOOST_AUTO_TEST_CASE(log_error_macro_no_crash)
{
    LoggerStateGuard guard;
    Logger::GetInstance().SetLogLevel(LogLevel::DEBUG);
    BOOST_CHECK_NO_THROW(LogError("test error %d\n", 99));
}

BOOST_AUTO_TEST_CASE(log_print_filtered_by_category)
{
    LoggerStateGuard guard;
    Logger& logger = Logger::GetInstance();
    logger.SetLogLevel(LogLevel::DEBUG);
    logger.SetCategories(BCLog::NET); // only NET enabled

    // WALLET not enabled — should be filtered (but not crash)
    BOOST_CHECK(!logger.WillLog(LogLevel::DEBUG, BCLog::WALLET));
    BOOST_CHECK_NO_THROW(LogPrint(BCLog::WALLET, "this should be filtered\n"));
}

BOOST_AUTO_TEST_CASE(log_print_filtered_by_level)
{
    LoggerStateGuard guard;
    Logger& logger = Logger::GetInstance();
    logger.SetLogLevel(LogLevel::ERR); // only ERROR

    // INFO is above ERROR threshold — filtered
    BOOST_CHECK(!logger.WillLog(LogLevel::INFO));
    BOOST_CHECK_NO_THROW(LogPrintf("this should be filtered by level\n"));
}

// === Thread safety ===

BOOST_AUTO_TEST_CASE(concurrent_level_changes)
{
    LoggerStateGuard guard;
    Logger& logger = Logger::GetInstance();

    std::vector<std::thread> threads;
    for (int i = 0; i < 4; i++) {
        threads.emplace_back([&logger, i]() {
            for (int j = 0; j < 100; j++) {
                logger.SetLogLevel(static_cast<LogLevel>(i % 5));
                logger.WillLog(LogLevel::INFO);
            }
        });
    }
    for (auto& t : threads) t.join();
    BOOST_CHECK(true); // no crash
}

BOOST_AUTO_TEST_CASE(concurrent_category_changes)
{
    LoggerStateGuard guard;
    Logger& logger = Logger::GetInstance();

    std::vector<std::thread> threads;
    for (int i = 0; i < 4; i++) {
        threads.emplace_back([&logger]() {
            for (int j = 0; j < 100; j++) {
                logger.EnableCategory(BCLog::NET);
                logger.DisableCategory(BCLog::NET);
                logger.WillLog(LogLevel::DEBUG, BCLog::NET);
            }
        });
    }
    for (auto& t : threads) t.join();
    BOOST_CHECK(true); // no crash
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// Suite 5: RPC setloglevel/getloglevel tests
// ============================================================================
BOOST_AUTO_TEST_SUITE(log_rpc_tests)

BOOST_AUTO_TEST_CASE(setloglevel_help)
{
    Array p;
    BOOST_CHECK_THROW(setloglevel(p, true), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(setloglevel_sets_level)
{
    LoggerStateGuard guard;
    Array p;
    p.push_back("debug");

    Value result = setloglevel(p, false);
    BOOST_CHECK_EQUAL(result.type(), obj_type);

    Object obj = result.get_obj();
    Value levelVal = json_spirit::find_value(obj, "level");
    BOOST_CHECK_EQUAL(levelVal.get_str(), "debug");
    BOOST_CHECK(Logger::GetInstance().GetLogLevel() == LogLevel::DEBUG);
}

BOOST_AUTO_TEST_CASE(setloglevel_sets_categories)
{
    LoggerStateGuard guard;
    Array p;
    p.push_back("debug");
    p.push_back("net,wallet");

    Value result = setloglevel(p, false);
    Object obj = result.get_obj();

    uint32_t expected = static_cast<uint32_t>(BCLog::NET) | static_cast<uint32_t>(BCLog::WALLET);
    BOOST_CHECK_EQUAL(Logger::GetInstance().GetCategories(), expected);
}

BOOST_AUTO_TEST_CASE(setloglevel_all_categories)
{
    LoggerStateGuard guard;
    Array p;
    p.push_back("info");
    p.push_back("all");

    setloglevel(p, false);
    BOOST_CHECK_EQUAL(Logger::GetInstance().GetCategories(), static_cast<uint32_t>(BCLog::ALL));
}

BOOST_AUTO_TEST_CASE(setloglevel_no_args_throws)
{
    Array p;
    BOOST_CHECK_THROW(setloglevel(p, false), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(getloglevel_help)
{
    Array p;
    BOOST_CHECK_THROW(getloglevel(p, true), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(getloglevel_returns_current_state)
{
    LoggerStateGuard guard;
    Logger& logger = Logger::GetInstance();
    logger.SetLogLevel(LogLevel::WARN);
    logger.SetCategories(BCLog::NET | BCLog::STAKE);

    Array p;
    Value result = getloglevel(p, false);
    BOOST_CHECK_EQUAL(result.type(), obj_type);

    Object obj = result.get_obj();
    BOOST_CHECK_EQUAL(json_spirit::find_value(obj, "level").get_str(), "warn");

    Value catsVal = json_spirit::find_value(obj, "categories");
    BOOST_CHECK_EQUAL(catsVal.type(), array_type);

    Array cats = catsVal.get_array();
    BOOST_CHECK_EQUAL(cats.size(), 2u);
}

BOOST_AUTO_TEST_CASE(setloglevel_roundtrip)
{
    LoggerStateGuard guard;

    // Set via RPC
    Array setParams;
    setParams.push_back("error");
    setParams.push_back("consensus,db");
    setloglevel(setParams, false);

    // Get via RPC
    Array getParams;
    Value result = getloglevel(getParams, false);
    Object obj = result.get_obj();
    BOOST_CHECK_EQUAL(json_spirit::find_value(obj, "level").get_str(), "error");
}

BOOST_AUTO_TEST_SUITE_END()
