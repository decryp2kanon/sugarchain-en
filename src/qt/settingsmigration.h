// Copyright (c) 2026 The Sugarchain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#ifndef BITCOIN_QT_SETTINGSMIGRATION_H
#define BITCOIN_QT_SETTINGSMIGRATION_H

#include <QString>

class QSettings;

/** Copy an absent GUI profile, leaving its source untouched. A private, atomic
 * journal makes failed/interrupted native-settings writes retryable. The caller
 * must stop startup on failure; no partially copied profile may be used. */
bool MigrateQtSettings(QSettings& legacy, QSettings& current, const QString& journal_path, QString& error);

/** Migrate the application's current QSettings namespace before its first use. */
bool MigrateLegacyQtSettings(const QString& legacy_app, QString& error);

#endif // BITCOIN_QT_SETTINGSMIGRATION_H
