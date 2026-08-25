/*
 * SPDX-FileCopyrightText: 2026 dolphin-poweruse
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef DRIVETOOLS_H
#define DRIVETOOLS_H

#include <QString>
#include <QStringList>

namespace Solid
{
class Device;
}
class QWidget;

/**
 * @brief Formatting and free space wiping, both of which destroy data.
 *
 * Everything here goes through UDisks2, which asks polkit rather than needing
 * root, and refuses to touch anything the running system depends on.
 */
namespace DriveTools
{
/** A filesystem that can actually be created on this machine. */
struct Filesystem {
    QString type; ///< what UDisks2 wants: "ext4", "vfat", ...
    QString name; ///< what a person reads
};

/** Only the ones whose mkfs is installed; offering the rest would just fail. */
QList<Filesystem> availableFilesystems();

/**
 * Whether this device is something the running system needs.
 *
 * Root, /boot, /home, /usr, /var and the swap in use are refused outright: a
 * file manager should not be able to format the system out from under itself.
 */
bool isSystemCritical(const Solid::Device &device, QString *reason = nullptr);

/** The UDisks2 object path for a device, empty when it has none. */
QString udisksPath(const Solid::Device &device);

/**
 * Unmounts the device if needed and formats it. Reports failures to @p window.
 *
 * @p erase asks UDisks2 to write zeros over the whole device first, which takes
 * as long as writing the device end to end.
 */
void format(const Solid::Device &device, const QString &type, const QString &label, bool erase, QWidget *window);
}

#endif
