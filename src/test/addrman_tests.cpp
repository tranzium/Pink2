// Copyright (c) 2024 The Pinkcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
//
// Unit tests for CAddrInfo and CAddrMan — address manager.

#include <boost/test/unit_test.hpp>

#include "addrman.h"
#include "netbase.h"

// Helper: create a CAddress from an IPv4 string and port
static CAddress CreateAddr(const std::string& ip, int port = 9134)
{
    CService service(ip, port);
    CAddress addr(service);
    addr.nTime = GetAdjustedTime();
    return addr;
}

BOOST_AUTO_TEST_SUITE(addrman_tests)

// ============================================================================
// CAddrInfo
// ============================================================================

BOOST_AUTO_TEST_CASE(addrinfo_init_defaults)
{
    CAddress addr = CreateAddr("1.2.3.4");
    CService source;
    LookupNumeric("5.6.7.8", source);
    CAddrInfo info(addr, source);

    // Serialize and check default fields via roundtrip
    CDataStream ss(SER_GETHASH, 0);
    ss << info;

    CAddrInfo restored;
    ss >> restored;

    // After Init(), nLastSuccess and nAttempts should be 0
    // These are serialized fields we can verify
    // The CAddrInfo serialization includes: CAddress base, source, nLastSuccess, nAttempts
    // We verify by checking IsTerrible behavior (nAttempts < ADDRMAN_RETRIES = 3)
    // A freshly created address with recent nTime should NOT be terrible
    int64_t nNow = GetAdjustedTime();
    BOOST_CHECK(!info.IsTerrible(nNow));
}

BOOST_AUTO_TEST_CASE(addrinfo_is_terrible_old)
{
    CAddress addr = CreateAddr("10.0.0.1");
    addr.nTime = 1000000; // very old (year 1970)
    CService source;
    LookupNumeric("10.0.0.2", source);
    CAddrInfo info(addr, source);

    int64_t nNow = GetAdjustedTime();

    // Address not seen in ADDRMAN_HORIZON_DAYS (30 days) — should be terrible
    BOOST_CHECK(info.IsTerrible(nNow));
}

BOOST_AUTO_TEST_CASE(addrinfo_not_terrible_recent)
{
    int64_t nNow = GetAdjustedTime();

    CAddress addr = CreateAddr("192.168.1.1");
    addr.nTime = nNow - 3600; // seen 1 hour ago
    CService source;
    LookupNumeric("192.168.1.2", source);
    CAddrInfo info(addr, source);

    // Recent address with no failures — should NOT be terrible
    BOOST_CHECK(!info.IsTerrible(nNow));
}

BOOST_AUTO_TEST_CASE(addrinfo_is_terrible_future_time)
{
    int64_t nNow = GetAdjustedTime();

    CAddress addr = CreateAddr("172.16.0.1");
    addr.nTime = nNow + 20 * 60; // 20 minutes in the future (> 10 min threshold)
    CService source;
    LookupNumeric("172.16.0.2", source);
    CAddrInfo info(addr, source);

    // "Flying DeLorean" — time too far in future
    BOOST_CHECK(info.IsTerrible(nNow));
}

BOOST_AUTO_TEST_CASE(addrinfo_get_chance_positive)
{
    int64_t nNow = GetAdjustedTime();

    CAddress addr = CreateAddr("8.8.8.8");
    addr.nTime = nNow - 3600;
    CService source;
    LookupNumeric("8.8.4.4", source);
    CAddrInfo info(addr, source);

    double chance = info.GetChance(nNow);
    BOOST_CHECK(chance > 0.0);
    BOOST_CHECK(chance <= 1.0);
}

BOOST_AUTO_TEST_CASE(addrinfo_get_chance_old_lower)
{
    int64_t nNow = GetAdjustedTime();

    // Recent address
    CAddress addrRecent = CreateAddr("1.1.1.1");
    addrRecent.nTime = nNow - 60; // 1 minute ago
    CService src;
    LookupNumeric("2.2.2.2", src);
    CAddrInfo infoRecent(addrRecent, src);

    // Old address
    CAddress addrOld = CreateAddr("3.3.3.3");
    addrOld.nTime = nNow - 86400 * 7; // 7 days ago
    CAddrInfo infoOld(addrOld, src);

    // Recent should have higher chance than old (all else equal)
    double chanceRecent = infoRecent.GetChance(nNow);
    double chanceOld = infoOld.GetChance(nNow);
    BOOST_CHECK(chanceRecent > chanceOld);
}

BOOST_AUTO_TEST_CASE(addrinfo_get_tried_bucket_deterministic)
{
    CAddress addr = CreateAddr("93.184.216.34", 9134);
    CService source;
    LookupNumeric("10.0.0.1", source);
    CAddrInfo info(addr, source);

    std::vector<unsigned char> nKey(32, 0x42);

    int bucket1 = info.GetTriedBucket(nKey);
    int bucket2 = info.GetTriedBucket(nKey);

    BOOST_CHECK_EQUAL(bucket1, bucket2);
    BOOST_CHECK(bucket1 >= 0 && bucket1 < ADDRMAN_TRIED_BUCKET_COUNT);
}

BOOST_AUTO_TEST_CASE(addrinfo_get_new_bucket_deterministic)
{
    CAddress addr = CreateAddr("198.51.100.1", 9134);
    CService source;
    LookupNumeric("203.0.113.1", source);
    CAddrInfo info(addr, source);

    std::vector<unsigned char> nKey(32, 0xAB);

    int bucket1 = info.GetNewBucket(nKey);
    int bucket2 = info.GetNewBucket(nKey);

    BOOST_CHECK_EQUAL(bucket1, bucket2);
    BOOST_CHECK(bucket1 >= 0 && bucket1 < ADDRMAN_NEW_BUCKET_COUNT);
}

BOOST_AUTO_TEST_CASE(addrinfo_different_key_different_bucket)
{
    CAddress addr = CreateAddr("198.51.100.1", 9134);
    CService source;
    LookupNumeric("203.0.113.1", source);
    CAddrInfo info(addr, source);

    std::vector<unsigned char> key1(32, 0x01);
    std::vector<unsigned char> key2(32, 0xFF);

    int bucket1 = info.GetTriedBucket(key1);
    int bucket2 = info.GetTriedBucket(key2);

    // Different keys should (very likely) produce different buckets
    // This is probabilistic but with very different keys, collision is unlikely
    BOOST_CHECK(bucket1 != bucket2);
}

// ============================================================================
// CAddrMan
// ============================================================================

BOOST_AUTO_TEST_CASE(addrman_add_and_size)
{
    CAddrMan addrman;
    BOOST_CHECK_EQUAL(addrman.size(), 0);

    CAddress addr = CreateAddr("1.2.3.4", 9134);
    CService source;
    LookupNumeric("5.6.7.8", source);

    addrman.Add(addr, source);
    BOOST_CHECK_EQUAL(addrman.size(), 1);
}

BOOST_AUTO_TEST_SUITE_END()
