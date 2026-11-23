// Copyright (c) 2026 The Sugarchain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#include <qt/paymentipc.h>

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLockFile>

namespace {
constexpr int CONNECT_TIMEOUT{1000};
bool AbsentListener(QLocalSocket::LocalSocketError error)
{
    return error == QLocalSocket::ServerNotFoundError || error == QLocalSocket::ConnectionRefusedError;
}
} // namespace

QString PaymentIpcServerName(const QString& datadir, bool legacy)
{
    // Preserve the existing per-network datadir hash so legacy URI delivery is
    // scoped to exactly the same data directory and network.
    return (legacy ? QStringLiteral("SugarchainKomorebi") : QStringLiteral("SugarchainEn")) + QString::number(qHash(datadir));
}

bool ListenPaymentIpc(QLocalServer& server, const QString& name, const QString& lock_path,
                      std::unique_ptr<QLockFile>& claim, QString& error)
{
    if (!QDir().mkpath(QFileInfo(lock_path).absolutePath())) {
        error = QStringLiteral("Cannot create the payment-handler startup lock directory.");
        return false;
    }
    claim = std::make_unique<QLockFile>(lock_path);
    if (!claim->tryLock(0)) {
        error = QStringLiteral("Another wallet GUI is running or starting for this data directory. "
                               "Use that window, or retry once its startup has completed.");
        return false;
    }
    server.setSocketOptions(QLocalServer::UserAccessOption);
    if (server.listen(name)) return true;

    if (server.serverError() == QAbstractSocket::AddressInUseError) {
        QLocalSocket probe;
        probe.connectToServer(name);
        if (!probe.waitForConnected(CONNECT_TIMEOUT) && AbsentListener(probe.error())) {
            // Our lifetime claim serializes all En cleanup/startup attempts.
            // Legacy listeners have another name and are never removed here.
            if (QLocalServer::removeServer(name) && server.listen(name)) return true;
        }
    }
    error = QStringLiteral("Cannot start the payment handler: its socket is in use or unavailable. "
                           "No running wallet or data-directory lock was removed.");
    return false;
}
