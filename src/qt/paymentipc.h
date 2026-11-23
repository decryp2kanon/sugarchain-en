// Copyright (c) 2026 The Sugarchain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#ifndef BITCOIN_QT_PAYMENTIPC_H
#define BITCOIN_QT_PAYMENTIPC_H

#include <QSet>
#include <QStringList>

#include <memory>

class QLocalServer;
class QLockFile;

QString PaymentIpcServerName(const QString& datadir, bool legacy = false);

/** Hold the startup claim throughout the server lifetime. Never unlink a live
 * listener, including when two GUI processes start at the same time. */
bool ListenPaymentIpc(QLocalServer& server, const QString& name, const QString& lock_path,
                      std::unique_ptr<QLockFile>& claim, QString& error);

#endif // BITCOIN_QT_PAYMENTIPC_H
