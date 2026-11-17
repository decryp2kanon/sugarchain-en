// Copyright (c) 2026 The Sugarchain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#include <qt/test/paymentipctests.h>

#include <qt/paymentipc.h>

#include <QDataStream>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLockFile>
#include <QTemporaryDir>
#include <QTest>

namespace {
QString ReceiveRequest(QLocalServer& server)
{
    if (!server.hasPendingConnections() && !server.waitForNewConnection(1000)) return {};
    std::unique_ptr<QLocalSocket> socket(server.nextPendingConnection());
    if (!socket || (socket->bytesAvailable() == 0 && !socket->waitForReadyRead(1000))) return {};
    QDataStream in(socket.get());
    in.setVersion(QDataStream::Qt_4_0);
    QString request;
    in >> request;
    return request;
}
} // namespace

void PaymentIpcTests::namesAreScoped()
{
    const QString main_name = PaymentIpcServerName("/example/main");
    QVERIFY(main_name.startsWith("SugarchainEn"));
    const QString old_name = PaymentIpcServerName("/example/main", true);
    QVERIFY(old_name.startsWith("SugarchainKomorebi"));
    QCOMPARE(main_name.mid(QString("SugarchainEn").size()), old_name.mid(QString("SugarchainKomorebi").size()));
    QVERIFY(main_name != PaymentIpcServerName("/example/main/regtest"));
    QVERIFY(main_name != PaymentIpcServerName("/different/main"));
}

void PaymentIpcTests::preferEnOnce()
{
    QTemporaryDir dir;
    const QString current_name = PaymentIpcServerName(dir.path());
    const QString old_name = PaymentIpcServerName(dir.path(), true);
    QLocalServer current, old;
    QVERIFY(current.listen(current_name));
    QVERIFY(old.listen(old_name));
    const QString request{"sugarchain:test-only?label=En"};
    QString error;
    QCOMPARE(SendPaymentIpc({current_name, old_name}, {request}, error), PaymentIpcResult::SENT);
    QCOMPARE(ReceiveRequest(current), request);
    QVERIFY(!current.waitForNewConnection(10));
    QVERIFY(!old.waitForNewConnection(10));
    QVERIFY(error.isEmpty());
}

void PaymentIpcTests::legacyFallbackOnce()
{
    QTemporaryDir dir;
    const QString old_name = PaymentIpcServerName(dir.path(), true);
    QLocalServer old;
    QVERIFY(old.listen(old_name));
    const QString request{"sugarchain:test-only?label=Legacy"};
    QString error;
    QCOMPARE(SendPaymentIpc({PaymentIpcServerName(dir.path()), old_name}, {request}, error), PaymentIpcResult::SENT);
    QCOMPARE(ReceiveRequest(old), request);
    QVERIFY(!old.waitForNewConnection(10));
    QVERIFY(error.isEmpty());
}

void PaymentIpcTests::noListener()
{
    QTemporaryDir dir;
    QString error;
    QCOMPARE(SendPaymentIpc({PaymentIpcServerName(dir.path()), PaymentIpcServerName(dir.path(), true)}, {"sugarchain:test-only"}, error), PaymentIpcResult::NO_SERVER);
    QVERIFY(error.isEmpty());
}

void PaymentIpcTests::concurrentStartupClaim()
{
    QTemporaryDir dir;
    const QString name = PaymentIpcServerName(dir.path());
    const QString lock_path = dir.filePath("startup.lock");
    QLocalServer first, second;
    std::unique_ptr<QLockFile> first_claim, second_claim;
    QString error;
    QVERIFY(ListenPaymentIpc(first, name, lock_path, first_claim, error));
    QVERIFY(!ListenPaymentIpc(second, name, lock_path, second_claim, error));
    QVERIFY(!error.isEmpty());
    QVERIFY(first.isListening());
    error.clear();
    QCOMPARE(SendPaymentIpc({name}, {"sugarchain:first-still-running"}, error), PaymentIpcResult::SENT);
    QCOMPARE(ReceiveRequest(first), QString("sugarchain:first-still-running"));
    first.close();
    first_claim.reset();
    QVERIFY(ListenPaymentIpc(second, name, lock_path, second_claim, error));
}

void PaymentIpcTests::incompleteDeliveryDoesNotFallback()
{
    QTemporaryDir dir;
    const QString current_name = PaymentIpcServerName(dir.path());
    const QString old_name = PaymentIpcServerName(dir.path(), true);
    QLocalServer current, old;
    QVERIFY(current.listen(current_name));
    QVERIFY(old.listen(old_name));
    // Deliberately do not read from the receiving socket. Exceed its OS buffer
    // to force an uncertain partial write without crashing either process.
    QString error;
    const QString oversized = QStringLiteral("sugarchain:") + QString(4 * 1024 * 1024, QLatin1Char('x'));
    QCOMPARE(SendPaymentIpc({current_name, old_name}, {oversized}, error), PaymentIpcResult::FAILED);
    QVERIFY(!error.isEmpty());
    QVERIFY(!old.waitForNewConnection(10));
}
