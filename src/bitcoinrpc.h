// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2012 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef _BITCOINRPC_H_
#define _BITCOINRPC_H_ 1

#include <string>
#include <list>
#include <map>

class CBlockIndex;

#include "json/nlohmann/json.hpp"
#include "wallet.h"

#include "util.h"
#include "checkpoints.h"

using json = nlohmann::json;

// HTTP status codes
enum HTTPStatusCode
{
    HTTP_OK                    = 200,
    HTTP_BAD_REQUEST           = 400,
    HTTP_UNAUTHORIZED          = 401,
    HTTP_FORBIDDEN             = 403,
    HTTP_NOT_FOUND             = 404,
    HTTP_INTERNAL_SERVER_ERROR = 500,
};

// Bitcoin RPC error codes
enum RPCErrorCode
{
    // Standard JSON-RPC 2.0 errors
    RPC_INVALID_REQUEST  = -32600,
    RPC_METHOD_NOT_FOUND = -32601,
    RPC_INVALID_PARAMS   = -32602,
    RPC_INTERNAL_ERROR   = -32603,
    RPC_PARSE_ERROR      = -32700,

    // General application defined errors
    RPC_MISC_ERROR                  = -1,  // std::exception thrown in command handling
    RPC_FORBIDDEN_BY_SAFE_MODE      = -2,  // Server is in safe mode, and command is not allowed in safe mode
    RPC_TYPE_ERROR                  = -3,  // Unexpected type was passed as parameter
    RPC_INVALID_ADDRESS_OR_KEY      = -5,  // Invalid address or key
    RPC_OUT_OF_MEMORY               = -7,  // Ran out of memory during operation
    RPC_INVALID_PARAMETER           = -8,  // Invalid, missing or duplicate parameter
    RPC_DATABASE_ERROR              = -20, // Database error
    RPC_DESERIALIZATION_ERROR       = -22, // Error parsing or validating structure in raw format

    // P2P client errors
    RPC_CLIENT_NOT_CONNECTED        = -9,  // Bitcoin is not connected
    RPC_CLIENT_IN_INITIAL_DOWNLOAD  = -10, // Still downloading initial blocks

    // Wallet errors
    RPC_WALLET_ERROR                = -4,  // Unspecified problem with wallet (key not found etc.)
    RPC_WALLET_INSUFFICIENT_FUNDS   = -6,  // Not enough funds in wallet or account
    RPC_WALLET_INVALID_ACCOUNT_NAME = -11, // Invalid account name
    RPC_WALLET_KEYPOOL_RAN_OUT      = -12, // Keypool ran out, call keypoolrefill first
    RPC_WALLET_UNLOCK_NEEDED        = -13, // Enter the wallet passphrase with walletpassphrase first
    RPC_WALLET_PASSPHRASE_INCORRECT = -14, // The wallet passphrase entered was incorrect
    RPC_WALLET_WRONG_ENC_STATE      = -15, // Command given in wrong wallet encryption state (encrypting an encrypted wallet etc.)
    RPC_WALLET_ENCRYPTION_FAILED    = -16, // Failed to encrypt the wallet
    RPC_WALLET_ALREADY_UNLOCKED     = -17, // Wallet is already unlocked
};

json JSONRPCError(int code, const std::string& message);

void ThreadRPCServer();
int CommandLineRPC(int argc, char *argv[]);

/** Convert parameter values for RPC call from strings to command-specific JSON objects. */
json RPCConvertValues(const std::string &strMethod, const std::vector<std::string> &strParams);

/*
  Type-check arguments; throws JSONRPCError if wrong type given. Does not check that
  the right number of arguments are passed, just that any passed are the correct type.
  Use like:  RPCTypeCheck(params, {json::value_t::string, json::value_t::number_integer, json::value_t::object});
*/
void RPCTypeCheck(const json& params,
                  const std::list<json::value_t>& typesExpected, bool fAllowNull=false);
/*
  Check for expected keys/value types in an Object.
  Use like: RPCTypeCheck(object, {{"name", json::value_t::string}, {"value", json::value_t::number_integer}});
*/
void RPCTypeCheck(const json& o,
                  const std::map<std::string, json::value_t>& typesExpected, bool fAllowNull=false);

using rpcfn_type = json(*)(const json&, bool);

class CRPCCommand
{
public:
    std::string name;
    rpcfn_type actor;
    bool okSafeMode;
    bool unlocked;
};

/**
 * Bitcoin RPC command dispatcher.
 */
class CRPCTable
{
private:
    std::map<std::string, const CRPCCommand*> mapCommands;
public:
    CRPCTable();
    const CRPCCommand* operator[](std::string name) const;
    std::string help(std::string name) const;

    /**
     * Execute a method.
     * @param method   Method to execute
     * @param params   Array of arguments (JSON objects)
     * @returns Result of the call.
     * @throws an exception (json) when an error happens.
     */
    json execute(const std::string &method, const json &params) const;
};

extern const CRPCTable tableRPC;

extern int64_t nWalletUnlockTime;
extern int64_t AmountFromValue(const json& value);
extern json ValueFromAmount(int64_t amount);
extern double GetDifficulty(const CBlockIndex* blockindex = nullptr);

extern double GetPoWMHashPS();
extern double GetPoSKernelPS();

extern std::string HexBits(unsigned int nBits);
extern std::string HelpRequiringPassphrase();
extern void EnsureWalletIsUnlocked();

//
// Utilities: convert hex-encoded Values
// (throws error if not hex).
//
extern uint256 ParseHashV(const json& v, std::string strName);
extern uint256 ParseHashO(const json& o, std::string strKey);
extern std::vector<unsigned char> ParseHexV(const json& v, std::string strName);
extern std::vector<unsigned char> ParseHexO(const json& o, std::string strKey);

extern json getconnectioncount(const json& params, bool fHelp); // in rpcnet.cpp
extern json getpeerinfo(const json& params, bool fHelp);
extern json getnodes(const json& params, bool fHelp);
extern json dumpwallet(const json& params, bool fHelp);
extern json importwallet(const json& params, bool fHelp);
extern json dumpprivkey(const json& params, bool fHelp); // in rpcdump.cpp
extern json importprivkey(const json& params, bool fHelp);

extern json sendalert(const json& params, bool fHelp);

extern json getsubsidy(const json& params, bool fHelp);
extern json getmininginfo(const json& params, bool fHelp);
extern json getstakinginfo(const json& params, bool fHelp);
extern json getwork(const json& params, bool fHelp);
extern json getworkex(const json& params, bool fHelp);
extern json getblocktemplate(const json& params, bool fHelp);
extern json submitblock(const json& params, bool fHelp);

extern json getnewaddress(const json& params, bool fHelp); // in rpcwallet.cpp
extern json getaccountaddress(const json& params, bool fHelp);
extern json setaccount(const json& params, bool fHelp);
extern json getaccount(const json& params, bool fHelp);
extern json getaddressesbyaccount(const json& params, bool fHelp);
extern json sendtoaddress(const json& params, bool fHelp);
extern json signmessage(const json& params, bool fHelp);
extern json verifymessage(const json& params, bool fHelp);
extern json getreceivedbyaddress(const json& params, bool fHelp);
extern json getreceivedbyaccount(const json& params, bool fHelp);
extern json getbalance(const json& params, bool fHelp);
extern json movecmd(const json& params, bool fHelp);
extern json sendfrom(const json& params, bool fHelp);
extern json sendmany(const json& params, bool fHelp);
extern json addmultisigaddress(const json& params, bool fHelp);
extern json addredeemscript(const json& params, bool fHelp);
extern json listreceivedbyaddress(const json& params, bool fHelp);
extern json listreceivedbyaccount(const json& params, bool fHelp);
extern json listtransactions(const json& params, bool fHelp);
extern json listaddressgroupings(const json& params, bool fHelp);
extern json listaccounts(const json& params, bool fHelp);
extern json listsinceblock(const json& params, bool fHelp);
extern json gettransaction(const json& params, bool fHelp);
extern json backupwallet(const json& params, bool fHelp);
extern json keypoolrefill(const json& params, bool fHelp);
extern json walletpassphrase(const json& params, bool fHelp);
extern json walletpassphrasechange(const json& params, bool fHelp);
extern json walletlock(const json& params, bool fHelp);
extern json encryptwallet(const json& params, bool fHelp);
extern json validateaddress(const json& params, bool fHelp);
extern json getinfo(const json& params, bool fHelp);
extern json getwalletinfo(const json& params, bool fHelp);
extern json reservebalance(const json& params, bool fHelp);
extern json combinethreshold(const json& params, bool fHelp);
extern json splitthreshold(const json& params, bool fHelp);
extern json checkwallet(const json& params, bool fHelp);
extern json repairwallet(const json& params, bool fHelp);
extern json resendtx(const json& params, bool fHelp);
extern json makekeypair(const json& params, bool fHelp);
extern json validatepubkey(const json& params, bool fHelp);
extern json getnewpubkey(const json& params, bool fHelp);
extern json setstakesplitthreshold(const json& params, bool fHelp);
extern json getstakesplitthreshold(const json& params, bool fHelp);

extern json getrawtransaction(const json& params, bool fHelp); // in rcprawtransaction.cpp
extern json listunspent(const json& params, bool fHelp);
extern json createrawtransaction(const json& params, bool fHelp);
extern json decoderawtransaction(const json& params, bool fHelp);
extern json decodescript(const json& params, bool fHelp);
extern json signrawtransaction(const json& params, bool fHelp);
extern json sendrawtransaction(const json& params, bool fHelp);

extern json getbestblockhash(const json& params, bool fHelp); // in rpcblockchain.cpp
extern json getblockcount(const json& params, bool fHelp); // in rpcblockchain.cpp
extern json getdifficulty(const json& params, bool fHelp);
extern json settxfee(const json& params, bool fHelp);
extern json getrawmempool(const json& params, bool fHelp);
extern json getblockhash(const json& params, bool fHelp);
extern json getblock(const json& params, bool fHelp);
extern json getblockbynumber(const json& params, bool fHelp);
extern json getcheckpoint(const json& params, bool fHelp);

extern json getnewstealthaddress(const json& params, bool fHelp);
extern json liststealthaddresses(const json& params, bool fHelp);
extern json importstealthaddress(const json& params, bool fHelp);
extern json sendtostealthaddress(const json& params, bool fHelp);
extern json clearwallettransactions(const json& params, bool fHelp);
extern json scanforalltxns(const json& params, bool fHelp);
extern json scanforstealthtxns(const json& params, bool fHelp);

extern json addstakeout(const json& params, bool fHelp);
extern json delstakeout(const json& params, bool fHelp);
extern json liststakeout(const json& params, bool fHelp);
extern json getstakeoutinfo(const json& params, bool fHelp);

extern json smsgenable(const json& params, bool fHelp);
extern json smsgdisable(const json& params, bool fHelp);
extern json smsglocalkeys(const json& params, bool fHelp);
extern json smsgoptions(const json& params, bool fHelp);
extern json smsgscanchain(const json& params, bool fHelp);
extern json smsgscanbuckets(const json& params, bool fHelp);
extern json smsgaddkey(const json& params, bool fHelp);
extern json smsggetpubkey(const json& params, bool fHelp);
extern json smsgsend(const json& params, bool fHelp);

extern json setloglevel(const json& params, bool fHelp);
extern json getloglevel(const json& params, bool fHelp);
extern json smsgsendanon(const json& params, bool fHelp);
extern json smsginbox(const json& params, bool fHelp);
extern json smsgoutbox(const json& params, bool fHelp);
extern json smsgbuckets(const json& params, bool fHelp);

#endif
