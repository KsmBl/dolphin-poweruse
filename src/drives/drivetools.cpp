/*
 * SPDX-FileCopyrightText: 2026 dolphin-poweruse
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "drivetools.h"

#include <KLocalizedString>
#include <KMessageBox>

#include <Solid/Block>
#include <Solid/Device>
#include <Solid/StorageAccess>
#include <Solid/StorageVolume>

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QStandardPaths>
#include <QStorageInfo>

namespace
{
const char s_udisksService[] = "org.freedesktop.UDisks2";
const char s_blockInterface[] = "org.freedesktop.UDisks2.Block";
const char s_filesystemInterface[] = "org.freedesktop.UDisks2.Filesystem";

/** Mount points the running system would miss immediately. */
bool isCriticalMountPoint(const QString &mountPoint)
{
    static const QStringList critical = {
        QStringLiteral("/"),
        QStringLiteral("/boot"),
        QStringLiteral("/boot/efi"),
        QStringLiteral("/efi"),
        QStringLiteral("/home"),
        QStringLiteral("/usr"),
        QStringLiteral("/var"),
        QStringLiteral("/etc"),
        QStringLiteral("/nix"),
    };
    return critical.contains(mountPoint);
}
}

QList<DriveTools::Filesystem> DriveTools::availableFilesystems()
{
    // The name of the tool UDisks2 will reach for, so the list matches reality.
    static const QList<QPair<QString, QString>> candidates = {
        {QStringLiteral("ext4"), i18nc("@item:inlistbox filesystem", "ext4 — Linux, the usual choice")},
        {QStringLiteral("ext3"), i18nc("@item:inlistbox filesystem", "ext3 — Linux, older")},
        {QStringLiteral("ext2"), i18nc("@item:inlistbox filesystem", "ext2 — Linux, no journal")},
        {QStringLiteral("btrfs"), i18nc("@item:inlistbox filesystem", "Btrfs — Linux, snapshots")},
        {QStringLiteral("xfs"), i18nc("@item:inlistbox filesystem", "XFS — Linux, large files")},
        {QStringLiteral("f2fs"), i18nc("@item:inlistbox filesystem", "F2FS — Linux, flash storage")},
        {QStringLiteral("vfat"), i18nc("@item:inlistbox filesystem", "FAT32 — reads everywhere, 4 GiB file limit")},
        {QStringLiteral("exfat"), i18nc("@item:inlistbox filesystem", "exFAT — reads almost everywhere, no size limit")},
        {QStringLiteral("ntfs"), i18nc("@item:inlistbox filesystem", "NTFS — Windows")},
    };

    QList<Filesystem> found;
    for (const auto &[type, name] : candidates) {
        const QString tool = type == QLatin1String("ntfs") ? QStringLiteral("mkfs.ntfs") : QStringLiteral("mkfs.") + type;
        if (!QStandardPaths::findExecutable(tool).isEmpty()
            || !QStandardPaths::findExecutable(tool, {QStringLiteral("/usr/sbin"), QStringLiteral("/sbin")}).isEmpty()) {
            found.append({type, name});
        }
    }
    return found;
}

QString DriveTools::udisksPath(const Solid::Device &device)
{
    // Solid uses the UDisks2 object path as its identifier for these devices.
    const QString udi = device.udi();
    return udi.startsWith(QLatin1String("/org/freedesktop/UDisks2/")) ? udi : QString();
}

namespace
{
/*!
 * Whether this one device -- ignoring anything that lives on it -- is something
 * the running system needs.
 */
bool isCriticalItself(const Solid::Device &device, QString *reason)
{
    const auto setReason = [reason](const QString &text) {
        if (reason) {
            *reason = text;
        }
        return true;
    };

    Solid::Device copy = device;

    if (const auto *access = copy.as<Solid::StorageAccess>(); access && access->isAccessible()) {
        const QString mountPoint = access->filePath();
        if (isCriticalMountPoint(mountPoint)) {
            return setReason(i18nc("@info", "This device is mounted at %1, which the running system needs.", mountPoint));
        }
    }

    // The device the running root lives on, whatever it is mounted as now -- and
    // checked even when it is not mounted, so an unmounted system partition is
    // still refused.
    const QStorageInfo root(QStringLiteral("/"));
    if (const auto *block = copy.as<Solid::Block>(); block && root.device() == block->device().toUtf8()) {
        return setReason(i18nc("@info", "This device holds the running system."));
    }

    if (const auto *volume = copy.as<Solid::StorageVolume>();
        volume && volume->usage() == Solid::StorageVolume::Other && volume->fsType() == QLatin1String("swap")) {
        return setReason(i18nc("@info", "This device is in use as swap."));
    }

    return false;
}

/*!
 * Whether @p volume is a partition of @p disk.
 *
 * Solid hangs a whole disk and its partitions off the same drive as siblings,
 * so there is no parent to walk up to. What identifies them as belonging
 * together is that drive plus the kernel's naming: sda1 is on sda, nvme0n1p2 is
 * on nvme0n1.
 */
bool isPartitionOf(const Solid::Device &volume, const Solid::Device &disk)
{
    if (volume.udi() == disk.udi() || volume.parentUdi() != disk.parentUdi()) {
        return false;
    }
    Solid::Device volumeCopy = volume;
    Solid::Device diskCopy = disk;
    const auto *volumeBlock = volumeCopy.as<Solid::Block>();
    const auto *diskBlock = diskCopy.as<Solid::Block>();
    if (!volumeBlock || !diskBlock) {
        return false;
    }
    const QString volumePath = volumeBlock->device();
    const QString diskPath = diskBlock->device();
    return volumePath.length() > diskPath.length() && volumePath.startsWith(diskPath);
}
}

bool DriveTools::isSystemCritical(const Solid::Device &device, QString *reason)
{
    if (udisksPath(device).isEmpty()) {
        if (reason) {
            *reason = i18nc("@info", "This device is not one UDisks2 can format.");
        }
        return true;
    }

    if (isCriticalItself(device, reason)) {
        return true;
    }

    // Formatting a whole disk takes its partitions with it, so the disk is only
    // safe to touch if every partition on it is.
    for (const Solid::Device &volume : Solid::Device::listFromType(Solid::DeviceInterface::StorageVolume)) {
        if (!isPartitionOf(volume, device)) {
            continue;
        }
        QString why;
        if (isCriticalItself(volume, &why)) {
            if (reason) {
                Solid::Device partition(volume.udi());
                const auto *block = partition.as<Solid::Block>();
                *reason = i18nc("@info", "%1 is a partition on this device, and the running system needs it.", block ? block->device() : volume.udi());
            }
            return true;
        }
    }

    return false;
}

void DriveTools::format(const Solid::Device &device, const QString &type, const QString &label, bool erase, QWidget *window)
{
    const QString path = udisksPath(device);
    if (path.isEmpty()) {
        return;
    }

    Solid::Device copy = device;
    const auto reportFailure = [window](const QString &what, const QString &detail) {
        KMessageBox::error(window, detail.isEmpty() ? what : i18nc("@info", "%1\n\n%2", what, detail));
    };

    const auto doFormat = [=]() {
        QVariantMap options;
        options.insert(QStringLiteral("update-partition-type"), true);
        if (!label.isEmpty()) {
            options.insert(QStringLiteral("label"), label);
        }
        if (erase) {
            // UDisks2 writes zeros over the whole device before making the
            // filesystem, so nothing of the old contents survives.
            options.insert(QStringLiteral("erase"), QStringLiteral("zero"));
        }

        QDBusMessage message = QDBusMessage::createMethodCall(QLatin1String(s_udisksService), path, QLatin1String(s_blockInterface), QStringLiteral("Format"));
        message.setArguments({type, options});

        auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(message, erase ? 24 * 60 * 60 * 1000 : 10 * 60 * 1000), window);
        QObject::connect(watcher, &QDBusPendingCallWatcher::finished, window, [reportFailure](QDBusPendingCallWatcher *watcher) {
            watcher->deleteLater();
            const QDBusPendingReply<> reply = *watcher;
            if (reply.isError()) {
                reportFailure(i18nc("@info", "Formatting failed."), reply.error().message());
            }
        });
    };

    // A mounted filesystem cannot be formatted; unmount first and wait for it.
    if (const auto *access = copy.as<Solid::StorageAccess>(); access && access->isAccessible()) {
        QDBusMessage unmount =
            QDBusMessage::createMethodCall(QLatin1String(s_udisksService), path, QLatin1String(s_filesystemInterface), QStringLiteral("Unmount"));
        unmount.setArguments({QVariantMap{}});

        auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(unmount, 5 * 60 * 1000), window);
        QObject::connect(watcher, &QDBusPendingCallWatcher::finished, window, [doFormat, reportFailure](QDBusPendingCallWatcher *watcher) {
            watcher->deleteLater();
            const QDBusPendingReply<> reply = *watcher;
            if (reply.isError()) {
                reportFailure(i18nc("@info", "The device could not be unmounted, so it was not formatted."), reply.error().message());
                return;
            }
            doFormat();
        });
        return;
    }

    doFormat();
}
