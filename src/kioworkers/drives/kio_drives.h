/*
 * SPDX-FileCopyrightText: 2026 dolphin-poweruse
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef KIO_DRIVES_H
#define KIO_DRIVES_H

#include <KIO/WorkerBase>

#include <Solid/Device>

/**
 * @brief Lists every connected drive, volume and container as a folder.
 *
 * "drives:/" gives one entry per storage device Solid knows about. Entering an
 * entry lands in the device's mount point, mounting or unlocking it first if
 * that is still needed.
 */
class DrivesWorker : public KIO::WorkerBase
{
public:
    DrivesWorker(const QByteArray &pool, const QByteArray &app);

    KIO::WorkerResult listDir(const QUrl &url) override;
    KIO::WorkerResult stat(const QUrl &url) override;

private:
    /** The device a "drives:/<udi>" URL points at, or an invalid device. */
    Solid::Device deviceFor(const QUrl &url) const;

    /** Mounts or unlocks the device if needed and returns its mount point. */
    QString mountPointOf(Solid::Device &device, QString *error) const;

    KIO::UDSEntry entryFor(Solid::Device &device, const QString &name) const;
    KIO::UDSEntry rootEntry() const;

    /** Every storage device, in a stable order. */
    QList<Solid::Device> devices() const;
};

#endif
