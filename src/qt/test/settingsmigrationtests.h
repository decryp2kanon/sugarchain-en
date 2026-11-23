// Copyright (c) 2026 The Sugarchain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#ifndef BITCOIN_QT_TEST_SETTINGSMIGRATIONTESTS_H
#define BITCOIN_QT_TEST_SETTINGSMIGRATIONTESTS_H

#include <QObject>

class SettingsMigrationTests : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void copyAndKeepSource();
    void existingDestination();
    void emptyDestination();
    void noProfiles();
    void separateNetworks();
    void interruptedWriteRetries();
    void invalidJournalFailsClosed();
};

#endif // BITCOIN_QT_TEST_SETTINGSMIGRATIONTESTS_H
