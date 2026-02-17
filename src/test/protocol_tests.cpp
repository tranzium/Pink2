// Copyright (c) 2024-2026 The Pinkcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <boost/test/unit_test.hpp>

#include "protocol.h"
#include "serialize.h"
#include "uint256.h"

#include <string>
#include <sstream>

extern unsigned char pchMessageStart[4];

BOOST_AUTO_TEST_SUITE(protocol_tests)

// ============================================================================
// CMessageHeader tests
// ============================================================================

BOOST_AUTO_TEST_CASE(message_header_default_ctor)
{
    CMessageHeader hdr;

    // pchMessageStart should match the global
    BOOST_CHECK_EQUAL(memcmp(hdr.pchMessageStart, pchMessageStart, CMessageHeader::MESSAGE_START_SIZE), 0);

    // nChecksum should be zero
    BOOST_CHECK_EQUAL(hdr.nChecksum, 0u);
}

BOOST_AUTO_TEST_CASE(message_header_valid_message)
{
    CMessageHeader hdr("version", 100);

    BOOST_CHECK(hdr.IsValid());
    BOOST_CHECK_EQUAL(hdr.GetCommand(), "version");
    BOOST_CHECK_EQUAL(hdr.nMessageSize, 100u);
}

BOOST_AUTO_TEST_CASE(message_header_invalid_start_bytes)
{
    CMessageHeader hdr("version", 100);

    // Corrupt the start bytes
    hdr.pchMessageStart[0] = 0xFF;
    hdr.pchMessageStart[1] = 0xFF;
    BOOST_CHECK(!hdr.IsValid());
}

BOOST_AUTO_TEST_CASE(message_header_non_printable_command)
{
    CMessageHeader hdr;
    memcpy(hdr.pchMessageStart, pchMessageStart, CMessageHeader::MESSAGE_START_SIZE);
    memset(hdr.pchCommand, 0, CMessageHeader::COMMAND_SIZE);

    // Insert a control character
    hdr.pchCommand[0] = 'v';
    hdr.pchCommand[1] = 0x01;  // non-printable
    hdr.nMessageSize = 0;

    BOOST_CHECK(!hdr.IsValid());
}

BOOST_AUTO_TEST_CASE(message_header_get_command_null_terminated)
{
    CMessageHeader hdr("ping", 0);
    std::string cmd = hdr.GetCommand();
    BOOST_CHECK_EQUAL(cmd, "ping");
    BOOST_CHECK_EQUAL(cmd.size(), 4u);
}

BOOST_AUTO_TEST_CASE(message_header_get_command_max_length)
{
    // Fill all 12 bytes with printable chars (no null terminator)
    CMessageHeader hdr;
    memcpy(hdr.pchMessageStart, pchMessageStart, CMessageHeader::MESSAGE_START_SIZE);
    memset(hdr.pchCommand, 'A', CMessageHeader::COMMAND_SIZE);
    hdr.nMessageSize = 0;

    BOOST_CHECK(hdr.IsValid());
    std::string cmd = hdr.GetCommand();
    BOOST_CHECK_EQUAL(cmd.size(), static_cast<size_t>(CMessageHeader::COMMAND_SIZE));
    BOOST_CHECK_EQUAL(cmd, std::string(CMessageHeader::COMMAND_SIZE, 'A'));
}

BOOST_AUTO_TEST_CASE(message_header_serialization_roundtrip)
{
    CMessageHeader hdr("getdata", 42);

    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << hdr;

    CMessageHeader hdr2;
    ss >> hdr2;

    BOOST_CHECK_EQUAL(hdr.GetCommand(), hdr2.GetCommand());
    BOOST_CHECK_EQUAL(hdr.nMessageSize, hdr2.nMessageSize);
    BOOST_CHECK_EQUAL(hdr.nChecksum, hdr2.nChecksum);
    BOOST_CHECK_EQUAL(memcmp(hdr.pchMessageStart, hdr2.pchMessageStart, CMessageHeader::MESSAGE_START_SIZE), 0);
}

// ============================================================================
// CInv tests
// ============================================================================

BOOST_AUTO_TEST_CASE(cinv_default_ctor)
{
    CInv inv;
    BOOST_CHECK_EQUAL(inv.type, 0);
    BOOST_CHECK(inv.hash == 0);
}

BOOST_AUTO_TEST_CASE(cinv_int_hash_ctor_tx)
{
    uint256 hash("0000000000000000000000000000000000000000000000000000000000000001");
    CInv inv(1, hash);  // MSG_TX = 1
    BOOST_CHECK_EQUAL(inv.type, 1);
    BOOST_CHECK(inv.hash == hash);
}

BOOST_AUTO_TEST_CASE(cinv_int_hash_ctor_block)
{
    uint256 hash("0000000000000000000000000000000000000000000000000000000000000002");
    CInv inv(2, hash);  // MSG_BLOCK = 2
    BOOST_CHECK_EQUAL(inv.type, 2);
    BOOST_CHECK(inv.hash == hash);
}

BOOST_AUTO_TEST_CASE(cinv_string_hash_ctor_tx)
{
    uint256 hash("0000000000000000000000000000000000000000000000000000000000000001");
    CInv inv("tx", hash);
    BOOST_CHECK_EQUAL(inv.type, 1);
    BOOST_CHECK(inv.hash == hash);
}

BOOST_AUTO_TEST_CASE(cinv_string_hash_ctor_block)
{
    uint256 hash("0000000000000000000000000000000000000000000000000000000000000002");
    CInv inv("block", hash);
    BOOST_CHECK_EQUAL(inv.type, 2);
    BOOST_CHECK(inv.hash == hash);
}

BOOST_AUTO_TEST_CASE(cinv_string_hash_invalid_type)
{
    uint256 hash("0000000000000000000000000000000000000000000000000000000000000001");
    BOOST_CHECK_THROW(CInv("unknown_type", hash), std::out_of_range);
}

BOOST_AUTO_TEST_CASE(cinv_is_known_type_boundaries)
{
    uint256 hash;

    CInv inv0(0, hash);
    BOOST_CHECK(!inv0.IsKnownType());  // type 0 = "ERROR", not known

    CInv inv1(1, hash);
    BOOST_CHECK(inv1.IsKnownType());   // type 1 = "tx"

    CInv inv2(2, hash);
    BOOST_CHECK(inv2.IsKnownType());   // type 2 = "block"

    CInv inv3;
    inv3.type = 3;
    BOOST_CHECK(!inv3.IsKnownType());  // type 3 = out of range
}

BOOST_AUTO_TEST_CASE(cinv_get_command)
{
    uint256 hash;
    CInv inv1(1, hash);
    BOOST_CHECK_EQUAL(std::string(inv1.GetCommand()), "tx");

    CInv inv2(2, hash);
    BOOST_CHECK_EQUAL(std::string(inv2.GetCommand()), "block");
}

BOOST_AUTO_TEST_CASE(cinv_get_command_unknown_throws)
{
    CInv inv;
    inv.type = 99;
    BOOST_CHECK_THROW(inv.GetCommand(), std::out_of_range);
}

BOOST_AUTO_TEST_CASE(cinv_tostring)
{
    uint256 hash("0000000000000000000000000000000000000000000000000000000000000abc");
    CInv inv(1, hash);
    std::string s = inv.ToString();
    BOOST_CHECK(s.find("tx") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(cinv_serialization_roundtrip)
{
    uint256 hash("00000000000000000000000000000000000000000000000000000000deadbeef");
    CInv inv(2, hash);

    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << inv;

    CInv inv2;
    ss >> inv2;

    BOOST_CHECK_EQUAL(inv.type, inv2.type);
    BOOST_CHECK(inv.hash == inv2.hash);
}

// ============================================================================
// CAddress tests
// ============================================================================

BOOST_AUTO_TEST_CASE(caddress_default_ctor)
{
    CAddress addr;
    BOOST_CHECK_EQUAL(addr.nServices, NODE_NETWORK);
    BOOST_CHECK_EQUAL(addr.nTime, 100000000u);
    BOOST_CHECK_EQUAL(addr.nLastTry, 0);
}

BOOST_AUTO_TEST_CASE(caddress_service_ctor)
{
    CService svc("1.2.3.4", 9134);
    CAddress addr(svc, NODE_NETWORK);

    BOOST_CHECK_EQUAL(addr.nServices, NODE_NETWORK);
    BOOST_CHECK_EQUAL(addr.GetPort(), 9134);
}

BOOST_AUTO_TEST_CASE(caddress_serialization_roundtrip)
{
    CService svc("10.0.0.1", 8333);
    CAddress addr(svc, NODE_NETWORK);

    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << addr;

    CAddress addr2;
    ss >> addr2;

    BOOST_CHECK_EQUAL(addr.nServices, addr2.nServices);
    BOOST_CHECK_EQUAL(addr.GetPort(), addr2.GetPort());
}

// ============================================================================
// operator< for CInv
// ============================================================================

BOOST_AUTO_TEST_CASE(cinv_less_than_ordering)
{
    uint256 hash1("0000000000000000000000000000000000000000000000000000000000000001");
    uint256 hash2("0000000000000000000000000000000000000000000000000000000000000002");

    CInv a(1, hash1);
    CInv b(1, hash2);
    CInv c(2, hash1);

    // Same type, different hash: hash1 < hash2
    BOOST_CHECK(a < b);
    BOOST_CHECK(!(b < a));

    // Different type: type 1 < type 2
    BOOST_CHECK(a < c);
    BOOST_CHECK(!(c < a));
}

BOOST_AUTO_TEST_SUITE_END()
