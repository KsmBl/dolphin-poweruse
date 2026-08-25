/*
 * SPDX-FileCopyrightText: 2026 dolphin-poweruse
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef FORMATDIALOG_H
#define FORMATDIALOG_H

#include "drivetools.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QPushButton;

/**
 * @brief Asks what to format a drive as, and makes sure it was meant.
 *
 * Formatting cannot be undone, so the button stays disabled until the device
 * node has been typed out by hand. Picking the wrong entry in a list is easy;
 * typing /dev/sdb1 by accident is not.
 */
class FormatDialog : public QDialog
{
    Q_OBJECT

public:
    FormatDialog(QWidget *parent, const QString &devicePath, const QString &description, const QList<DriveTools::Filesystem> &filesystems);

    QString filesystemType() const;
    QString label() const;
    bool eraseFirst() const;

private:
    void updateFormatButton();

    const QString m_devicePath;

    QComboBox *m_filesystem;
    QLineEdit *m_label;
    QCheckBox *m_erase;
    QLineEdit *m_confirmation;
    QPushButton *m_formatButton;
};

#endif
