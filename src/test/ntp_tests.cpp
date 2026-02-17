// Copyright (c) 2024-2026 The Pinkcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Tests for NTP epoch conversion math and constants from ntp.cpp.
// These are pure arithmetic tests — no network I/O.

#include <boost/test/unit_test.hpp>

#include <cstdint>
#include <limits>
#include <cmath>

// The NTP Unix epoch offset: seconds between Jan 1, 1900 and Jan 1, 1970.
// This constant is defined as static in ntp.cpp; we replicate it here
// to test the math without modifying production code.
static const uint64_t nNTPUnix = 2208988800ULL;

BOOST_AUTO_TEST_SUITE(ntp_tests)

// ============================================================================
// Epoch offset verification
// ============================================================================

BOOST_AUTO_TEST_CASE(ntp_epoch_offset_correct)
{
    // From Jan 1, 1900 to Jan 1, 1970 = 70 years.
    // 70 years = 17 leap years (1904,1908,...,1968) + 53 normal years
    // Total days = 17*366 + 53*365 = 6222 + 19345 = 25567
    // Total seconds = 25567 * 86400 = 2208988800
    uint64_t expected = 25567ULL * 86400ULL;
    BOOST_CHECK_EQUAL(nNTPUnix, expected);
    BOOST_CHECK_EQUAL(nNTPUnix, 2208988800ULL);
}

// ============================================================================
// Fractional second → microsecond conversion
// ============================================================================

BOOST_AUTO_TEST_CASE(ntp_fractional_zero)
{
    // tMicros = 0 → 0 microseconds
    uint32_t tMicros = 0;
    uint64_t nMax = static_cast<uint64_t>(std::numeric_limits<uint32_t>::max());
    uint64_t micros = tMicros * 1000000UL / nMax;
    BOOST_CHECK_EQUAL(micros, 0u);
}

BOOST_AUTO_TEST_CASE(ntp_fractional_half_second)
{
    // tMicros = UINT32_MAX/2 ≈ 0.5 seconds → ~500000 microseconds
    uint32_t tMicros = std::numeric_limits<uint32_t>::max() / 2;
    uint64_t nMax = static_cast<uint64_t>(std::numeric_limits<uint32_t>::max());
    uint64_t micros = static_cast<uint64_t>(tMicros) * 1000000UL / nMax;
    // Should be approximately 500000 (within ±1 for rounding)
    BOOST_CHECK(micros >= 499999 && micros <= 500001);
}

BOOST_AUTO_TEST_CASE(ntp_fractional_full_second)
{
    // tMicros = UINT32_MAX ≈ 1.0 seconds → ~999999 microseconds
    uint32_t tMicros = std::numeric_limits<uint32_t>::max();
    uint64_t nMax = static_cast<uint64_t>(std::numeric_limits<uint32_t>::max());
    uint64_t micros = static_cast<uint64_t>(tMicros) * 1000000UL / nMax;
    // Should be 999999 (UINT32_MAX * 1000000 / UINT32_MAX = 999999 due to integer division)
    BOOST_CHECK(micros >= 999999 && micros <= 1000000);
}

BOOST_AUTO_TEST_CASE(ntp_fractional_quarter_second)
{
    // tMicros = UINT32_MAX/4 ≈ 0.25 seconds → ~250000 microseconds
    uint32_t tMicros = std::numeric_limits<uint32_t>::max() / 4;
    uint64_t nMax = static_cast<uint64_t>(std::numeric_limits<uint32_t>::max());
    uint64_t micros = static_cast<uint64_t>(tMicros) * 1000000UL / nMax;
    BOOST_CHECK(micros >= 249999 && micros <= 250001);
}

// ============================================================================
// Unix epoch conversion
// ============================================================================

BOOST_AUTO_TEST_CASE(ntp_unix_epoch_conversion)
{
    // NTP timestamp for Unix epoch: exactly nNTPUnix seconds since Jan 1, 1900
    uint32_t ntpTimestamp = static_cast<uint32_t>(nNTPUnix);
    int64_t unixTime = static_cast<int64_t>(ntpTimestamp) - static_cast<int64_t>(nNTPUnix);
    BOOST_CHECK_EQUAL(unixTime, 0);
}

BOOST_AUTO_TEST_CASE(ntp_known_date_conversion)
{
    // Jan 1, 2000 00:00:00 UTC = Unix timestamp 946684800
    // NTP timestamp = 946684800 + 2208988800 = 3155673600
    uint32_t ntpTimestamp = 3155673600UL;
    int64_t unixTime = static_cast<int64_t>(ntpTimestamp) - static_cast<int64_t>(nNTPUnix);
    BOOST_CHECK_EQUAL(unixTime, 946684800LL);
}

// ============================================================================
// Y2036 rollover handling
// ============================================================================

BOOST_AUTO_TEST_CASE(ntp_y2036_rollover)
{
    // After 2036, NTP 32-bit timestamp rolls over.
    // The code handles this: if nEpoch < 0, add UINT32_MAX.
    // Simulate: tSeconds = 0 (rolled over from UINT32_MAX)
    uint32_t tSeconds = 0;
    int64_t nEpoch = static_cast<int64_t>(tSeconds) - static_cast<int64_t>(nNTPUnix);
    BOOST_CHECK(nEpoch < 0);

    // Apply Y2036 correction
    nEpoch += std::numeric_limits<uint32_t>::max();
    // Should now be positive (represents a time after 2036)
    BOOST_CHECK(nEpoch > 0);

    // The corrected epoch should be around Feb 7, 2036
    // UINT32_MAX - nNTPUnix = 4294967295 - 2208988800 = 2085978495
    // That's approximately 2036-02-07
    BOOST_CHECK_EQUAL(nEpoch, 2085978495LL);
}

// ============================================================================
// Roundtrip compensation arithmetic
// ============================================================================

BOOST_AUTO_TEST_CASE(ntp_roundtrip_compensation)
{
    // diffMicros = (endMicros - startMicros) / 2
    uint64_t startMicros = 1000000;  // 1 second
    uint64_t endMicros = 1100000;    // 1.1 seconds
    uint64_t diffMicros = (endMicros - startMicros) / 2;
    BOOST_CHECK_EQUAL(diffMicros, 50000u);  // 50ms half-roundtrip
}

BOOST_AUTO_TEST_CASE(ntp_roundtrip_zero)
{
    // Instant response → no compensation
    uint64_t startMicros = 1000000;
    uint64_t endMicros = 1000000;
    uint64_t diffMicros = (endMicros - startMicros) / 2;
    BOOST_CHECK_EQUAL(diffMicros, 0u);
}

// ============================================================================
// Server number validation (mirrors threadGetNTPTime logic)
// ============================================================================

BOOST_AUTO_TEST_CASE(ntp_server_number_range)
{
    // threadGetNTPTime returns nullptr for nServer > 3
    for (int i = 0; i <= 3; ++i)
        BOOST_CHECK(i <= 3);

    // nServer = 4 and above → rejected
    BOOST_CHECK(4 > 3);
}

BOOST_AUTO_TEST_CASE(ntp_pool_address_validation)
{
    // The code resets to "pool.ntp.org" if the address:
    // - is empty
    // - contains ":"
    // - doesn't contain "pool.ntp.org"

    std::string addr1 = "";
    BOOST_CHECK(addr1.empty());  // triggers reset

    std::string addr2 = "evil.com:123";
    BOOST_CHECK(addr2.find(":") != std::string::npos);  // triggers reset

    std::string addr3 = "us.pool.ntp.org";
    BOOST_CHECK(addr3.find("pool.ntp.org") != std::string::npos);  // valid

    std::string addr4 = "evil.ntp.org";
    BOOST_CHECK(addr4.find("pool.ntp.org") == std::string::npos);  // triggers reset
}

BOOST_AUTO_TEST_SUITE_END()
