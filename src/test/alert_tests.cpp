// Copyright (c) 2024 The Pinkcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
//
// Unit tests for CUnsignedAlert and CAlert — alert system data structures.

#include <boost/test/unit_test.hpp>

#include "alert.h"
#include "serialize.h"
#include "version.h"
#include "util.h"

BOOST_AUTO_TEST_SUITE(alert_tests)

// ============================================================================
// CUnsignedAlert
// ============================================================================

BOOST_AUTO_TEST_CASE(unsigned_alert_set_null)
{
    CUnsignedAlert alert;
    alert.nVersion = 99;
    alert.nExpiration = 12345;
    alert.nID = 7;
    alert.nCancel = 3;
    alert.nMinVer = 1;
    alert.nMaxVer = 100;
    alert.nPriority = 500;
    alert.strComment = "test";
    alert.strStatusBar = "status";

    alert.SetNull();

    BOOST_CHECK_EQUAL(alert.nVersion, 1);
    BOOST_CHECK_EQUAL(alert.nRelayUntil, 0);
    BOOST_CHECK_EQUAL(alert.nExpiration, 0);
    BOOST_CHECK_EQUAL(alert.nID, 0);
    BOOST_CHECK_EQUAL(alert.nCancel, 0);
    BOOST_CHECK(alert.setCancel.empty());
    BOOST_CHECK_EQUAL(alert.nMinVer, 0);
    BOOST_CHECK_EQUAL(alert.nMaxVer, 0);
    BOOST_CHECK(alert.setSubVer.empty());
    BOOST_CHECK_EQUAL(alert.nPriority, 0);
    BOOST_CHECK(alert.strComment.empty());
    BOOST_CHECK(alert.strStatusBar.empty());
    BOOST_CHECK(alert.strReserved.empty());
}

BOOST_AUTO_TEST_CASE(unsigned_alert_to_string)
{
    CUnsignedAlert alert;
    alert.SetNull();
    alert.nID = 42;
    alert.strStatusBar = "Test Alert";

    std::string s = alert.ToString();
    BOOST_CHECK(!s.empty());
    BOOST_CHECK(s.find("nID") != std::string::npos);
    BOOST_CHECK(s.find("42") != std::string::npos);
    BOOST_CHECK(s.find("Test Alert") != std::string::npos);
}

// ============================================================================
// CAlert
// ============================================================================

BOOST_AUTO_TEST_CASE(alert_set_null)
{
    CAlert alert;
    alert.vchMsg.push_back(0x01);
    alert.vchSig.push_back(0x02);
    alert.nExpiration = 99999;

    alert.SetNull();

    BOOST_CHECK(alert.vchMsg.empty());
    BOOST_CHECK(alert.vchSig.empty());
    BOOST_CHECK_EQUAL(alert.nExpiration, 0);
}

BOOST_AUTO_TEST_CASE(alert_is_null_true)
{
    CAlert alert;
    alert.SetNull();
    // nExpiration == 0 → IsNull
    BOOST_CHECK(alert.IsNull());
}

BOOST_AUTO_TEST_CASE(alert_is_null_false)
{
    CAlert alert;
    alert.SetNull();
    alert.nExpiration = std::numeric_limits<int64_t>::max();
    BOOST_CHECK(!alert.IsNull());
}

BOOST_AUTO_TEST_CASE(alert_is_in_effect)
{
    CAlert alert;
    alert.SetNull();
    // Far future expiration — should be in effect
    alert.nExpiration = GetAdjustedTime() + 86400 * 365;
    BOOST_CHECK(alert.IsInEffect());
}

BOOST_AUTO_TEST_CASE(alert_not_in_effect_expired)
{
    CAlert alert;
    alert.SetNull();
    // Already expired
    alert.nExpiration = 1000000; // way in the past
    BOOST_CHECK(!alert.IsInEffect());
}

BOOST_AUTO_TEST_CASE(alert_cancels_lower_id)
{
    CAlert canceller;
    canceller.SetNull();
    canceller.nExpiration = GetAdjustedTime() + 86400;
    canceller.nCancel = 5;

    CAlert target;
    target.SetNull();
    target.nID = 4;

    // canceller with nCancel=5 should cancel target with nID=4
    BOOST_CHECK(canceller.Cancels(target));

    // Should not cancel nID=6
    target.nID = 6;
    BOOST_CHECK(!canceller.Cancels(target));

    // Expired canceller should not cancel anything
    CAlert expiredCanceller;
    expiredCanceller.SetNull();
    expiredCanceller.nExpiration = 1000; // past
    expiredCanceller.nCancel = 10;

    target.nID = 3;
    BOOST_CHECK(!expiredCanceller.Cancels(target));
}

BOOST_AUTO_TEST_CASE(alert_applies_to_version)
{
    CAlert alert;
    alert.SetNull();
    alert.nExpiration = GetAdjustedTime() + 86400;
    alert.nMinVer = 10;
    alert.nMaxVer = 100;

    // In range
    BOOST_CHECK(alert.AppliesTo(50, ""));
    BOOST_CHECK(alert.AppliesTo(10, ""));  // lower bound
    BOOST_CHECK(alert.AppliesTo(100, "")); // upper bound

    // Out of range
    BOOST_CHECK(!alert.AppliesTo(9, ""));
    BOOST_CHECK(!alert.AppliesTo(101, ""));

    // Expired alert applies to nothing
    CAlert expired;
    expired.SetNull();
    expired.nExpiration = 1000;
    expired.nMinVer = 1;
    expired.nMaxVer = 999999;
    BOOST_CHECK(!expired.AppliesTo(50, ""));
}

BOOST_AUTO_TEST_CASE(alert_serialization_roundtrip)
{
    // Serialize a CUnsignedAlert, deserialize, verify fields match
    CUnsignedAlert orig;
    orig.SetNull();
    orig.nVersion = 1;
    orig.nRelayUntil = 1700000000;
    orig.nExpiration = 1800000000;
    orig.nID = 42;
    orig.nCancel = 10;
    orig.setCancel.insert(5);
    orig.setCancel.insert(7);
    orig.nMinVer = 60000;
    orig.nMaxVer = 70000;
    orig.setSubVer.insert("/Satoshi:0.9/");
    orig.nPriority = 100;
    orig.strComment = "Test comment";
    orig.strStatusBar = "Alert status bar";

    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << orig;

    CUnsignedAlert restored;
    ss >> restored;

    BOOST_CHECK_EQUAL(restored.nVersion, orig.nVersion);
    BOOST_CHECK_EQUAL(restored.nRelayUntil, orig.nRelayUntil);
    BOOST_CHECK_EQUAL(restored.nExpiration, orig.nExpiration);
    BOOST_CHECK_EQUAL(restored.nID, orig.nID);
    BOOST_CHECK_EQUAL(restored.nCancel, orig.nCancel);
    BOOST_CHECK(restored.setCancel == orig.setCancel);
    BOOST_CHECK_EQUAL(restored.nMinVer, orig.nMinVer);
    BOOST_CHECK_EQUAL(restored.nMaxVer, orig.nMaxVer);
    BOOST_CHECK(restored.setSubVer == orig.setSubVer);
    BOOST_CHECK_EQUAL(restored.nPriority, orig.nPriority);
    BOOST_CHECK_EQUAL(restored.strComment, orig.strComment);
    BOOST_CHECK_EQUAL(restored.strStatusBar, orig.strStatusBar);
}

BOOST_AUTO_TEST_SUITE_END()
