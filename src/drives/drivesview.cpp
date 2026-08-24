/*
 * SPDX-FileCopyrightText: 2026 dolphin-custom
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "drivesview.h"

#include "drivesmodel.h"

#include <KIO/Global>
#include <KLocalizedString>

#include <QApplication>
#include <QFontMetrics>
#include <QIcon>
#include <QPainter>
#include <QStyleOptionProgressBar>

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

    // Device node on top, mount point below it.
    const QString devicePath = index.data(DrivesModel::DevicePathRole).toString();
    QString mountPoint = index.data(DrivesModel::MountPointRole).toString();
    if (mountPoint.isEmpty()) {
        mountPoint = index.data(DrivesModel::AccessibleRole).toBool() ? QString() : i18nc("@item:intable", "Not mounted");
    }

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
                      boldMetrics.elidedText(devicePath, Qt::ElideMiddle, textRect.width()));

    painter->setFont(opt.font);
    if (!selected) {
        painter->setPen(opt.palette.color(QPalette::Disabled, QPalette::Text));
    }
    painter->drawText(QRect(textRect.left(), firstTop + lineHeight, textRect.width(), lineHeight),
                      Qt::AlignLeft | Qt::AlignVCenter,
                      opt.fontMetrics.elidedText(mountPoint, Qt::ElideMiddle, textRect.width()));
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

    connect(this, &QAbstractItemView::activated, this, [this](const QModelIndex &index) {
        const QUrl url = index.data(DrivesModel::TargetUrlRole).toUrl();
        if (url.isValid()) {
            Q_EMIT driveActivated(url);
        }
    });
}

void DrivesView::refresh()
{
    m_model->refresh();
}
