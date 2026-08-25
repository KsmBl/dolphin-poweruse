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

    /**
     * Opens the properties dialog for the highlighted drive -- the same one the
     * context menu opens, so Alt+Return and right-click agree.
     *
     * @return false when there is no drive to show, so the caller can fall back
     * to whatever it would have done otherwise.
     */
    bool showPropertiesForCurrent();

Q_SIGNALS:
    /** A drive was activated; the mount point, or drives:/<udi> when unmounted. */
    void driveActivated(const QUrl &url);

protected:
    void contextMenuEvent(QContextMenuEvent *event) override;

    /**
     * Claims Alt+Return before the window-wide "Properties" action can take it.
     *
     * That action works on the ordinary view, which is hidden and empty while
     * this list is up, so it would open a dialog about drives:/ instead of
     * about the drive. Accepting the ShortcutOverride makes Qt deliver the key
     * here as an ordinary key press instead.
     */
    bool event(QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    /**
     * Mounts through Solid, which asks UDisks2 to do it for the current user -
     * no root, and a passphrase prompt for an encrypted container. Navigates to
     * the mount point once it is there.
     */
    void mountAndOpen(const QModelIndex &index);
    void unmount(const QModelIndex &index);
    void showProperties(const QModelIndex &index);

    /*! Both destroy data, so both ask first and refuse system devices. */
    void formatDrive(const QModelIndex &index);
    void wipeFreeSpace(const QModelIndex &index);

    DrivesModel *m_model;
    /** Devices waiting for a mount or unmount to finish. */
    QHash<QString, Solid::Device> m_pending;
};

#endif
