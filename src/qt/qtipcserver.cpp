// Copyright (c) 2024-2026 The Pinkcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "qtipcserver.h"
#include "guiconstants.h"
#include "ui_interface.h"
#include "util.h"
#include "string_utils.h"

#include <QLocalServer>
#include <QLocalSocket>
#include <QTimer>
#include <string>

static QLocalServer* ipcServer = nullptr;

static bool ipcScanCmd(int argc, char *argv[], bool fRelay)
{
    bool fSent = false;
    for (int i = 1; i < argc; i++)
    {
        if (strutil::istarts_with(argv[i], "pinkcoin:"))
        {
            QLocalSocket socket;
            socket.connectToServer(BITCOINURI_QUEUE_NAME, QIODevice::WriteOnly);
            if (socket.waitForConnected(500))
            {
                QByteArray data(argv[i]);
                socket.write(data);
                socket.waitForBytesWritten(1000);
                socket.disconnectFromServer();
                fSent = true;
            }
            else if (fRelay)
                break;
        }
    }
    return fSent;
}

void ipcScanRelay(int argc, char *argv[])
{
    if (ipcScanCmd(argc, argv, true))
        exit(0);
}

void ipcInit(int argc, char *argv[])
{
    // Remove any stale server
    QLocalServer::removeServer(BITCOINURI_QUEUE_NAME);

    ipcServer = new QLocalServer();
    if (!ipcServer->listen(BITCOINURI_QUEUE_NAME))
    {
        printf("ipcInit() - failed to listen on %s: %s\n",
               BITCOINURI_QUEUE_NAME,
               ipcServer->errorString().toStdString().c_str());
        delete ipcServer;
        ipcServer = nullptr;
        return;
    }

    QObject::connect(ipcServer, &QLocalServer::newConnection, []() {
        QLocalSocket* client = ipcServer->nextPendingConnection();
        if (!client) return;

        QObject::connect(client, &QLocalSocket::readyRead, [client]() {
            QByteArray data = client->readAll();
            if (data.size() > 0 && data.size() <= MAX_URI_LENGTH)
            {
                std::string uri(data.constData(), data.size());
                uiInterface.ThreadSafeHandleURI(uri);
            }
            client->disconnectFromServer();
            client->deleteLater();
        });

        QTimer::singleShot(2000, client, [client]() {
            if (client->state() != QLocalSocket::UnconnectedState) {
                client->disconnectFromServer();
                client->deleteLater();
            }
        });
    });

    ipcScanCmd(argc, argv, false);
}
