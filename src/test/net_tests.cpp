// Copyright (c) 2024 The Pinkcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

//
// Tier D: Network layer tests
//
// Tests for net.h/net.cpp, netbase.cpp (ParseNetwork), and protocol constants
// that are NOT already covered by netbase_tests.cpp or addrman_tests.cpp.
//
// Covers: ParseNetwork, CNetMessage state machine, CNode construction/properties,
// inventory relay, address relay, ban list, byte counters, SetLimited/IsLimited,
// SetReachable/IsReachable, CNetAddr::GetByte/GetHash/GetReachabilityFrom.
//

#include <boost/test/unit_test.hpp>

#include <string>
#include <vector>

#include "net.h"
#include "netbase.h"
#include "protocol.h"
#include "serialize.h"
#include "version.h"
#include "util.h"

using namespace std;

BOOST_AUTO_TEST_SUITE(net_tests)

// ============================================================================
// ParseNetwork — case-insensitive network name parsing (netbase.cpp)
// ============================================================================

BOOST_AUTO_TEST_CASE(parse_network_ipv4)
{
    BOOST_CHECK_EQUAL(ParseNetwork("ipv4"), NET_IPV4);
}

BOOST_AUTO_TEST_CASE(parse_network_ipv6)
{
    BOOST_CHECK_EQUAL(ParseNetwork("ipv6"), NET_IPV6);
}

BOOST_AUTO_TEST_CASE(parse_network_tor)
{
    BOOST_CHECK_EQUAL(ParseNetwork("tor"), NET_TOR);
}

BOOST_AUTO_TEST_CASE(parse_network_i2p)
{
    BOOST_CHECK_EQUAL(ParseNetwork("i2p"), NET_I2P);
}

BOOST_AUTO_TEST_CASE(parse_network_unknown_returns_unroutable)
{
    BOOST_CHECK_EQUAL(ParseNetwork("bogus"), NET_UNROUTABLE);
    BOOST_CHECK_EQUAL(ParseNetwork(""), NET_UNROUTABLE);
}

BOOST_AUTO_TEST_CASE(parse_network_case_insensitive)
{
    BOOST_CHECK_EQUAL(ParseNetwork("IPV4"), NET_IPV4);
    BOOST_CHECK_EQUAL(ParseNetwork("Ipv6"), NET_IPV6);
    BOOST_CHECK_EQUAL(ParseNetwork("TOR"), NET_TOR);
    BOOST_CHECK_EQUAL(ParseNetwork("I2P"), NET_I2P);
}

// ============================================================================
// Net constants — pinned values from net.h
// ============================================================================

BOOST_AUTO_TEST_CASE(net_timing_constants)
{
    BOOST_CHECK_EQUAL(PING_INTERVAL, 120);     // 2 * 60
    BOOST_CHECK_EQUAL(TIMEOUT_INTERVAL, 1200); // 20 * 60
}

BOOST_AUTO_TEST_CASE(net_local_score_enum)
{
    BOOST_CHECK_EQUAL(LOCAL_NONE, 0);
    BOOST_CHECK_EQUAL(LOCAL_IF, 1);
    BOOST_CHECK_EQUAL(LOCAL_BIND, 2);
    BOOST_CHECK_EQUAL(LOCAL_UPNP, 3);
    BOOST_CHECK_EQUAL(LOCAL_HTTP, 4);
    BOOST_CHECK_EQUAL(LOCAL_MANUAL, 5);
    BOOST_CHECK_EQUAL(LOCAL_MAX, 6);
}

BOOST_AUTO_TEST_CASE(net_thread_id_enum)
{
    BOOST_CHECK_EQUAL(THREAD_SOCKETHANDLER, 0);
    BOOST_CHECK_EQUAL(THREAD_OPENCONNECTIONS, 1);
    BOOST_CHECK_EQUAL(THREAD_MESSAGEHANDLER, 2);
    BOOST_CHECK_EQUAL(THREAD_RPCLISTENER, 3);
    BOOST_CHECK_EQUAL(THREAD_UPNP, 4);
    BOOST_CHECK_EQUAL(THREAD_DNSSEED, 5);
    BOOST_CHECK_EQUAL(THREAD_ADDEDCONNECTIONS, 6);
    BOOST_CHECK_EQUAL(THREAD_DUMPADDRESS, 7);
    BOOST_CHECK_EQUAL(THREAD_RPCHANDLER, 8);
    BOOST_CHECK_EQUAL(THREAD_STAKE_MINER, 9);
    BOOST_CHECK_EQUAL(THREAD_MAX, 10);
}

// ============================================================================
// CNetMessage — message parsing state machine (net.h / net.cpp)
// ============================================================================

BOOST_AUTO_TEST_CASE(netmessage_initial_state)
{
    CNetMessage msg(SER_NETWORK, INIT_PROTO_VERSION);
    BOOST_CHECK_EQUAL(msg.in_data, false);
    BOOST_CHECK_EQUAL(msg.nHdrPos, 0u);
    BOOST_CHECK_EQUAL(msg.nDataPos, 0u);
    BOOST_CHECK_EQUAL(msg.nTime, 0);
    BOOST_CHECK(!msg.complete());
}

BOOST_AUTO_TEST_CASE(netmessage_read_header_partial)
{
    CNetMessage msg(SER_NETWORK, INIT_PROTO_VERSION);

    // Feed only 10 bytes — header is 24 bytes, stays in header mode
    char partial[10] = {};
    int handled = msg.readHeader(partial, 10);

    BOOST_CHECK_EQUAL(handled, 10);
    BOOST_CHECK_EQUAL(msg.nHdrPos, 10u);
    BOOST_CHECK(!msg.in_data);
    BOOST_CHECK(!msg.complete());
}

BOOST_AUTO_TEST_CASE(netmessage_read_header_two_chunks)
{
    CNetMessage msg(SER_NETWORK, INIT_PROTO_VERSION);

    // Serialize a valid header for "ping" with 0-byte payload
    CMessageHeader hdr("ping", 0);
    CDataStream ss(SER_NETWORK, INIT_PROTO_VERSION);
    ss << hdr;
    string raw = ss.str();
    BOOST_REQUIRE_EQUAL(raw.size(), 24u);

    // First chunk: 10 bytes
    int h1 = msg.readHeader(raw.data(), 10);
    BOOST_CHECK_EQUAL(h1, 10);
    BOOST_CHECK_EQUAL(msg.nHdrPos, 10u);
    BOOST_CHECK(!msg.in_data);

    // Second chunk: remaining 14 bytes
    int h2 = msg.readHeader(raw.data() + 10, 14);
    BOOST_CHECK_EQUAL(h2, 14);
    BOOST_CHECK_EQUAL(msg.nHdrPos, 24u);
    BOOST_CHECK(msg.in_data);
    BOOST_CHECK_EQUAL(msg.hdr.GetCommand(), "ping");
    BOOST_CHECK(msg.complete());  // 0-byte payload → complete
}

BOOST_AUTO_TEST_CASE(netmessage_read_header_full_zero_payload)
{
    CNetMessage msg(SER_NETWORK, INIT_PROTO_VERSION);

    CMessageHeader hdr("ping", 0);
    CDataStream ss(SER_NETWORK, INIT_PROTO_VERSION);
    ss << hdr;
    string raw = ss.str();

    int handled = msg.readHeader(raw.data(), raw.size());
    BOOST_CHECK_EQUAL(handled, 24);
    BOOST_CHECK(msg.in_data);
    BOOST_CHECK_EQUAL(msg.hdr.nMessageSize, 0u);
    BOOST_CHECK(msg.complete());
}

BOOST_AUTO_TEST_CASE(netmessage_read_header_with_payload)
{
    CNetMessage msg(SER_NETWORK, INIT_PROTO_VERSION);

    CMessageHeader hdr("version", 100);
    CDataStream ss(SER_NETWORK, INIT_PROTO_VERSION);
    ss << hdr;
    string raw = ss.str();

    int handled = msg.readHeader(raw.data(), raw.size());
    BOOST_CHECK_EQUAL(handled, 24);
    BOOST_CHECK(msg.in_data);
    BOOST_CHECK_EQUAL(msg.hdr.nMessageSize, 100u);
    BOOST_CHECK(!msg.complete());  // need 100 bytes of data
}

BOOST_AUTO_TEST_CASE(netmessage_read_header_oversized_rejected)
{
    CNetMessage msg(SER_NETWORK, INIT_PROTO_VERSION);

    // MAX_SIZE + 1 exceeds the 32MB limit → readHeader returns -1
    CMessageHeader hdr("ping", MAX_SIZE + 1);
    CDataStream ss(SER_NETWORK, INIT_PROTO_VERSION);
    ss << hdr;
    string raw = ss.str();

    int handled = msg.readHeader(raw.data(), raw.size());
    BOOST_CHECK_EQUAL(handled, -1);
}

BOOST_AUTO_TEST_CASE(netmessage_read_data_accumulates)
{
    CNetMessage msg(SER_NETWORK, INIT_PROTO_VERSION);

    // Set up header for 20-byte payload
    CMessageHeader hdr("tx", 20);
    CDataStream ss(SER_NETWORK, INIT_PROTO_VERSION);
    ss << hdr;
    string raw = ss.str();
    msg.readHeader(raw.data(), raw.size());
    BOOST_REQUIRE(msg.in_data);

    // Partial read: 10 of 20 bytes
    char data1[10] = {};
    BOOST_CHECK_EQUAL(msg.readData(data1, 10), 10);
    BOOST_CHECK_EQUAL(msg.nDataPos, 10u);
    BOOST_CHECK(!msg.complete());

    // Complete read: remaining 10 bytes
    char data2[10] = {};
    BOOST_CHECK_EQUAL(msg.readData(data2, 10), 10);
    BOOST_CHECK_EQUAL(msg.nDataPos, 20u);
    BOOST_CHECK(msg.complete());
}

BOOST_AUTO_TEST_CASE(netmessage_read_data_excess_capped)
{
    CNetMessage msg(SER_NETWORK, INIT_PROTO_VERSION);

    // Header says 5 bytes of data
    CMessageHeader hdr("ping", 5);
    CDataStream ss(SER_NETWORK, INIT_PROTO_VERSION);
    ss << hdr;
    string raw = ss.str();
    msg.readHeader(raw.data(), raw.size());

    // Offer 20 bytes — only 5 should be consumed
    char data[20] = {};
    BOOST_CHECK_EQUAL(msg.readData(data, 20), 5);
    BOOST_CHECK_EQUAL(msg.nDataPos, 5u);
    BOOST_CHECK(msg.complete());
}

// ============================================================================
// CNode::ReceiveMsgBytes — higher-level message assembly
// ============================================================================

BOOST_AUTO_TEST_CASE(receive_msg_bytes_complete_message)
{
    // CNode with INVALID_SOCKET + inbound=true skips PushVersion
    CAddress addr(CService("1.2.3.4", 9134));
    CNode node(INVALID_SOCKET, addr, "", true);

    // Build complete message: 24-byte header + 4-byte payload
    CMessageHeader hdr("ping", 4);
    CDataStream ss(SER_NETWORK, INIT_PROTO_VERSION);
    ss << hdr;
    char payload[4] = {0x01, 0x02, 0x03, 0x04};
    ss.write(payload, 4);
    string raw = ss.str();
    BOOST_REQUIRE_EQUAL(raw.size(), 28u);

    {
        LOCK(node.cs_vRecvMsg);
        bool ok = node.ReceiveMsgBytes(raw.data(), raw.size());
        BOOST_CHECK(ok);
        BOOST_CHECK_EQUAL(node.vRecvMsg.size(), 1u);
        BOOST_CHECK(node.vRecvMsg.back().complete());
        BOOST_CHECK_EQUAL(node.vRecvMsg.back().hdr.GetCommand(), "ping");
    }
}

BOOST_AUTO_TEST_CASE(receive_msg_bytes_oversized_rejected)
{
    CAddress addr(CService("1.2.3.4", 9134));
    CNode node(INVALID_SOCKET, addr, "", true);

    CMessageHeader hdr("ping", MAX_SIZE + 1);
    CDataStream ss(SER_NETWORK, INIT_PROTO_VERSION);
    ss << hdr;
    string raw = ss.str();

    {
        LOCK(node.cs_vRecvMsg);
        bool ok = node.ReceiveMsgBytes(raw.data(), raw.size());
        BOOST_CHECK(!ok);
    }
}

// ============================================================================
// CNode construction and properties
// ============================================================================

BOOST_AUTO_TEST_CASE(cnode_initial_state)
{
    CAddress addr(CService("1.2.3.4", 9134));
    CNode node(INVALID_SOCKET, addr, "", true);

    BOOST_CHECK_EQUAL(node.nServices, 0u);
    BOOST_CHECK_EQUAL(node.hSocket, INVALID_SOCKET);
    BOOST_CHECK_EQUAL(node.nVersion, 0);
    BOOST_CHECK_EQUAL(node.strSubVer, "");
    BOOST_CHECK_EQUAL(node.fInbound, true);
    BOOST_CHECK_EQUAL(node.fOneShot, false);
    BOOST_CHECK_EQUAL(node.fClient, false);
    BOOST_CHECK_EQUAL(node.fNetworkNode, false);
    BOOST_CHECK_EQUAL(node.fSuccessfullyConnected, false);
    BOOST_CHECK_EQUAL(node.fDisconnect, false);
    BOOST_CHECK_EQUAL(node.nStartingHeight, -1);
    BOOST_CHECK_EQUAL(node.nPingNonceSent, 0u);
    BOOST_CHECK_EQUAL(node.nPingUsecStart, 0);
    BOOST_CHECK_EQUAL(node.nPingUsecTime, 0);
    BOOST_CHECK_EQUAL(node.fPingQueued, false);
    BOOST_CHECK_EQUAL(node.nSendBytes, 0u);
    BOOST_CHECK_EQUAL(node.nRecvBytes, 0u);
    BOOST_CHECK_EQUAL(node.GetRefCount(), 0);
    // Empty addrName defaults to addr.ToStringIPPort()
    BOOST_CHECK_EQUAL(node.addrName, "1.2.3.4:9134");
}

BOOST_AUTO_TEST_CASE(cnode_custom_addrname)
{
    CAddress addr(CService("1.2.3.4", 9134));
    CNode node(INVALID_SOCKET, addr, "custom-peer", true);

    BOOST_CHECK_EQUAL(node.addrName, "custom-peer");
}

BOOST_AUTO_TEST_CASE(cnode_addref_release)
{
    CAddress addr(CService("1.2.3.4", 9134));
    CNode node(INVALID_SOCKET, addr, "", true);

    BOOST_CHECK_EQUAL(node.GetRefCount(), 0);
    node.AddRef();
    BOOST_CHECK_EQUAL(node.GetRefCount(), 1);
    node.AddRef();
    BOOST_CHECK_EQUAL(node.GetRefCount(), 2);
    node.Release();
    BOOST_CHECK_EQUAL(node.GetRefCount(), 1);
    node.Release();
    BOOST_CHECK_EQUAL(node.GetRefCount(), 0);
}

BOOST_AUTO_TEST_CASE(cnode_copystats)
{
    CAddress addr(CService("1.2.3.4", 9134));
    CNode node(INVALID_SOCKET, addr, "test-peer", true);

    node.nServices = 1;
    node.nVersion = 60019;
    node.strSubVer = "/Pink2:2.4.0/";
    node.nStartingHeight = 500000;

    CNodeStats stats;
    node.copyStats(stats);

    BOOST_CHECK_EQUAL(stats.nServices, 1u);
    BOOST_CHECK_EQUAL(stats.addrName, "test-peer");
    BOOST_CHECK_EQUAL(stats.nVersion, 60019);
    BOOST_CHECK_EQUAL(stats.strSubVer, "/Pink2:2.4.0/");
    BOOST_CHECK_EQUAL(stats.fInbound, true);
    BOOST_CHECK_EQUAL(stats.nStartingHeight, 500000);
    BOOST_CHECK_EQUAL(stats.nMisbehavior, 0);
    // No ping in flight → dPingWait and dPingTime are 0
    BOOST_CHECK_EQUAL(stats.dPingWait, 0.0);
    BOOST_CHECK_EQUAL(stats.dPingTime, 0.0);
}

// ============================================================================
// CNode inventory management
// ============================================================================

BOOST_AUTO_TEST_CASE(cnode_add_inventory_known)
{
    CAddress addr(CService("1.2.3.4", 9134));
    CNode node(INVALID_SOCKET, addr, "", true);

    CInv inv(MSG_TX, uint256("0xaabb"));

    {
        LOCK(node.cs_inventory);
        BOOST_CHECK_EQUAL(node.setInventoryKnown.count(inv), 0u);
    }

    node.AddInventoryKnown(inv);

    {
        LOCK(node.cs_inventory);
        BOOST_CHECK_EQUAL(node.setInventoryKnown.count(inv), 1u);
    }
}

BOOST_AUTO_TEST_CASE(cnode_push_inventory_dedup)
{
    CAddress addr(CService("1.2.3.4", 9134));
    CNode node(INVALID_SOCKET, addr, "", true);

    CInv inv1(MSG_TX, uint256("0x1111"));
    CInv inv2(MSG_TX, uint256("0x2222"));

    // Push two different inventories
    node.PushInventory(inv1);
    node.PushInventory(inv2);
    {
        LOCK(node.cs_inventory);
        BOOST_CHECK_EQUAL(node.vInventoryToSend.size(), 2u);
    }

    // Mark inv1 as known, then push again — should be suppressed
    node.AddInventoryKnown(inv1);
    node.PushInventory(inv1);
    {
        LOCK(node.cs_inventory);
        BOOST_CHECK_EQUAL(node.vInventoryToSend.size(), 2u);  // still 2
    }
}

// ============================================================================
// CNode address relay
// ============================================================================

BOOST_AUTO_TEST_CASE(cnode_push_address_valid)
{
    CAddress addr(CService("1.2.3.4", 9134));
    CNode node(INVALID_SOCKET, addr, "", true);

    CAddress pushAddr(CService("5.6.7.8", 9134));
    node.PushAddress(pushAddr);
    BOOST_CHECK_EQUAL(node.vAddrToSend.size(), 1u);
}

BOOST_AUTO_TEST_CASE(cnode_push_address_known_filtered)
{
    CAddress addr(CService("1.2.3.4", 9134));
    CNode node(INVALID_SOCKET, addr, "", true);

    CAddress pushAddr(CService("5.6.7.8", 9134));
    node.AddAddressKnown(pushAddr);
    node.PushAddress(pushAddr);
    BOOST_CHECK_EQUAL(node.vAddrToSend.size(), 0u);  // filtered out
}

BOOST_AUTO_TEST_CASE(cnode_push_address_invalid_rejected)
{
    CAddress addr(CService("1.2.3.4", 9134));
    CNode node(INVALID_SOCKET, addr, "", true);

    // Default CAddress is 0.0.0.0:0 — IsValid() returns false
    CAddress invalidAddr;
    node.PushAddress(invalidAddr);
    BOOST_CHECK_EQUAL(node.vAddrToSend.size(), 0u);
}

// ============================================================================
// Ban list — CNode static methods
// ============================================================================

BOOST_AUTO_TEST_CASE(ban_list_initially_empty)
{
    CNode::ClearBanned();
    BOOST_CHECK(!CNode::IsBanned(CNetAddr("1.2.3.4")));
    BOOST_CHECK(!CNode::IsBanned(CNetAddr("5.6.7.8")));
}

BOOST_AUTO_TEST_CASE(ban_list_misbehaving_below_threshold)
{
    CNode::ClearBanned();
    CAddress addr(CService("3.4.5.6", 9134));
    CNode node(INVALID_SOCKET, addr, "", true);

    // 50 < 100 (default banscore) → not banned
    bool banned = node.Misbehaving(50);
    BOOST_CHECK(!banned);
    BOOST_CHECK(!CNode::IsBanned(CNetAddr("3.4.5.6")));

    CNode::ClearBanned();
}

BOOST_AUTO_TEST_CASE(ban_list_misbehaving_at_threshold)
{
    CNode::ClearBanned();
    CAddress addr(CService("4.5.6.7", 9134));
    CNode node(INVALID_SOCKET, addr, "", true);

    // 100 >= 100 → banned
    bool banned = node.Misbehaving(100);
    BOOST_CHECK(banned);
    BOOST_CHECK(CNode::IsBanned(CNetAddr("4.5.6.7")));

    CNode::ClearBanned();
}

BOOST_AUTO_TEST_CASE(ban_list_misbehaving_cumulative)
{
    CNode::ClearBanned();
    CAddress addr(CService("5.6.7.8", 9134));
    CNode node(INVALID_SOCKET, addr, "", true);

    // First offense: 50 → total 50, below threshold
    BOOST_CHECK(!node.Misbehaving(50));
    BOOST_CHECK(!CNode::IsBanned(CNetAddr("5.6.7.8")));

    // Second offense: 50 → total 100, at threshold → banned
    BOOST_CHECK(node.Misbehaving(50));
    BOOST_CHECK(CNode::IsBanned(CNetAddr("5.6.7.8")));

    CNode::ClearBanned();
}

BOOST_AUTO_TEST_CASE(ban_list_local_node_exempt)
{
    CNode::ClearBanned();
    CAddress addr(CService("127.0.0.1", 9134));
    CNode node(INVALID_SOCKET, addr, "", true);

    // Local nodes always return false from Misbehaving, never banned
    bool banned = node.Misbehaving(200);
    BOOST_CHECK(!banned);
    BOOST_CHECK(!CNode::IsBanned(CNetAddr("127.0.0.1")));

    CNode::ClearBanned();
}

// ============================================================================
// Static byte counters
// ============================================================================

BOOST_AUTO_TEST_CASE(byte_counters_recv)
{
    // Counters accumulate globally — measure delta
    uint64_t before = CNode::GetTotalBytesRecv();
    CNode::RecordBytesRecv(100);
    CNode::RecordBytesRecv(200);
    uint64_t after = CNode::GetTotalBytesRecv();
    BOOST_CHECK_EQUAL(after - before, 300u);
}

BOOST_AUTO_TEST_CASE(byte_counters_sent)
{
    uint64_t before = CNode::GetTotalBytesSent();
    CNode::RecordBytesSent(500);
    CNode::RecordBytesSent(300);
    uint64_t after = CNode::GetTotalBytesSent();
    BOOST_CHECK_EQUAL(after - before, 800u);
}

// ============================================================================
// SetLimited / IsLimited — per-network connection limiting
// ============================================================================

BOOST_AUTO_TEST_CASE(limited_set_and_check)
{
    // Ensure clean start
    SetLimited(NET_IPV4, false);
    BOOST_CHECK(!IsLimited(NET_IPV4));

    SetLimited(NET_IPV4, true);
    BOOST_CHECK(IsLimited(NET_IPV4));

    // Cleanup
    SetLimited(NET_IPV4, false);
    BOOST_CHECK(!IsLimited(NET_IPV4));
}

BOOST_AUTO_TEST_CASE(limited_unroutable_ignored)
{
    // SetLimited guards against NET_UNROUTABLE — no-op
    SetLimited(NET_UNROUTABLE, true);
    BOOST_CHECK(!IsLimited(NET_UNROUTABLE));
}

BOOST_AUTO_TEST_CASE(limited_per_network)
{
    SetLimited(NET_IPV4, false);
    SetLimited(NET_IPV6, false);
    SetLimited(NET_TOR, false);

    // Limiting one network doesn't affect others
    SetLimited(NET_TOR, true);
    BOOST_CHECK(!IsLimited(NET_IPV4));
    BOOST_CHECK(!IsLimited(NET_IPV6));
    BOOST_CHECK(IsLimited(NET_TOR));

    SetLimited(NET_TOR, false);
}

BOOST_AUTO_TEST_CASE(limited_addr_overload)
{
    // IsLimited(CNetAddr) delegates to IsLimited(addr.GetNetwork())
    SetLimited(NET_IPV4, true);

    // 8.8.8.8 is routable IPv4 → GetNetwork() returns NET_IPV4
    BOOST_CHECK(IsLimited(CNetAddr("8.8.8.8")));

    SetLimited(NET_IPV4, false);
}

// ============================================================================
// SetReachable / IsReachable — network reachability
// ============================================================================

BOOST_AUTO_TEST_CASE(reachable_default_unreachable)
{
    // vfReachable[] starts all false → nothing is reachable
    SetReachable(NET_IPV4, false);
    SetLimited(NET_IPV4, false);
    BOOST_CHECK(!IsReachable(CNetAddr("8.8.8.8")));
}

BOOST_AUTO_TEST_CASE(reachable_set_and_check)
{
    SetReachable(NET_IPV4, true);
    SetLimited(NET_IPV4, false);
    BOOST_CHECK(IsReachable(CNetAddr("8.8.8.8")));

    SetReachable(NET_IPV4, false);
}

BOOST_AUTO_TEST_CASE(reachable_limited_overrides)
{
    // Reachable but limited → net is NOT reachable
    SetReachable(NET_IPV4, true);
    SetLimited(NET_IPV4, true);
    BOOST_CHECK(!IsReachable(CNetAddr("8.8.8.8")));

    SetReachable(NET_IPV4, false);
    SetLimited(NET_IPV4, false);
}

BOOST_AUTO_TEST_CASE(reachable_ipv6_sets_ipv4_reachable)
{
    SetReachable(NET_IPV4, false);
    SetReachable(NET_IPV6, false);

    // SetReachable(NET_IPV6, true) also sets vfReachable[NET_IPV4] = true
    SetReachable(NET_IPV6, true);
    SetLimited(NET_IPV4, false);

    BOOST_CHECK(IsReachable(CNetAddr("8.8.8.8")));

    // Cleanup
    SetReachable(NET_IPV4, false);
    SetReachable(NET_IPV6, false);
}

// ============================================================================
// CNetAddr additional — GetByte, GetHash, GetReachabilityFrom
// ============================================================================

BOOST_AUTO_TEST_CASE(netaddr_getbyte)
{
    CNetAddr addr("1.2.3.4");
    // IPv4-mapped: ip[12..15] = {1, 2, 3, 4}
    // GetByte(n) returns ip[15-n]
    BOOST_CHECK_EQUAL(addr.GetByte(0), 4u);
    BOOST_CHECK_EQUAL(addr.GetByte(1), 3u);
    BOOST_CHECK_EQUAL(addr.GetByte(2), 2u);
    BOOST_CHECK_EQUAL(addr.GetByte(3), 1u);
}

BOOST_AUTO_TEST_CASE(netaddr_gethash_deterministic)
{
    CNetAddr addr("8.8.8.8");
    uint64_t h1 = addr.GetHash();
    uint64_t h2 = addr.GetHash();
    BOOST_CHECK_EQUAL(h1, h2);

    // Different address → different hash (with overwhelming probability)
    CNetAddr addr2("8.8.4.4");
    BOOST_CHECK(h1 != addr2.GetHash());
}

BOOST_AUTO_TEST_CASE(netaddr_reachability_ipv4_to_ipv4)
{
    // Both routable IPv4 → REACH_IPV4 (4)
    CNetAddr us("8.8.8.8");
    CNetAddr partner("1.2.3.4");
    BOOST_CHECK_EQUAL(us.GetReachabilityFrom(&partner), 4);
}

BOOST_AUTO_TEST_CASE(netaddr_reachability_unroutable)
{
    // Non-routable address → REACH_UNREACHABLE (0)
    CNetAddr local("127.0.0.1");
    CNetAddr partner("1.2.3.4");
    BOOST_CHECK_EQUAL(local.GetReachabilityFrom(&partner), 0);
}

BOOST_AUTO_TEST_CASE(netaddr_reachability_from_nullptr)
{
    // nullptr partner → NET_UNKNOWN → default case
    // ourNet=NET_IPV4 → REACH_IPV4 (4)
    CNetAddr us("8.8.8.8");
    BOOST_CHECK_EQUAL(us.GetReachabilityFrom(nullptr), 4);
}

BOOST_AUTO_TEST_SUITE_END()
