// Copyright (c) 2024-2026 The Pinkcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef QTIPCSERVER_H
#define QTIPCSERVER_H

// Qt-based IPC for passing pinkcoin: URIs between instances.
// Replaces boost::interprocess::message_queue.

void ipcInit(int argc, char *argv[]);
void ipcScanRelay(int argc, char *argv[]);

#define BITCOINURI_QUEUE_NAME "PinkcoinURI"

#endif // QTIPCSERVER_H
