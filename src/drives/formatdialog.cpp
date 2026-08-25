/*
 * SPDX-FileCopyrightText: 2026 dolphin-poweruse
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "formatdialog.h"

#include <KLocalizedString>

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

FormatDialog::FormatDialog(QWidget *parent, const QString &devicePath, const QString &description, const QList<DriveTools::Filesystem> &filesystems)
    : QDialog(parent)
    , m_devicePath(devicePath)
{
    setWindowTitle(i18nc("@title:window", "Format Drive"));

    auto *warning = new QLabel(i18nc("@info",
                                     "<para><b>Everything on %1 will be gone.</b> Formatting writes a new, empty "
                                     "filesystem over the whole device, and there is no undo.</para>",
                                     description.isEmpty() ? devicePath : description),
                               this);
    warning->setWordWrap(true);
    warning->setTextFormat(Qt::RichText);

    m_filesystem = new QComboBox(this);
    for (const DriveTools::Filesystem &filesystem : filesystems) {
        m_filesystem->addItem(filesystem.name, filesystem.type);
    }

    m_label = new QLineEdit(this);
    m_label->setPlaceholderText(i18nc("@info:placeholder", "Optional, e.g. Backup"));
    m_label->setMaxLength(32);

    m_erase = new QCheckBox(i18nc("@option:check", "Overwrite the whole device with zeros first"), this);
    m_erase->setToolTip(i18nc("@info:tooltip",
                              "Leaves nothing of the old contents recoverable, and takes as long as writing the "
                              "device from end to end."));

    m_confirmation = new QLineEdit(this);
    m_confirmation->setPlaceholderText(devicePath);

    auto *confirmationHint = new QLabel(i18nc("@info", "Type <b>%1</b> to confirm.", devicePath), this);
    confirmationHint->setTextFormat(Qt::RichText);
    confirmationHint->setWordWrap(true);

    auto *form = new QFormLayout();
    form->addRow(i18nc("@label:listbox", "Filesystem:"), m_filesystem);
    form->addRow(i18nc("@label:textbox", "Label:"), m_label);
    form->addRow(QString(), m_erase);
    form->addRow(i18nc("@label:textbox", "Confirm:"), m_confirmation);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    m_formatButton = buttons->addButton(i18nc("@action:button", "Format"), QDialogButtonBox::AcceptRole);
    m_formatButton->setIcon(QIcon::fromTheme(QStringLiteral("edit-delete")));
    m_formatButton->setEnabled(false);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(warning);
    layout->addLayout(form);
    layout->addWidget(confirmationHint);
    layout->addWidget(buttons);

    connect(m_confirmation, &QLineEdit::textChanged, this, &FormatDialog::updateFormatButton);
    m_confirmation->setFocus();
}

QString FormatDialog::filesystemType() const
{
    return m_filesystem->currentData().toString();
}

QString FormatDialog::label() const
{
    return m_label->text().trimmed();
}

bool FormatDialog::eraseFirst() const
{
    return m_erase->isChecked();
}

void FormatDialog::updateFormatButton()
{
    m_formatButton->setEnabled(m_confirmation->text().trimmed() == m_devicePath);
}
