// Copyright (c) 2026 The Sugarchain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#include <qt/test/settingsmigrationtests.h>

#include <qt/settingsmigration.h>

#include <QDir>
#include <QFile>
#include <QPoint>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>

void SettingsMigrationTests::copyAndKeepSource()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QSettings old(dir.filePath("old.ini"), QSettings::IniFormat);
    QSettings current(dir.filePath("new.ini"), QSettings::IniFormat);
    old.setValue("language", "ko_KR");
    old.setValue("strDataDir", "/test-only/path with spaces");
    old.setValue("window/position", QPoint(37, 91));
    old.setValue("fMinimizeToTray", true);
    old.sync();
    QString error;
    const QString journal = dir.filePath("migration.journal");
    QVERIFY2(MigrateQtSettings(old, current, journal, error), qPrintable(error));
    QCOMPARE(current.value("language").toString(), QString("ko_KR"));
    QCOMPARE(current.value("strDataDir"), old.value("strDataDir"));
    QCOMPARE(current.value("window/position").toPoint(), QPoint(37, 91));
    QVERIFY(current.value("fMinimizeToTray").toBool());
    QCOMPARE(current.allKeys(), old.allKeys());
    QVERIFY(QFile::exists(old.fileName()));
    QVERIFY(!QFile::exists(journal));
    old.setValue("language", "changed after migration");
    old.sync();
    QVERIFY(MigrateQtSettings(old, current, journal, error));
    QCOMPARE(current.value("language").toString(), QString("ko_KR"));
}

void SettingsMigrationTests::existingDestination()
{
    QTemporaryDir dir;
    QSettings old(dir.filePath("old.ini"), QSettings::IniFormat);
    QSettings current(dir.filePath("new.ini"), QSettings::IniFormat);
    old.setValue("language", "old");
    current.setValue("language", "new");
    old.sync();
    current.sync();
    QString error;
    QVERIFY(MigrateQtSettings(old, current, dir.filePath("journal"), error));
    QCOMPARE(current.value("language").toString(), QString("new"));
}

void SettingsMigrationTests::emptyDestination()
{
    QTemporaryDir dir;
    QSettings old(dir.filePath("old.ini"), QSettings::IniFormat);
    old.setValue("language", "old");
    old.sync();
    QFile empty(dir.filePath("new.ini"));
    QVERIFY(empty.open(QIODevice::WriteOnly));
    empty.close();
    QSettings current(empty.fileName(), QSettings::IniFormat);
    QString error;
    QVERIFY(MigrateQtSettings(old, current, dir.filePath("journal"), error));
    QVERIFY(current.allKeys().isEmpty());
}

void SettingsMigrationTests::noProfiles()
{
    QTemporaryDir dir;
    QSettings old(dir.filePath("old.ini"), QSettings::IniFormat);
    QSettings current(dir.filePath("new.ini"), QSettings::IniFormat);
    QString error;
    QVERIFY(MigrateQtSettings(old, current, dir.filePath("journal"), error));
    QVERIFY(!QFile::exists(old.fileName()));
    QVERIFY(!QFile::exists(current.fileName()));
    QVERIFY(current.allKeys().isEmpty());
}

void SettingsMigrationTests::separateNetworks()
{
    QTemporaryDir dir;
    const QStringList networks{"main", "testnet", "testnet4", "signet", "regtest"};
    for (const auto& network : networks) {
        QSettings old(dir.filePath(network + "-old.ini"), QSettings::IniFormat);
        QSettings current(dir.filePath(network + "-new.ini"), QSettings::IniFormat);
        old.setValue("network-setting", network);
        old.sync();
        QString error;
        QVERIFY(MigrateQtSettings(old, current, dir.filePath(network + ".journal"), error));
    }
    for (const auto& network : networks) {
        QSettings current(dir.filePath(network + "-new.ini"), QSettings::IniFormat);
        QCOMPARE(current.value("network-setting").toString(), network);
    }
}

void SettingsMigrationTests::interruptedWriteRetries()
{
    QTemporaryDir dir;
    QSettings old(dir.filePath("old.ini"), QSettings::IniFormat);
    old.setValue("language", "ko_KR");
    old.setValue("window/position", QPoint(7, 12));
    old.sync();
    QFile obstruction(dir.filePath("destination"));
    QVERIFY(obstruction.open(QIODevice::WriteOnly));
    obstruction.close();
    const QString target = dir.filePath("destination/new.ini");
    const QString journal = dir.filePath("migration.journal");
    QString error;
    {
        QSettings current(target, QSettings::IniFormat);
        QVERIFY(!MigrateQtSettings(old, current, journal, error));
        QVERIFY(QFile::exists(journal));
        QVERIFY(!error.isEmpty());
    }
    QVERIFY(obstruction.remove());
    QVERIFY(QDir().mkpath(dir.filePath("destination")));
    // Simulate a partial previous write. The journal, not file presence, wins.
    QSettings partial(target, QSettings::IniFormat);
    partial.setValue("partial-only", "must disappear");
    partial.sync();
    old.setValue("language", "source changed after snapshot");
    old.sync();
    QVERIFY2(MigrateQtSettings(old, partial, journal, error), qPrintable(error));
    QCOMPARE(partial.value("language").toString(), QString("ko_KR"));
    QCOMPARE(partial.value("window/position").toPoint(), QPoint(7, 12));
    QVERIFY(!partial.contains("partial-only"));
    QVERIFY(!QFile::exists(journal));
}

void SettingsMigrationTests::invalidJournalFailsClosed()
{
    QTemporaryDir dir;
    QSettings old(dir.filePath("old.ini"), QSettings::IniFormat);
    QSettings current(dir.filePath("new.ini"), QSettings::IniFormat);
    QFile journal(dir.filePath("migration.journal"));
    QVERIFY(journal.open(QIODevice::WriteOnly));
    journal.write("incomplete");
    journal.close();
    QString error;
    QVERIFY(!MigrateQtSettings(old, current, journal.fileName(), error));
    QVERIFY(current.allKeys().isEmpty());
    QVERIFY(!error.isEmpty());
}
