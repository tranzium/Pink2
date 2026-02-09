#include <boost/test/unit_test.hpp>

#include <string>
#include <vector>

#include "netbase.h"
#include "net.h"
#include "protocol.h"
#include "version.h"
#include "serialize.h"

using namespace std;

BOOST_AUTO_TEST_SUITE(netbase_tests)

BOOST_AUTO_TEST_CASE(netbase_networks)
{
    BOOST_CHECK(CNetAddr("127.0.0.1").GetNetwork()                              == NET_UNROUTABLE);
    BOOST_CHECK(CNetAddr("::1").GetNetwork()                                    == NET_UNROUTABLE);
    BOOST_CHECK(CNetAddr("8.8.8.8").GetNetwork()                                == NET_IPV4);
    BOOST_CHECK(CNetAddr("2001::8888").GetNetwork()                             == NET_IPV6);
    BOOST_CHECK(CNetAddr("FD87:D87E:EB43:edb1:8e4:3588:e546:35ca").GetNetwork() == NET_TOR);
}

BOOST_AUTO_TEST_CASE(netbase_properties)
{
    BOOST_CHECK(CNetAddr("127.0.0.1").IsIPv4());
    BOOST_CHECK(CNetAddr("::FFFF:192.168.1.1").IsIPv4());
    BOOST_CHECK(CNetAddr("::1").IsIPv6());
    BOOST_CHECK(CNetAddr("10.0.0.1").IsRFC1918());
    BOOST_CHECK(CNetAddr("192.168.1.1").IsRFC1918());
    BOOST_CHECK(CNetAddr("172.31.255.255").IsRFC1918());
    BOOST_CHECK(CNetAddr("2001:0DB8::").IsRFC3849());
    BOOST_CHECK(CNetAddr("169.254.1.1").IsRFC3927());
    BOOST_CHECK(CNetAddr("2002::1").IsRFC3964());
    BOOST_CHECK(CNetAddr("FC00::").IsRFC4193());
    BOOST_CHECK(CNetAddr("2001::2").IsRFC4380());
    BOOST_CHECK(CNetAddr("2001:10::").IsRFC4843());
    BOOST_CHECK(CNetAddr("FE80::").IsRFC4862());
    BOOST_CHECK(CNetAddr("64:FF9B::").IsRFC6052());
    BOOST_CHECK(CNetAddr("FD87:D87E:EB43:edb1:8e4:3588:e546:35ca").IsTor());
    BOOST_CHECK(CNetAddr("127.0.0.1").IsLocal());
    BOOST_CHECK(CNetAddr("::1").IsLocal());
    BOOST_CHECK(CNetAddr("8.8.8.8").IsRoutable());
    BOOST_CHECK(CNetAddr("2001::1").IsRoutable());
    BOOST_CHECK(CNetAddr("127.0.0.1").IsValid());
}

bool static TestSplitHost(string test, string host, int port)
{
    string hostOut;
    int portOut = -1;
    SplitHostPort(test, portOut, hostOut);
    return hostOut == host && port == portOut;
}

BOOST_AUTO_TEST_CASE(netbase_splithost)
{
    BOOST_CHECK(TestSplitHost("www.bitcoin.org", "www.bitcoin.org", -1));
    BOOST_CHECK(TestSplitHost("[www.bitcoin.org]", "www.bitcoin.org", -1));
    BOOST_CHECK(TestSplitHost("www.bitcoin.org:80", "www.bitcoin.org", 80));
    BOOST_CHECK(TestSplitHost("[www.bitcoin.org]:80", "www.bitcoin.org", 80));
    BOOST_CHECK(TestSplitHost("127.0.0.1", "127.0.0.1", -1));
    BOOST_CHECK(TestSplitHost("127.0.0.1:8333", "127.0.0.1", 8333));
    BOOST_CHECK(TestSplitHost("[127.0.0.1]", "127.0.0.1", -1));
    BOOST_CHECK(TestSplitHost("[127.0.0.1]:8333", "127.0.0.1", 8333));
    BOOST_CHECK(TestSplitHost("::ffff:127.0.0.1", "::ffff:127.0.0.1", -1));
    BOOST_CHECK(TestSplitHost("[::ffff:127.0.0.1]:8333", "::ffff:127.0.0.1", 8333));
    BOOST_CHECK(TestSplitHost("[::]:8333", "::", 8333));
    BOOST_CHECK(TestSplitHost("::8333", "::8333", -1));
    BOOST_CHECK(TestSplitHost(":8333", "", 8333));
    BOOST_CHECK(TestSplitHost("[]:8333", "", 8333));
    BOOST_CHECK(TestSplitHost("", "", -1));
}

bool static TestParse(string src, string canon)
{
    CService addr;
    if (!LookupNumeric(src.c_str(), addr, 65535))
        return canon == "";
    return canon == addr.ToString();
}

BOOST_AUTO_TEST_CASE(netbase_lookupnumeric)
{
    BOOST_CHECK(TestParse("127.0.0.1", "127.0.0.1:65535"));
    BOOST_CHECK(TestParse("127.0.0.1:8333", "127.0.0.1:8333"));
    BOOST_CHECK(TestParse("::ffff:127.0.0.1", "127.0.0.1:65535"));
    BOOST_CHECK(TestParse("::", "[::]:65535"));
    BOOST_CHECK(TestParse("[::]:8333", "[::]:8333"));
    BOOST_CHECK(TestParse("[127.0.0.1]", "127.0.0.1:65535"));
    BOOST_CHECK(TestParse(":::", ""));
}

BOOST_AUTO_TEST_CASE(onioncat_test)
{
    // values from http://www.cypherpunk.at/onioncat/wiki/OnionCat
    CNetAddr addr1("5wyqrzbvrdsumnok.onion");
    CNetAddr addr2("FD87:D87E:EB43:edb1:8e4:3588:e546:35ca");
    BOOST_CHECK(addr1 == addr2);
    BOOST_CHECK(addr1.IsTor());
    BOOST_CHECK(addr1.ToStringIP() == "5wyqrzbvrdsumnok.onion");
    BOOST_CHECK(addr1.IsRoutable());
}

// ============================================================================
// Protocol version constants — must remain stable across builds
// ============================================================================

BOOST_AUTO_TEST_CASE(protocol_version_constants)
{
    BOOST_CHECK_EQUAL(PROTOCOL_VERSION, 60019);
    BOOST_CHECK_EQUAL(INIT_PROTO_VERSION, 209);
    BOOST_CHECK_EQUAL(MIN_PEER_PROTO_VERSION, 60018);
    BOOST_CHECK_EQUAL(CADDR_TIME_VERSION, 31402);
    BOOST_CHECK_EQUAL(BIP0031_VERSION, 60000);
    BOOST_CHECK_EQUAL(MEMPOOL_GD_VERSION, 60002);
    BOOST_CHECK_EQUAL(NOBLKS_VERSION_START, 60002);
    BOOST_CHECK_EQUAL(NOBLKS_VERSION_END, 60014);
    BOOST_CHECK_EQUAL(DATABASE_VERSION, 70509);
}

BOOST_AUTO_TEST_CASE(protocol_default_port)
{
    BOOST_CHECK_EQUAL(GetDefaultPort(false), 9134);   // mainnet
    BOOST_CHECK_EQUAL(GetDefaultPort(true), 19134);    // testnet
}

BOOST_AUTO_TEST_CASE(protocol_node_network_flag)
{
    BOOST_CHECK_EQUAL(NODE_NETWORK, 1u);
}

// ============================================================================
// CNetAddr extended tests
// ============================================================================

BOOST_AUTO_TEST_CASE(netbase_ipv4_mapped)
{
    // IPv4-mapped IPv6 addresses should be detected as IPv4
    CNetAddr mapped("::FFFF:10.0.0.1");
    BOOST_CHECK(mapped.IsIPv4());
    BOOST_CHECK(mapped.IsRFC1918());

    CNetAddr mapped2("::FFFF:8.8.8.8");
    BOOST_CHECK(mapped2.IsIPv4());
    BOOST_CHECK(mapped2.IsRoutable());
}

BOOST_AUTO_TEST_CASE(netbase_non_routable)
{
    // Private, local, and documentation addresses are not routable
    BOOST_CHECK(!CNetAddr("10.0.0.1").IsRoutable());
    BOOST_CHECK(!CNetAddr("192.168.1.1").IsRoutable());
    BOOST_CHECK(!CNetAddr("172.16.0.1").IsRoutable());
    BOOST_CHECK(!CNetAddr("127.0.0.1").IsRoutable());
    BOOST_CHECK(!CNetAddr("::1").IsRoutable());
    BOOST_CHECK(!CNetAddr("2001:0DB8::1").IsRoutable());  // documentation range
}

BOOST_AUTO_TEST_CASE(netbase_multicast)
{
    BOOST_CHECK(CNetAddr("FF02::1").IsMulticast());
    BOOST_CHECK(!CNetAddr("8.8.8.8").IsMulticast());
}

BOOST_AUTO_TEST_CASE(netbase_address_groups)
{
    // Same /16 should produce same group
    vector<unsigned char> g1 = CNetAddr("1.2.3.4").GetGroup();
    vector<unsigned char> g2 = CNetAddr("1.2.4.5").GetGroup();
    BOOST_CHECK(g1 == g2);

    // Different /16 should produce different group
    vector<unsigned char> g3 = CNetAddr("1.3.3.4").GetGroup();
    BOOST_CHECK(g1 != g3);

    // IPv6 addresses get different groups from IPv4
    vector<unsigned char> g4 = CNetAddr("2001::1").GetGroup();
    BOOST_CHECK(g1 != g4);

    // Deterministic — same input always same group
    vector<unsigned char> g1b = CNetAddr("1.2.3.4").GetGroup();
    BOOST_CHECK(g1 == g1b);
}

BOOST_AUTO_TEST_CASE(netbase_tostring)
{
    BOOST_CHECK_EQUAL(CNetAddr("127.0.0.1").ToStringIP(), "127.0.0.1");
    BOOST_CHECK_EQUAL(CNetAddr("::1").ToStringIP(), "::1");
}

// ============================================================================
// CService tests
// ============================================================================

BOOST_AUTO_TEST_CASE(netbase_cservice_construct)
{
    CService svc;
    LookupNumeric("1.2.3.4", svc, 8333);
    BOOST_CHECK_EQUAL(svc.GetPort(), 8333);
    BOOST_CHECK_EQUAL(svc.ToString(), "1.2.3.4:8333");
}

BOOST_AUTO_TEST_CASE(netbase_cservice_tostring)
{
    CService svc;
    LookupNumeric("192.168.1.1", svc, 9134);
    BOOST_CHECK_EQUAL(svc.ToStringIPPort(), "192.168.1.1:9134");

    // IPv6
    CService svc6;
    LookupNumeric("::1", svc6, 9134);
    BOOST_CHECK_EQUAL(svc6.ToStringIPPort(), "[::1]:9134");
}

BOOST_AUTO_TEST_CASE(netbase_cservice_comparison)
{
    CService a, b, c;
    LookupNumeric("1.2.3.4", a, 100);
    LookupNumeric("1.2.3.4", b, 200);
    LookupNumeric("1.2.3.5", c, 100);

    BOOST_CHECK(a == a);
    BOOST_CHECK(a != b);  // different port
    BOOST_CHECK(a != c);  // different IP
    BOOST_CHECK(a < b);   // same IP, lower port
}

BOOST_AUTO_TEST_CASE(netbase_cservice_serialize_roundtrip)
{
    CService original;
    LookupNumeric("8.8.4.4", original, 9134);

    CDataStream ss(SER_DISK, CLIENT_VERSION);
    ss << original;

    CService deserialized;
    ss >> deserialized;

    BOOST_CHECK(original == deserialized);
    BOOST_CHECK_EQUAL(deserialized.GetPort(), 9134);
    BOOST_CHECK_EQUAL(deserialized.ToString(), "8.8.4.4:9134");
}

// ============================================================================
// CAddress tests
// ============================================================================

BOOST_AUTO_TEST_CASE(protocol_caddress_init)
{
    CAddress addr;
    BOOST_CHECK_EQUAL(addr.nServices, NODE_NETWORK);
    BOOST_CHECK_EQUAL(addr.nTime, 100000000u);
    BOOST_CHECK_EQUAL(addr.nLastTry, 0);
}

BOOST_AUTO_TEST_CASE(protocol_caddress_serialize_disk)
{
    CService svc;
    LookupNumeric("8.8.8.8", svc, 9134);
    CAddress original(svc, NODE_NETWORK);
    original.nTime = 1700000000;

    CDataStream ss(SER_DISK, CLIENT_VERSION);
    ss << original;

    CAddress deserialized;
    ss >> deserialized;

    BOOST_CHECK_EQUAL(deserialized.nServices, NODE_NETWORK);
    BOOST_CHECK_EQUAL(deserialized.nTime, 1700000000u);
    BOOST_CHECK_EQUAL(deserialized.GetPort(), 9134);
}

// ============================================================================
// CInv tests
// ============================================================================

BOOST_AUTO_TEST_CASE(protocol_cinv_types)
{
    CInv invTx(1, uint256(0));
    CInv invBlock(2, uint256(0));

    BOOST_CHECK(invTx.IsKnownType());
    BOOST_CHECK(invBlock.IsKnownType());
    BOOST_CHECK_EQUAL(string(invTx.GetCommand()), "tx");
    BOOST_CHECK_EQUAL(string(invBlock.GetCommand()), "block");

    // Type 0 is "ERROR" / unknown
    CInv invErr(0, uint256(0));
    BOOST_CHECK(!invErr.IsKnownType());
}

BOOST_AUTO_TEST_CASE(protocol_cinv_string_construct)
{
    uint256 hash("0xdeadbeef");
    CInv invTx("tx", hash);
    BOOST_CHECK_EQUAL(invTx.type, 1);
    BOOST_CHECK(invTx.hash == hash);

    CInv invBlock("block", hash);
    BOOST_CHECK_EQUAL(invBlock.type, 2);
}

BOOST_AUTO_TEST_CASE(protocol_cinv_invalid_type_throws)
{
    BOOST_CHECK_THROW(CInv("bogus", uint256(0)), std::out_of_range);
}

BOOST_AUTO_TEST_CASE(protocol_cinv_serialize_roundtrip)
{
    uint256 hash("0xaabbccdd");
    CInv original(1, hash);

    CDataStream ss(SER_DISK, CLIENT_VERSION);
    ss << original;

    CInv deserialized;
    ss >> deserialized;

    BOOST_CHECK_EQUAL(deserialized.type, 1);
    BOOST_CHECK(deserialized.hash == hash);
}

BOOST_AUTO_TEST_CASE(protocol_cinv_comparison)
{
    uint256 h1("0x1111");
    uint256 h2("0x2222");

    CInv a(1, h1);  // tx, low hash
    CInv b(1, h2);  // tx, high hash
    CInv c(2, h1);  // block, low hash

    // Type takes priority over hash
    BOOST_CHECK(a < c);   // tx < block
    BOOST_CHECK(a < b);   // same type, lower hash
    BOOST_CHECK(!(c < a));
}

// ============================================================================
// CMessageHeader tests
// ============================================================================

BOOST_AUTO_TEST_CASE(protocol_message_header_constants)
{
    BOOST_CHECK_EQUAL(CMessageHeader::HEADER_SIZE, 24u);
    BOOST_CHECK_EQUAL(CMessageHeader::COMMAND_SIZE, 12u);
    BOOST_CHECK_EQUAL(CMessageHeader::MESSAGE_START_SIZE, 4u);
    BOOST_CHECK_EQUAL(CMessageHeader::CHECKSUM_SIZE, 4u);
}

BOOST_AUTO_TEST_CASE(protocol_message_header_valid)
{
    // Default constructor uses global pchMessageStart, should be valid
    CMessageHeader hdr("version", 100);
    BOOST_CHECK(hdr.IsValid());
    BOOST_CHECK_EQUAL(hdr.GetCommand(), "version");
    BOOST_CHECK_EQUAL(hdr.nMessageSize, 100u);
}

BOOST_AUTO_TEST_CASE(protocol_message_header_getcommand)
{
    CMessageHeader hdr1("tx", 0);
    BOOST_CHECK_EQUAL(hdr1.GetCommand(), "tx");

    CMessageHeader hdr2("block", 0);
    BOOST_CHECK_EQUAL(hdr2.GetCommand(), "block");

    CMessageHeader hdr3("ping", 0);
    BOOST_CHECK_EQUAL(hdr3.GetCommand(), "ping");
}

// ============================================================================
// ReceiveFloodSize / SendBufferSize tests
// ============================================================================

BOOST_AUTO_TEST_CASE(receive_flood_size_default)
{
    // Ensure clean state for both buffer args
    mapArgs.erase("-maxreceivebuffer");
    mapArgs.erase("-maxsendbuffer");
    // Default: 5000 * 1000 = 5,000,000
    BOOST_CHECK_EQUAL(ReceiveFloodSize(), 5000u * 1000u);
}

BOOST_AUTO_TEST_CASE(send_buffer_size_default)
{
    // Ensure clean state for both buffer args
    mapArgs.erase("-maxreceivebuffer");
    mapArgs.erase("-maxsendbuffer");
    // Default: 1000 * 1000 = 1,000,000
    BOOST_CHECK_EQUAL(SendBufferSize(), 1000u * 1000u);
}

BOOST_AUTO_TEST_CASE(receive_flood_size_custom)
{
    mapArgs["-maxreceivebuffer"] = "10000";
    BOOST_CHECK_EQUAL(ReceiveFloodSize(), 10000u * 1000u);
    mapArgs.erase("-maxreceivebuffer"); // cleanup
}

BOOST_AUTO_TEST_SUITE_END()
