// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2012 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_INIT_H
#define BITCOIN_INIT_H

#include "wallet.h"

#include <memory>

class ThreadGroup;

extern std::unique_ptr<CWallet> pwalletMain;
extern std::unique_ptr<CWallet> pstakeDB;
void StartShutdown();
void Shutdown();
bool AppInit2(ThreadGroup& threadGroup);
std::string HelpMessage();

#endif
