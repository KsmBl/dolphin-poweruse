/*
 * SPDX-FileCopyrightText: 2026 dolphin-poweruse
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "kio_drives.h"

#include <KIO/UDSEntry>
#include <KLocalizedString>

#include <Solid/DeviceInterface>
#include <Solid/StorageAccess>
#include <Solid/StorageDrive>
#include <Solid/StorageVolume>

#include <QCoreApplication>
#include <QEventLoop>
#include <QUrl>

#include <sys/stat.h>

// Pseudo plugin class that carries the protocol metadata into the binary.
class KIOPluginForMetaData : public QObject
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.kde.kio.worker.drives" FILE "drives.json")
};

namespace
{
/** The UDI carried by a "drives:/<udi>" URL, percent-decoded. */
QString udiFromUrl(const QUrl &url)
{
    // A UDI starts with a slash of its own, so "drives:/" + UDI arrives here as
    // a path with two leading slashes; only the worker's own one is dropped.
    QString path = url.path();
    if (path.startsWith(QLatin1String("//"))) {
        path.remove(0, 1);
    }
    return path == QLatin1String("/") ? QString() : path;
}

bool isRoot(const QUrl &url)
{
    return udiFromUrl(url).isEmpty();
}
}

DrivesWorker::DrivesWorker(const QByteArray &pool, const QByteArray &app)
    : KIO::WorkerBase("drives", pool, app)
{
}

QList<Solid::Device> DrivesWorker::devices() const
{
    // Anything that can be mounted: disks, partitions, optical media, phones,
    // and encrypted containers, which show up once they can be unlocked.
    QList<Solid::Device> found = Solid::Device::listFromType(Solid::DeviceInterface::StorageAccess);
    std::sort(found.begin(), found.end(), [](const Solid::Device &left, const Solid::Device &right) {
        return left.displayName().localeAwareCompare(right.displayName()) < 0;
    });
    return found;
}

Solid::Device DrivesWorker::deviceFor(const QUrl &url) const
{
    const QString udi = udiFromUrl(url);
    if (udi.isEmpty()) {
        return {};
    }
    return Solid::Device(udi);
}

QString DrivesWorker::mountPointOf(Solid::Device &device, QString *error) const
{
    auto *access = device.as<Solid::StorageAccess>();
    if (!access) {
        *error = i18n("This device cannot be opened as a folder.");
        return {};
    }

    if (access->isAccessible()) {
        return access->filePath();
    }

    // Not mounted yet: ask Solid to set it up and wait for the answer. An
    // encrypted container asks for its passphrase at this point.
    QEventLoop loop;
    bool succeeded = false;
    QString message;
    QObject::connect(access, &Solid::StorageAccess::setupDone, &loop, [&](Solid::ErrorType errorType, QVariant errorData, const QString &) {
        succeeded = errorType == Solid::NoError;
        message = errorData.toString();
        loop.quit();
    });
    access->setup();
    loop.exec();

    if (!succeeded || !access->isAccessible()) {
        *error = message.isEmpty() ? i18n("The device could not be mounted.") : message;
        return {};
    }
    return access->filePath();
}

KIO::UDSEntry DrivesWorker::rootEntry() const
{
    KIO::UDSEntry entry;
    entry.fastInsert(KIO::UDSEntry::UDS_NAME, QStringLiteral("."));
    entry.fastInsert(KIO::UDSEntry::UDS_DISPLAY_NAME, i18n("Drives"));
    entry.fastInsert(KIO::UDSEntry::UDS_FILE_TYPE, S_IFDIR);
    entry.fastInsert(KIO::UDSEntry::UDS_MIME_TYPE, QStringLiteral("inode/directory"));
    entry.fastInsert(KIO::UDSEntry::UDS_ICON_NAME, QStringLiteral("drive-harddisk"));
    entry.fastInsert(KIO::UDSEntry::UDS_ACCESS, S_IRUSR | S_IXUSR | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH);
    return entry;
}

KIO::UDSEntry DrivesWorker::entryFor(Solid::Device &device, const QString &name) const
{
    KIO::UDSEntry entry;
    entry.fastInsert(KIO::UDSEntry::UDS_NAME, name);
    entry.fastInsert(KIO::UDSEntry::UDS_FILE_TYPE, S_IFDIR);
    entry.fastInsert(KIO::UDSEntry::UDS_MIME_TYPE, QStringLiteral("inode/directory"));
    entry.fastInsert(KIO::UDSEntry::UDS_ACCESS, S_IRUSR | S_IXUSR | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH);
    entry.fastInsert(KIO::UDSEntry::UDS_ICON_NAME, device.icon());

    QString label = device.displayName();
    const auto *access = device.as<Solid::StorageAccess>();
    const auto *volume = device.as<Solid::StorageVolume>();

    if (volume && volume->usage() == Solid::StorageVolume::Encrypted) {
        label = i18nc("@item a locked encrypted container", "%1 (locked)", label);
    } else if (access && !access->isAccessible()) {
        label = i18nc("@item a drive that is not mounted yet", "%1 (not mounted)", label);
    }
    entry.fastInsert(KIO::UDSEntry::UDS_DISPLAY_NAME, label);

    if (access && access->isAccessible()) {
        // Mounted already: point straight at the mount point, so opening the
        // entry lands in the real folder rather than coming back through here.
        entry.fastInsert(KIO::UDSEntry::UDS_URL, QUrl::fromLocalFile(access->filePath()).toString());
        entry.fastInsert(KIO::UDSEntry::UDS_LOCAL_PATH, access->filePath());
    } else {
        QUrl url;
        url.setScheme(QStringLiteral("drives"));
        // setPath() takes a decoded path and encodes it on the way out.
        url.setPath(QLatin1Char('/') + device.udi());
        entry.fastInsert(KIO::UDSEntry::UDS_URL, url.toString());
    }

    return entry;
}

KIO::WorkerResult DrivesWorker::listDir(const QUrl &url)
{
    if (!isRoot(url)) {
        // A single device: mount it if needed and hand the caller over to its
        // mount point instead of listing anything ourselves.
        Solid::Device device = deviceFor(url);
        if (!device.isValid()) {
            return KIO::WorkerResult::fail(KIO::ERR_DOES_NOT_EXIST, url.toDisplayString());
        }

        QString error;
        const QString mountPoint = mountPointOf(device, &error);
        if (mountPoint.isEmpty()) {
            return KIO::WorkerResult::fail(KIO::ERR_CANNOT_MOUNT, error);
        }

        redirection(QUrl::fromLocalFile(mountPoint));
        return KIO::WorkerResult::pass();
    }

    QStringList used;
    const QList<Solid::Device> all = devices();
    for (Solid::Device device : all) {
        // UDS_NAME has to be unique within the listing, display names are not.
        QString name = device.displayName();
        name.replace(QLatin1Char('/'), QLatin1Char('-'));
        QString unique = name;
        for (int i = 2; used.contains(unique); ++i) {
            unique = QStringLiteral("%1 (%2)").arg(name).arg(i);
        }
        used.append(unique);

        listEntry(entryFor(device, unique));
    }

    listEntry(rootEntry());
    return KIO::WorkerResult::pass();
}

KIO::WorkerResult DrivesWorker::stat(const QUrl &url)
{
    if (isRoot(url)) {
        statEntry(rootEntry());
        return KIO::WorkerResult::pass();
    }

    Solid::Device device = deviceFor(url);
    if (!device.isValid()) {
        return KIO::WorkerResult::fail(KIO::ERR_DOES_NOT_EXIST, url.toDisplayString());
    }

    statEntry(entryFor(device, device.displayName()));
    return KIO::WorkerResult::pass();
}

extern "C" int Q_DECL_EXPORT kdemain(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("kio_drives"));

    if (argc != 4) {
        fprintf(stderr, "Usage: kio_drives protocol domain-socket1 domain-socket2\n");
        return -1;
    }

    DrivesWorker worker(argv[2], argv[3]);
    worker.dispatchLoop();
    return 0;
}

#include "kio_drives.moc"
