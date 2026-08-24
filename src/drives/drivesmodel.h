/*
 * SPDX-FileCopyrightText: 2026 dolphin-poweruse
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef DRIVESMODEL_H
#define DRIVESMODEL_H

#include <QAbstractListModel>
#include <QIcon>
#include <QList>
#include <QString>
#include <QUrl>

/**
 * @brief Every drive, partition and container Solid can mount.
 *
 * Rows carry what the drives view draws: the icon, the device node, where it is
 * mounted and how full it is.
 */
class DrivesModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Role {
        IconNameRole = Qt::UserRole + 1,
        DevicePathRole, ///< /dev/nvme0n1p2
        MountPointRole, ///< where it is reachable, empty when not mounted
        LabelRole, ///< the friendly name Solid gives it
        UsedBytesRole,
        TotalBytesRole,
        AccessibleRole,
        TargetUrlRole, ///< where a click should go
        UdiRole, ///< the Solid identifier, for mounting and unmounting
    };

    explicit DrivesModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;

    /** Re-reads the devices; hooked to Solid's added/removed notifications. */
    void refresh();

private:
    struct Drive {
        QString udi;
        QString icon;
        QString label;
        QString devicePath;
        QString mountPoint;
        qint64 used = -1;
        qint64 total = -1;
        bool accessible = false;
        QUrl targetUrl;
    };

    QList<Drive> m_drives;
};

#endif
