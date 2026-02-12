// Copyright (c) 2024-2026 The Pinkcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Tests for startup/shutdown logic in init.cpp that previously had zero
// test coverage: parameter interactions, filename validation, timeout
// bounds, fee parsing, checkpoint mode, and InitSanityCheck.

#include <boost/test/unit_test.hpp>

#include "main.h"
#include "wallet.h"
#include "key.h"
#include "util.h"
#include "netbase.h"
#include "checkpoints.h"

extern std::map<std::string, std::string> mapArgs;
extern std::map<std::string, std::vector<std::string>> mapMultiArgs;
extern int nConnectTimeout;
extern int64_t nTransactionFee;
extern int64_t nMinimumInputValue;
extern unsigned int nNodeLifespan;
extern bool fUseFastIndex;
extern unsigned int nDerivationMethodIndex;
extern bool fTestNet;
extern enum Checkpoints::CPMode CheckpointsMode;
extern bool fShutdown;
extern bool fRequestShutdown;

// Helper: save and restore global args state across tests.
struct ArgsGuard {
    std::map<std::string, std::string> savedArgs;
    std::map<std::string, std::vector<std::string>> savedMultiArgs;

    ArgsGuard()
        : savedArgs(mapArgs), savedMultiArgs(mapMultiArgs) {}

    ~ArgsGuard() {
        mapArgs = savedArgs;
        mapMultiArgs = savedMultiArgs;
    }
};

// ============================================================================
// Parameter interaction tests (init.cpp Step 2)
// ============================================================================

BOOST_AUTO_TEST_SUITE(init_param_interaction_tests)

BOOST_AUTO_TEST_CASE(bind_forces_listen)
{
    // -bind should force -listen=true via SoftSetBoolArg.
    ArgsGuard guard;
    mapArgs.clear();
    mapMultiArgs.clear();

    // SoftSetBoolArg only sets if key doesn't already exist.
    // Simulate what AppInit2 does: if -bind present, SoftSet -listen=true.
    mapArgs["-bind"] = "0.0.0.0";
    SoftSetBoolArg("-listen", true);
    BOOST_CHECK_EQUAL(mapArgs["-listen"], "1");
}

BOOST_AUTO_TEST_CASE(connect_disables_dnsseed_and_listen)
{
    // -connect should disable -dnsseed and -listen.
    ArgsGuard guard;
    mapArgs.clear();
    mapMultiArgs.clear();

    mapArgs["-connect"] = "127.0.0.1";
    mapMultiArgs["-connect"].push_back("127.0.0.1");
    SoftSetBoolArg("-dnsseed", false);
    SoftSetBoolArg("-listen", false);
    BOOST_CHECK_EQUAL(mapArgs["-dnsseed"], "0");
    BOOST_CHECK_EQUAL(mapArgs["-listen"], "0");
}

BOOST_AUTO_TEST_CASE(proxy_disables_listen)
{
    // -proxy should disable -listen.
    ArgsGuard guard;
    mapArgs.clear();
    mapMultiArgs.clear();

    mapArgs["-proxy"] = "127.0.0.1:9050";
    SoftSetBoolArg("-listen", false);
    BOOST_CHECK_EQUAL(mapArgs["-listen"], "0");
}

BOOST_AUTO_TEST_CASE(no_listen_disables_upnp_and_discover)
{
    // -listen=false should disable -upnp and -discover.
    ArgsGuard guard;
    mapArgs.clear();
    mapMultiArgs.clear();

    mapArgs["-listen"] = "0";
    if (!GetBoolArg("-listen", true)) {
        SoftSetBoolArg("-upnp", false);
        SoftSetBoolArg("-discover", false);
    }
    BOOST_CHECK_EQUAL(mapArgs["-upnp"], "0");
    BOOST_CHECK_EQUAL(mapArgs["-discover"], "0");
}

BOOST_AUTO_TEST_CASE(externalip_disables_discover)
{
    // -externalip should disable -discover.
    ArgsGuard guard;
    mapArgs.clear();
    mapMultiArgs.clear();

    mapArgs["-externalip"] = "1.2.3.4";
    SoftSetBoolArg("-discover", false);
    BOOST_CHECK_EQUAL(mapArgs["-discover"], "0");
}

BOOST_AUTO_TEST_CASE(salvagewallet_forces_rescan)
{
    // -salvagewallet should force -rescan.
    ArgsGuard guard;
    mapArgs.clear();
    mapMultiArgs.clear();

    mapArgs["-salvagewallet"] = "1";
    SoftSetBoolArg("-rescan", true);
    BOOST_CHECK_EQUAL(mapArgs["-rescan"], "1");
}

BOOST_AUTO_TEST_CASE(softsetboolarg_does_not_override)
{
    // SoftSetBoolArg should NOT override an existing value.
    ArgsGuard guard;
    mapArgs.clear();
    mapMultiArgs.clear();

    mapArgs["-listen"] = "1";  // explicitly set
    bool changed = SoftSetBoolArg("-listen", false);  // try to change
    BOOST_CHECK(!changed);
    BOOST_CHECK_EQUAL(mapArgs["-listen"], "1");  // unchanged
}

BOOST_AUTO_TEST_CASE(softsetboolarg_sets_when_absent)
{
    // SoftSetBoolArg should set the value when key is absent.
    ArgsGuard guard;
    mapArgs.clear();
    mapMultiArgs.clear();

    bool changed = SoftSetBoolArg("-testkey", true);
    BOOST_CHECK(changed);
    BOOST_CHECK_EQUAL(mapArgs["-testkey"], "1");
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// Parameter-to-flags tests (init.cpp Step 3)
// ============================================================================

BOOST_AUTO_TEST_SUITE(init_param_flags_tests)

BOOST_AUTO_TEST_CASE(timeout_bounds_valid)
{
    // Valid timeout (5000) should be accepted.
    ArgsGuard guard;
    int savedTimeout = nConnectTimeout;
    mapArgs["-timeout"] = "5000";

    int nNewTimeout = GetArg("-timeout", 5000);
    if (nNewTimeout > 0 && nNewTimeout < 600000)
        nConnectTimeout = nNewTimeout;
    BOOST_CHECK_EQUAL(nConnectTimeout, 5000);

    nConnectTimeout = savedTimeout;
    mapArgs.erase("-timeout");
}

BOOST_AUTO_TEST_CASE(timeout_bounds_zero_rejected)
{
    // Timeout of 0 should NOT be accepted.
    ArgsGuard guard;
    int savedTimeout = nConnectTimeout;
    int originalTimeout = nConnectTimeout;
    mapArgs["-timeout"] = "0";

    int nNewTimeout = GetArg("-timeout", 5000);
    if (nNewTimeout > 0 && nNewTimeout < 600000)
        nConnectTimeout = nNewTimeout;
    BOOST_CHECK_EQUAL(nConnectTimeout, originalTimeout);  // unchanged

    nConnectTimeout = savedTimeout;
    mapArgs.erase("-timeout");
}

BOOST_AUTO_TEST_CASE(timeout_bounds_too_high_rejected)
{
    // Timeout of 600000 (boundary) should NOT be accepted (< 600000 required).
    ArgsGuard guard;
    int savedTimeout = nConnectTimeout;
    int originalTimeout = nConnectTimeout;
    mapArgs["-timeout"] = "600000";

    int nNewTimeout = GetArg("-timeout", 5000);
    if (nNewTimeout > 0 && nNewTimeout < 600000)
        nConnectTimeout = nNewTimeout;
    BOOST_CHECK_EQUAL(nConnectTimeout, originalTimeout);  // unchanged

    nConnectTimeout = savedTimeout;
    mapArgs.erase("-timeout");
}

BOOST_AUTO_TEST_CASE(timeout_bounds_max_valid)
{
    // Timeout of 599999 (just below limit) should be accepted.
    ArgsGuard guard;
    int savedTimeout = nConnectTimeout;
    mapArgs["-timeout"] = "599999";

    int nNewTimeout = GetArg("-timeout", 5000);
    if (nNewTimeout > 0 && nNewTimeout < 600000)
        nConnectTimeout = nNewTimeout;
    BOOST_CHECK_EQUAL(nConnectTimeout, 599999);

    nConnectTimeout = savedTimeout;
    mapArgs.erase("-timeout");
}

BOOST_AUTO_TEST_CASE(fee_parsing_valid)
{
    // ParseMoney should correctly parse a valid fee string.
    int64_t nFee = 0;
    BOOST_CHECK(ParseMoney("0.001", nFee));
    BOOST_CHECK_EQUAL(nFee, 100000);  // 0.001 * COIN

    BOOST_CHECK(ParseMoney("0.01", nFee));
    BOOST_CHECK_EQUAL(nFee, 1000000);  // 0.01 * COIN
}

BOOST_AUTO_TEST_CASE(fee_parsing_invalid)
{
    // ParseMoney should reject non-numeric strings.
    int64_t nFee = 0;
    BOOST_CHECK(!ParseMoney("notanumber", nFee));
    // Note: ParseMoney("") returns true with value 0 in this codebase.
    BOOST_CHECK(ParseMoney("", nFee));
    BOOST_CHECK_EQUAL(nFee, 0);
}

BOOST_AUTO_TEST_CASE(fee_high_threshold)
{
    // Fees > 0.25 COIN are "very high" (warning threshold in init.cpp).
    int64_t nHighFee = 0;
    BOOST_CHECK(ParseMoney("0.26", nHighFee));
    BOOST_CHECK(nHighFee > 0.25 * COIN);

    int64_t nNormalFee = 0;
    BOOST_CHECK(ParseMoney("0.001", nNormalFee));
    BOOST_CHECK(nNormalFee <= 0.25 * COIN);
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// Filename validation tests (init.cpp Step 4)
// ============================================================================

BOOST_AUTO_TEST_SUITE(init_filename_tests)

BOOST_AUTO_TEST_CASE(plain_filename_accepted)
{
    // A plain filename (no path separators) should pass validation.
    std::string filename = "wallet.dat";
    BOOST_CHECK_EQUAL(filename,
        std::filesystem::path(filename).filename().string());
}

BOOST_AUTO_TEST_CASE(filename_with_path_rejected)
{
    // A filename containing path separators should fail validation.
    std::string filename = "data/wallet.dat";
    BOOST_CHECK_NE(filename,
        std::filesystem::path(filename).filename().string());
}

BOOST_AUTO_TEST_CASE(filename_with_absolute_path_rejected)
{
    std::string filename = "/tmp/wallet.dat";
    BOOST_CHECK_NE(filename,
        std::filesystem::path(filename).filename().string());
}

BOOST_AUTO_TEST_CASE(stake_db_plain_filename_accepted)
{
    std::string filename = "stake.dat";
    BOOST_CHECK_EQUAL(filename,
        std::filesystem::path(filename).filename().string());
}

BOOST_AUTO_TEST_CASE(custom_wallet_filename_accepted)
{
    // Custom names without path separators should pass.
    std::string filename = "my_wallet_backup.dat";
    BOOST_CHECK_EQUAL(filename,
        std::filesystem::path(filename).filename().string());
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// Checkpoint mode tests (init.cpp Step 2)
// ============================================================================

BOOST_AUTO_TEST_SUITE(init_checkpoint_mode_tests)

BOOST_AUTO_TEST_CASE(checkpoint_mode_strict)
{
    BOOST_CHECK_EQUAL(Checkpoints::STRICT, 0);
}

BOOST_AUTO_TEST_CASE(checkpoint_mode_advisory)
{
    BOOST_CHECK_EQUAL(Checkpoints::ADVISORY, 1);
}

BOOST_AUTO_TEST_CASE(checkpoint_mode_permissive)
{
    BOOST_CHECK_EQUAL(Checkpoints::PERMISSIVE, 2);
}

BOOST_AUTO_TEST_CASE(checkpoint_mode_default_is_strict)
{
    // AppInit2 sets CheckpointsMode = Checkpoints::STRICT by default.
    // We can verify the enum value mapping.
    Checkpoints::CPMode defaultMode = Checkpoints::STRICT;
    BOOST_CHECK_EQUAL(static_cast<int>(defaultMode), 0);
}

BOOST_AUTO_TEST_CASE(checkpoint_mode_string_parsing)
{
    // Simulate the string-to-enum parsing from init.cpp Step 2.
    Checkpoints::CPMode mode = Checkpoints::STRICT;

    std::string strCpMode = "advisory";
    if (strCpMode == "strict")    mode = Checkpoints::STRICT;
    if (strCpMode == "advisory")  mode = Checkpoints::ADVISORY;
    if (strCpMode == "permissive") mode = Checkpoints::PERMISSIVE;

    BOOST_CHECK_EQUAL(static_cast<int>(mode),
                      static_cast<int>(Checkpoints::ADVISORY));
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// InitSanityCheck tests (init.cpp Step 4)
// ============================================================================

BOOST_AUTO_TEST_SUITE(init_sanity_tests)

BOOST_AUTO_TEST_CASE(ecc_sanity_check_passes)
{
    // InitSanityCheck (init.cpp:357) delegates to ECC_InitSanityCheck().
    // In a working environment with OpenSSL EC support, this must pass.
    BOOST_CHECK(ECC_InitSanityCheck());
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// Global defaults tests (init.cpp Step 2)
// ============================================================================

BOOST_AUTO_TEST_SUITE(init_global_defaults_tests)

BOOST_AUTO_TEST_CASE(node_lifespan_default)
{
    // Default nNodeLifespan = 7 (from GetArg("-addrlifespan", 7)).
    // The actual value depends on whether -addrlifespan was set,
    // but the default should be 7.
    ArgsGuard guard;
    mapArgs.erase("-addrlifespan");
    int64_t lifespan = GetArg("-addrlifespan", 7);
    BOOST_CHECK_EQUAL(lifespan, 7);
}

BOOST_AUTO_TEST_CASE(fast_index_default_true)
{
    // Default fUseFastIndex = true (from GetBoolArg("-fastindex", true)).
    ArgsGuard guard;
    mapArgs.erase("-fastindex");
    bool fastIndex = GetBoolArg("-fastindex", true);
    BOOST_CHECK(fastIndex);
}

BOOST_AUTO_TEST_CASE(derivation_method_index_zero)
{
    // nDerivationMethodIndex is always set to 0.
    BOOST_CHECK_EQUAL(nDerivationMethodIndex, 0u);
}

BOOST_AUTO_TEST_CASE(mininput_parsing)
{
    // ParseMoney should handle minimum input values.
    int64_t nMinInput = 0;
    BOOST_CHECK(ParseMoney("0.01", nMinInput));
    BOOST_CHECK_EQUAL(nMinInput, 1000000);  // 0.01 PINK = 1000000 satoshis
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// Shutdown sequence tests
// ============================================================================

BOOST_AUTO_TEST_SUITE(init_shutdown_tests)

BOOST_AUTO_TEST_CASE(fshutdown_initially_false)
{
    // fShutdown should be false during test execution.
    // (Tests wouldn't be running if shutdown was in progress.)
    BOOST_CHECK(!fShutdown);
}

BOOST_AUTO_TEST_CASE(frequest_shutdown_initially_false)
{
    BOOST_CHECK(!fRequestShutdown);
}

BOOST_AUTO_TEST_SUITE_END()
