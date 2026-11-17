// Copyright (c) 2026 The Sugarchain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#include <qt/settingsmigration.h>

#include <QCryptographicHash>
#include <QDataStream>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QVariant>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {
constexpr quint32 JOURNAL_MAGIC{0x53454e31}; // SEN1

bool ProfileExists(QSettings& settings, bool& exists)
{
    // allKeys() alone cannot distinguish an absent profile from an existing,
    // deliberately empty one. Disable organization/system fallbacks as well.
    settings.setFallbacksEnabled(false);
    settings.sync();
    if (settings.status() != QSettings::NoError) return false;
#ifdef Q_OS_WIN
    if (settings.format() == QSettings::NativeFormat) {
        HKEY key{nullptr};
        const QString path = QStringLiteral("Software\\") + settings.organizationName() + QLatin1Char('\\') + settings.applicationName();
        const LSTATUS result = RegOpenKeyExW(HKEY_CURRENT_USER, reinterpret_cast<LPCWSTR>(path.utf16()), 0, KEY_READ, &key);
        if (result == ERROR_SUCCESS) RegCloseKey(key);
        if (result != ERROR_SUCCESS && result != ERROR_FILE_NOT_FOUND) return false;
        exists = result == ERROR_SUCCESS;
        return true;
    }
#endif
    exists = QFileInfo::exists(settings.fileName()) || !settings.allKeys().isEmpty();
    return true;
}

bool Failure(QString& error, const char* phase)
{
    qWarning("GUI settings migration failed at %s; original settings are preserved", phase);
    error = QStringLiteral("Cannot migrate the previous GUI settings (%1). Startup was stopped to preserve your settings. "
                           "The original profile is unchanged. Correct the configuration directory permissions or available disk space, then retry; "
                           "keep any migration journal for recovery.").arg(QString::fromLatin1(phase));
    return false;
}
} // namespace

bool MigrateQtSettings(QSettings& legacy, QSettings& current, const QString& journal_path, QString& error)
{
    bool current_exists{false};
    if (!ProfileExists(current, current_exists)) return Failure(error, "read destination");
    if (current_exists && !QFileInfo::exists(journal_path)) return true;

    bool legacy_exists{false};
    if (!ProfileExists(legacy, legacy_exists)) return Failure(error, "read source");
    if (!legacy_exists && !QFileInfo::exists(journal_path)) return true;

    const QString directory = QFileInfo(journal_path).absolutePath();
    if (!QDir().mkpath(directory)) return Failure(error, "create journal directory");
    QLockFile lock(journal_path + QStringLiteral(".lock"));
    if (!lock.tryLock(5000)) return Failure(error, "lock profile migration");

    // Another En process may have completed migration while we waited.
    if (!ProfileExists(current, current_exists)) return Failure(error, "recheck destination");
    if (current_exists && !QFileInfo::exists(journal_path)) return true;

    QVariantMap values;
    if (QFileInfo::exists(journal_path)) {
        QFile journal(journal_path);
        if (!journal.open(QIODevice::ReadOnly) || journal.size() > 32 * 1024 * 1024) return Failure(error, "open journal");
        QDataStream in(&journal);
        in.setVersion(QDataStream::Qt_6_0);
        quint32 magic{0};
        QString target;
        in >> magic >> target >> values;
        if (in.status() != QDataStream::Ok || !in.atEnd() || magic != JOURNAL_MAGIC || target != current.fileName()) {
            return Failure(error, "validate journal");
        }
        qInfo("Retrying interrupted GUI settings migration");
    } else {
        for (const auto& key : legacy.allKeys()) values.insert(key, legacy.value(key));
        if (legacy.status() != QSettings::NoError) return Failure(error, "snapshot source");
        // An empty source needs no write and must not create a phantom profile.
        if (values.isEmpty()) return true;

        QSaveFile journal(journal_path);
        if (!journal.open(QIODevice::WriteOnly) || !journal.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner)) {
            return Failure(error, "create journal");
        }
        QDataStream out(&journal);
        out.setVersion(QDataStream::Qt_6_0);
        out << JOURNAL_MAGIC << current.fileName() << values;
        if (out.status() != QDataStream::Ok || !journal.commit()) return Failure(error, "commit journal");
    }

    // Only a destination absent when the journal was created can reach here.
    // On interrupted writes, clear only that partial copy, never the source.
    current.clear();
    for (auto it = values.cbegin(); it != values.cend(); ++it) current.setValue(it.key(), it.value());
    current.sync();
    if (current.status() != QSettings::NoError) return Failure(error, "write destination");

    QSettings verified(current.fileName(), current.format());
    verified.setFallbacksEnabled(false);
    verified.sync();
    if (verified.status() != QSettings::NoError || verified.allKeys() != values.keys()) return Failure(error, "verify destination keys");
    for (auto it = values.cbegin(); it != values.cend(); ++it) {
        if (verified.value(it.key()) != it.value()) return Failure(error, "verify destination values");
    }
    // No GUI setting reader/writer runs until this succeeds. A crash before
    // journal removal therefore safely retries the identical typed snapshot.
    if (!QFile::remove(journal_path)) return Failure(error, "finalize journal");
    qInfo("GUI settings migration completed; original profile retained");
    return true;
}

bool MigrateLegacyQtSettings(const QString& legacy_app, QString& error)
{
    QSettings current;
    QSettings legacy(current.format(), current.scope(), current.organizationName(), legacy_app);
    const QByteArray profile_id = QCryptographicHash::hash(current.fileName().toUtf8(), QCryptographicHash::Sha256).toHex();
    const QString directory = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) +
                              QStringLiteral("/Sugarchain/.en-settings-migration/");
    return MigrateQtSettings(legacy, current, directory + QString::fromLatin1(profile_id) + QStringLiteral(".journal"), error);
}
