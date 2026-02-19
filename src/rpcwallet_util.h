// Copyright (c) 2024 The Pinkcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Shared helper declarations for RPC wallet command files.

#ifndef PINKCOIN_RPCWALLET_UTIL_H
#define PINKCOIN_RPCWALLET_UTIL_H

#include "json/json_spirit_value.h"
#include "main.h"

class CWalletTx;
class CWalletDB;

void WalletTxToJSON(const CWalletTx& wtx, json_spirit::Object& entry);
std::string AccountFromValue(const json_spirit::Value& value);
void accountingDeprecationCheck();
int64_t GetAccountBalance(CWalletDB& walletdb, const std::string& strAccount, int nMinDepth);
int64_t GetAccountBalance(const std::string& strAccount, int nMinDepth);

#endif // PINKCOIN_RPCWALLET_UTIL_H
