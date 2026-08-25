/*
 * SPDX-FileCopyrightText: 2026 dolphin-poweruse
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "drivesview.h"

#include "drivesmodel.h"
#include "drivetools.h"
#include "formatdialog.h"
#include "wipefreespacejob.h"

#include <KIO/Global>
#include <KJobWidgets>
#include <KLocalizedString>
#include <KMessageBox>
#include <KPropertiesDialog>

#include <Solid/Device>
#include <Solid/StorageAccess>

#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QDir>
#include <QFontMetrics>
#include <QIcon>
#include <QKeyEvent>
#include <QMenu>

#include <KIO/JobTracker>
#include <KJobTrackerInterface>
#include <QPainter>
#include <QStyleOptionProgressBar>

namespace
{
/** The home directory is long and always the same, so show it as "~". */
QString shortenHome(const QString &path)
{
    const QString home = QDir::homePath();
    if (path == home) {
        return QStringLiteral("~");
    }
    if (path.startsWith(home + QLatin1Char('/'))) {
        return QLatin1String("~") + path.mid(home.length());
    }
    return path;
}
}

DrivesDelegate::DrivesDelegate(QObject *parent)
    : QStyledItemDelegate(parent)
{
}

QSize DrivesDelegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    Q_UNUSED(index)
    // Device node, mount point, and the usage bar underneath them.
    const int lineHeight = option.fontMetrics.height();
    const int barHeight = qMax(lineHeight + 4, 16);
    const int textHeight = 2 * lineHeight + barHeight + 2 * s_spacing;
    return QSize(0, qMax(s_iconSize, textHeight) + 2 * s_margin);
}

void DrivesDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    QStyleOptionViewItem opt = option;
    initStyleOption(&opt, index);
    // The text is drawn here, not by the default implementation.
    opt.text.clear();
    opt.icon = QIcon();

    QStyle *style = opt.widget ? opt.widget->style() : QApplication::style();
    style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, opt.widget);

    const QRect rect = opt.rect.adjusted(s_margin, s_margin, -s_margin, -s_margin);
    const bool selected = opt.state & QStyle::State_Selected;
    const QPalette::ColorRole textRole = selected ? QPalette::HighlightedText : QPalette::Text;

    // Icon, always the same size so the rows line up.
    const QIcon icon = QIcon::fromTheme(index.data(DrivesModel::IconNameRole).toString());
    const QRect iconRect(rect.left(), rect.top() + (rect.height() - s_iconSize) / 2, s_iconSize, s_iconSize);
    icon.paint(painter, iconRect, Qt::AlignCenter, selected ? QIcon::Selected : QIcon::Normal);

    const qint64 used = index.data(DrivesModel::UsedBytesRole).toLongLong();
    const qint64 total = index.data(DrivesModel::TotalBytesRole).toLongLong();
    const bool showBar = used >= 0 && total > 0;

    const int textLeft = iconRect.right() + s_margin;
    const QRect textRect(textLeft, rect.top(), qMax(0, rect.right() - textLeft), rect.height());

    // Where it is reachable matters most, so that goes on top; the device node
    // is the detail underneath.
    const QString devicePath = index.data(DrivesModel::DevicePathRole).toString();
    QString mountPoint = index.data(DrivesModel::MountPointRole).toString();
    mountPoint = mountPoint.isEmpty() ? i18nc("@item:intable", "Not mounted") : shortenHome(mountPoint);

    QFont boldFont = opt.font;
    boldFont.setBold(true);
    const QFontMetrics boldMetrics(boldFont);

    const int lineHeight = opt.fontMetrics.height();
    const int barHeight = qMax(lineHeight + 4, 16);
    const int blockHeight = showBar ? 2 * lineHeight + barHeight + 2 * s_spacing : 2 * lineHeight;
    const int firstTop = textRect.top() + (textRect.height() - blockHeight) / 2;

    painter->save();
    painter->setFont(boldFont);
    painter->setPen(opt.palette.color(QPalette::Normal, textRole));
    painter->drawText(QRect(textRect.left(), firstTop, textRect.width(), lineHeight),
                      Qt::AlignLeft | Qt::AlignVCenter,
                      boldMetrics.elidedText(mountPoint, Qt::ElideMiddle, textRect.width()));

    painter->setFont(opt.font);
    if (!selected) {
        painter->setPen(opt.palette.color(QPalette::Disabled, QPalette::Text));
    }
    painter->drawText(QRect(textRect.left(), firstTop + lineHeight, textRect.width(), lineHeight),
                      Qt::AlignLeft | Qt::AlignVCenter,
                      opt.fontMetrics.elidedText(devicePath, Qt::ElideMiddle, textRect.width()));
    painter->restore();

    if (!showBar) {
        return;
    }

    // How full the partition or container is, sitting under the mount point.
    QStyleOptionProgressBar bar;
    // Without State_Horizontal the style draws the bar rotated, bottom to top.
    bar.state = QStyle::State_Enabled | QStyle::State_Horizontal;
    bar.direction = opt.direction;
    bar.palette = opt.palette;
    bar.fontMetrics = opt.fontMetrics;
    bar.minimum = 0;
    bar.maximum = 1000;
    bar.progress = static_cast<int>((static_cast<double>(used) / static_cast<double>(total)) * 1000.0);
    bar.textVisible = true;
    bar.textAlignment = Qt::AlignCenter;
    bar.text = i18nc("@info:status %1 and %2 are formatted sizes, e.g. 12 GiB", "%1 of %2 used", KIO::convertSize(used), KIO::convertSize(total));

    const int barWidth = qMin(textRect.width(), s_maxBarWidth);
    bar.rect = QRect(textRect.left(), firstTop + 2 * lineHeight + s_spacing, barWidth, barHeight);
    style->drawControl(QStyle::CE_ProgressBar, &bar, painter, opt.widget);
}

DrivesView::DrivesView(QWidget *parent)
    : QListView(parent)
    , m_model(new DrivesModel(this))
{
    setModel(m_model);
    setItemDelegate(new DrivesDelegate(this));
    setSelectionMode(QAbstractItemView::SingleSelection);
    setUniformItemSizes(true);
    setResizeMode(QListView::Adjust);
    setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    setFrameShape(QFrame::NoFrame);

    connect(this, &QAbstractItemView::activated, this, &DrivesView::mountAndOpen);
}

void DrivesView::refresh()
{
    m_model->refresh();
}

void DrivesView::mountAndOpen(const QModelIndex &index)
{
    if (!index.isValid()) {
        return;
    }

    const QString udi = index.data(DrivesModel::UdiRole).toString();
    Solid::Device device(udi);
    auto *access = device.as<Solid::StorageAccess>();
    if (!access) {
        return;
    }

    if (access->isAccessible()) {
        Q_EMIT driveActivated(QUrl::fromLocalFile(access->filePath()));
        return;
    }

    // Held onto until the answer arrives, so the interface stays alive.
    m_pending.insert(udi, device);

    connect(
        access,
        &Solid::StorageAccess::setupDone,
        this,
        [this, udi](Solid::ErrorType error, QVariant errorData, const QString &) {
            m_pending.remove(udi);
            m_model->refresh();

            if (error != Solid::NoError) {
                const QString message = errorData.toString();
                KMessageBox::error(this, message.isEmpty() ? i18nc("@info", "This device could not be mounted.") : message);
                return;
            }

            Solid::Device mounted(udi);
            const auto *mountedAccess = mounted.as<Solid::StorageAccess>();
            if (mountedAccess && mountedAccess->isAccessible()) {
                Q_EMIT driveActivated(QUrl::fromLocalFile(mountedAccess->filePath()));
            }
        },
        Qt::SingleShotConnection);

    // UDisks2 does the mounting for the current user; an encrypted container
    // asks for its passphrase here.
    access->setup();
}

void DrivesView::unmount(const QModelIndex &index)
{
    if (!index.isValid()) {
        return;
    }

    const QString udi = index.data(DrivesModel::UdiRole).toString();
    Solid::Device device(udi);
    auto *access = device.as<Solid::StorageAccess>();
    if (!access || !access->isAccessible()) {
        return;
    }

    m_pending.insert(udi, device);

    connect(
        access,
        &Solid::StorageAccess::teardownDone,
        this,
        [this, udi](Solid::ErrorType error, QVariant errorData, const QString &) {
            m_pending.remove(udi);
            m_model->refresh();

            if (error != Solid::NoError) {
                const QString message = errorData.toString();
                KMessageBox::error(this, message.isEmpty() ? i18nc("@info", "This device could not be unmounted.") : message);
            }
        },
        Qt::SingleShotConnection);

    access->teardown();
}

bool DrivesView::showPropertiesForCurrent()
{
    const QModelIndex index = currentIndex();
    if (!index.isValid()) {
        return false;
    }
    showProperties(index);
    return true;
}

void DrivesView::showProperties(const QModelIndex &index)
{
    if (!index.isValid()) {
        return;
    }

    // The mount point is what has a size worth looking at; for a device that is
    // not mounted the node itself is all there is to show.
    const QString mountPoint = index.data(DrivesModel::MountPointRole).toString();
    const QString devicePath = index.data(DrivesModel::DevicePathRole).toString();
    const QUrl url = QUrl::fromLocalFile(mountPoint.isEmpty() ? devicePath : mountPoint);

    auto *dialog = new KPropertiesDialog(url, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->show();
}

namespace
{
/*! The shortcut the "Properties" action carries, in both its spellings. */
bool isPropertiesShortcut(const QKeyEvent *event)
{
    return event->modifiers().testFlag(Qt::AltModifier) && (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter);
}
}

bool DrivesView::event(QEvent *event)
{
    if (event->type() == QEvent::ShortcutOverride && isPropertiesShortcut(static_cast<QKeyEvent *>(event))) {
        event->accept();
        return true;
    }
    return QListView::event(event);
}

void DrivesView::keyPressEvent(QKeyEvent *event)
{
    if (isPropertiesShortcut(event)) {
        showPropertiesForCurrent();
        return;
    }
    QListView::keyPressEvent(event);
}

void DrivesView::contextMenuEvent(QContextMenuEvent *event)
{
    const QModelIndex index = indexAt(event->pos());
    if (!index.isValid()) {
        return;
    }
    setCurrentIndex(index);

    const bool accessible = index.data(DrivesModel::AccessibleRole).toBool();
    const QString mountPoint = index.data(DrivesModel::MountPointRole).toString();
    const QString devicePath = index.data(DrivesModel::DevicePathRole).toString();

    QMenu menu(this);
    QAction *openAction = menu.addAction(QIcon::fromTheme(QStringLiteral("document-open-folder")),
                                         accessible ? i18nc("@action:inmenu", "Open") : i18nc("@action:inmenu", "Mount and Open"));

    QAction *mountAction = nullptr;
    QAction *unmountAction = nullptr;
    menu.addSeparator();
    if (accessible) {
        unmountAction = menu.addAction(QIcon::fromTheme(QStringLiteral("media-eject")), i18nc("@action:inmenu", "Unmount"));
    } else {
        mountAction = menu.addAction(QIcon::fromTheme(QStringLiteral("drive-harddisk")), i18nc("@action:inmenu", "Mount"));
    }

    menu.addSeparator();
    QAction *copyMountPoint = menu.addAction(QIcon::fromTheme(QStringLiteral("edit-copy")), i18nc("@action:inmenu", "Copy Mount Point"));
    copyMountPoint->setEnabled(!mountPoint.isEmpty());
    QAction *copyDevicePath = menu.addAction(QIcon::fromTheme(QStringLiteral("edit-copy")), i18nc("@action:inmenu", "Copy Device Path"));

    menu.addSeparator();
    QAction *wipeAction = menu.addAction(QIcon::fromTheme(QStringLiteral("edit-clear-all")), i18nc("@action:inmenu", "Overwrite Free Space…"));
    wipeAction->setEnabled(accessible);
    wipeAction->setToolTip(i18nc("@info:tooltip", "Overwrites what is left of deleted files with zeros."));

    QString criticalReason;
    Solid::Device device(index.data(DrivesModel::UdiRole).toString());
    const bool critical = DriveTools::isSystemCritical(device, &criticalReason);
    QAction *formatAction = menu.addAction(QIcon::fromTheme(QStringLiteral("drive-harddisk")), i18nc("@action:inmenu", "Format…"));
    formatAction->setEnabled(!critical);
    if (critical) {
        formatAction->setToolTip(criticalReason);
    }

    menu.addSeparator();
    QAction *propertiesAction = menu.addAction(QIcon::fromTheme(QStringLiteral("document-properties")), i18nc("@action:inmenu", "Properties…"));

    const QAction *chosen = menu.exec(event->globalPos());
    if (!chosen) {
        return;
    }

    if (chosen == openAction) {
        mountAndOpen(index);
    } else if (chosen == mountAction) {
        mountAndOpen(index);
    } else if (chosen == unmountAction) {
        unmount(index);
    } else if (chosen == copyMountPoint) {
        QGuiApplication::clipboard()->setText(mountPoint);
    } else if (chosen == copyDevicePath) {
        QGuiApplication::clipboard()->setText(devicePath);
    } else if (chosen == wipeAction) {
        wipeFreeSpace(index);
    } else if (chosen == formatAction) {
        formatDrive(index);
    } else if (chosen == propertiesAction) {
        showProperties(index);
    }
}

void DrivesView::formatDrive(const QModelIndex &index)
{
    if (!index.isValid()) {
        return;
    }

    Solid::Device device(index.data(DrivesModel::UdiRole).toString());
    QString reason;
    if (DriveTools::isSystemCritical(device, &reason)) {
        KMessageBox::error(this, reason);
        return;
    }

    const QList<DriveTools::Filesystem> filesystems = DriveTools::availableFilesystems();
    if (filesystems.isEmpty()) {
        KMessageBox::error(this, i18nc("@info", "No filesystem tools are installed, so nothing can be created."));
        return;
    }

    const QString devicePath = index.data(DrivesModel::DevicePathRole).toString();
    FormatDialog dialog(this, devicePath, index.data(DrivesModel::LabelRole).toString(), filesystems);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    DriveTools::format(device, dialog.filesystemType(), dialog.label(), dialog.eraseFirst(), this);
    m_model->refresh();
}

void DrivesView::wipeFreeSpace(const QModelIndex &index)
{
    if (!index.isValid()) {
        return;
    }

    const QString mountPoint = index.data(DrivesModel::MountPointRole).toString();
    if (mountPoint.isEmpty()) {
        return;
    }

    // Say plainly what this does and does not achieve before spending the time.
    const QString question = xi18nc("@info",
                                    "<para>This fills the free space of <filename>%1</filename> with zeros and then "
                                    "releases it again, so what is left of deleted files is overwritten.</para>"
                                    "<para>Files that are still there are not touched.</para>"
                                    "<para>It writes until the filesystem is nearly full and can take a long time. On "
                                    "an SSD it is <emphasis>not</emphasis> a guarantee: the drive decides which blocks "
                                    "it hands out, and spare areas are never reachable from here. Discarding "
                                    "(<command>fstrim</command>) is the tool that applies there.</para>",
                                    mountPoint);

    if (KMessageBox::warningContinueCancel(this,
                                           question,
                                           i18nc("@title:window", "Overwrite Free Space"),
                                           KGuiItem(i18nc("@action:button", "Overwrite"), QStringLiteral("edit-clear-all")),
                                           KStandardGuiItem::cancel())
        != KMessageBox::Continue) {
        return;
    }

    auto *job = new WipeFreeSpaceJob(mountPoint, this);
    KJobWidgets::setWindow(job, this);
    KIO::getJobTracker()->registerJob(job);
    connect(job, &KJob::result, this, [this](KJob *job) {
        if (job->error() && job->error() != KJob::KilledJobError) {
            KMessageBox::error(this, job->errorString());
        }
        m_model->refresh();
    });
    job->start();
}
