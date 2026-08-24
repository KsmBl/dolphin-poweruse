/*
 * SPDX-FileCopyrightText: 2026 dolphin-poweruse
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef DRIVESVIEW_H
#define DRIVESVIEW_H

#include <QHash>
#include <QListView>
#include <QStyledItemDelegate>
#include <QUrl>

#include <Solid/Device>

class DrivesModel;

/**
 * @brief Draws one drive per row: icon, device node over mount point, usage bar.
 */
class DrivesDelegate : public QStyledItemDelegate
{
    Q_OBJECT

public:
    explicit DrivesDelegate(QObject *parent = nullptr);

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;

private:
    static constexpr int s_iconSize = 32;
    static constexpr int s_margin = 8;
    static constexpr int s_spacing = 2;
    static constexpr int s_maxBarWidth = 460;
};

/**
 * @brief The list shown for drives:/, in place of the ordinary view.
 */
class DrivesView : public QListView
{
    Q_OBJECT

public:
    explicit DrivesView(QWidget *parent = nullptr);

    /** Re-reads the devices, for when the view becomes visible again. */
    void refresh();

Q_SIGNALS:
    /** A drive was activated; the mount point, or drives:/<udi> when unmounted. */
    void driveActivated(const QUrl &url);

protected:
    void contextMenuEvent(QContextMenuEvent *event) override;

private:
    /**
     * Mounts through Solid, which asks UDisks2 to do it for the current user -
     * no root, and a passphrase prompt for an encrypted container. Navigates to
     * the mount point once it is there.
     */
    void mountAndOpen(const QModelIndex &index);
    void unmount(const QModelIndex &index);
    void showProperties(const QModelIndex &index);

    DrivesModel *m_model;
    /** Devices waiting for a mount or unmount to finish. */
    QHash<QString, Solid::Device> m_pending;
};

#endif
