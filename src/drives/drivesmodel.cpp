/*
 * SPDX-FileCopyrightText: 2026 dolphin-poweruse
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "drivesmodel.h"

#include <Solid/Block>
#include <Solid/Device>
#include <Solid/DeviceInterface>
#include <Solid/DeviceNotifier>
#include <Solid/StorageAccess>
#include <Solid/StorageVolume>

#include <QStorageInfo>

DrivesModel::DrivesModel(QObject *parent)
    : QAbstractListModel(parent)
{
    refresh();

    auto *notifier = Solid::DeviceNotifier::instance();
    connect(notifier, &Solid::DeviceNotifier::deviceAdded, this, &DrivesModel::refresh);
    connect(notifier, &Solid::DeviceNotifier::deviceRemoved, this, &DrivesModel::refresh);
}

void DrivesModel::refresh()
{
    beginResetModel();
    m_drives.clear();

    QList<Solid::Device> devices = Solid::Device::listFromType(Solid::DeviceInterface::StorageAccess);
    std::sort(devices.begin(), devices.end(), [](const Solid::Device &left, const Solid::Device &right) {
        return left.displayName().localeAwareCompare(right.displayName()) < 0;
    });

    for (Solid::Device device : std::as_const(devices)) {
        Drive drive;
        drive.udi = device.udi();
        drive.icon = device.icon();
        drive.label = device.displayName();

        if (const auto *block = device.as<Solid::Block>()) {
            drive.devicePath = block->device();
        }
        if (drive.devicePath.isEmpty()) {
            drive.devicePath = device.product().isEmpty() ? device.udi() : device.product();
        }

        const auto *access = device.as<Solid::StorageAccess>();
        drive.accessible = access && access->isAccessible();
        if (drive.accessible) {
            drive.mountPoint = access->filePath();

            const QStorageInfo info(drive.mountPoint);
            if (info.isValid() && info.bytesTotal() > 0) {
                drive.total = info.bytesTotal();
                drive.used = info.bytesTotal() - info.bytesAvailable();
            }
            drive.targetUrl = QUrl::fromLocalFile(drive.mountPoint);
        } else {
            if (const auto *volume = device.as<Solid::StorageVolume>()) {
                drive.total = volume->size();
            }
            // Left to the drives:/ worker, which mounts or unlocks and redirects.
            QUrl url;
            url.setScheme(QStringLiteral("drives"));
            url.setPath(QLatin1Char('/') + device.udi());
            drive.targetUrl = url;
        }

        m_drives.append(drive);
    }

    endResetModel();
}

int DrivesModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_drives.count();
}

QVariant DrivesModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_drives.count()) {
        return {};
    }

    const Drive &drive = m_drives.at(index.row());
    switch (role) {
    case Qt::DisplayRole:
    case DevicePathRole:
        return drive.devicePath;
    case Qt::DecorationRole:
        return QIcon::fromTheme(drive.icon);
    case IconNameRole:
        return drive.icon;
    case MountPointRole:
        return drive.mountPoint;
    case LabelRole:
        return drive.label;
    case UsedBytesRole:
        return drive.used;
    case TotalBytesRole:
        return drive.total;
    case AccessibleRole:
        return drive.accessible;
    case TargetUrlRole:
        return drive.targetUrl;
    case UdiRole:
        return drive.udi;
    case Qt::ToolTipRole:
        return drive.label;
    default:
        return {};
    }
}
