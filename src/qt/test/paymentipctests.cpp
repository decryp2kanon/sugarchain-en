// Copyright (c) 2026 The Sugarchain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#include <qt/test/paymentipctests.h>

#include <qt/paymentipc.h>

#include <QLocalServer>
#include <QLocalSocket>
#include <QLockFile>
#include <QTemporaryDir>
#include <QTest>

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
    first.close();
    first_claim.reset();
    QVERIFY(ListenPaymentIpc(second, name, lock_path, second_claim, error));
}
