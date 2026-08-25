/*
 * SPDX-FileCopyrightText: 2026 dolphin-poweruse
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "wipefreespacejob.h"

#include <KLocalizedString>

#include <QDir>
#include <QFile>
#include <QStorageInfo>
#include <QTimer>

namespace
{
/*! Written per pass; big enough to be quick, small enough to stay responsive. */
constexpr qint64 s_chunkSize = 32 * 1024 * 1024;

/*!
 * Left free, so the filesystem is never actually filled to the last byte.
 * A full filesystem breaks whatever else is writing to it at the time.
 */
constexpr qint64 s_reserve = 64 * 1024 * 1024;
}

WipeFreeSpaceJob::WipeFreeSpaceJob(const QString &mountPoint, QObject *parent)
    : KJob(parent)
    , m_mountPoint(mountPoint)
    , m_timer(new QTimer(this))
{
    setCapabilities(Killable);
    m_timer->setInterval(0);
    connect(m_timer, &QTimer::timeout, this, &WipeFreeSpaceJob::writeChunk);
}

WipeFreeSpaceJob::~WipeFreeSpaceJob()
{
    // Never leave the filling file behind, whatever happened.
    if (!m_fillPath.isEmpty()) {
        QFile::remove(m_fillPath);
    }
}

void WipeFreeSpaceJob::start()
{
    const QStorageInfo info(m_mountPoint);
    if (!info.isValid() || info.isReadOnly()) {
        setError(KJob::UserDefinedError);
        setErrorText(i18nc("@info", "%1 cannot be written to.", m_mountPoint));
        emitResult();
        return;
    }

    // A filesystem living in memory has no blocks on a disk to overwrite, and
    // filling it would eat the machine's RAM instead.
    static const QList<QByteArray> memoryBacked = {"tmpfs", "ramfs", "devtmpfs"};
    if (memoryBacked.contains(info.fileSystemType())) {
        setError(KJob::UserDefinedError);
        setErrorText(
            i18nc("@info", "%1 is held in memory (%2), so there is nothing on a disk to overwrite.", m_mountPoint, QString::fromUtf8(info.fileSystemType())));
        emitResult();
        return;
    }

    m_target = qMax<qint64>(0, info.bytesAvailable() - s_reserve);
    if (m_target <= 0) {
        setError(KJob::UserDefinedError);
        setErrorText(i18nc("@info", "There is no free space to overwrite on %1.", m_mountPoint));
        emitResult();
        return;
    }

    m_fillPath = QDir(m_mountPoint).filePath(QStringLiteral(".dolphin-wipe-free-space"));
    QFile::remove(m_fillPath);

    setTotalAmount(KJob::Bytes, m_target);
    Q_EMIT description(this, i18nc("@info:progress", "Overwriting free space"), {i18nc("@info:progress", "Filesystem"), m_mountPoint});

    m_timer->start();
}

bool WipeFreeSpaceJob::doKill()
{
    m_cancelled = true;
    m_timer->stop();
    cleanUp();
    return true;
}

void WipeFreeSpaceJob::writeChunk()
{
    if (m_cancelled) {
        return;
    }

    QFile fill(m_fillPath);
    if (!fill.open(QIODevice::WriteOnly | QIODevice::Append)) {
        m_timer->stop();
        setError(KJob::UserDefinedError);
        setErrorText(i18nc("@info", "Could not write to %1: %2", m_mountPoint, fill.errorString()));
        cleanUp();
        emitResult();
        return;
    }

    static const QByteArray zeros(s_chunkSize, '\0');
    const qint64 remaining = m_target - m_written;
    const qint64 written = fill.write(zeros.constData(), qMin(remaining, s_chunkSize));
    fill.close();

    // Running out of space is the expected way to finish: the free blocks that
    // held deleted data now hold zeros.
    if (written <= 0 || m_written + written >= m_target) {
        m_written += qMax<qint64>(0, written);
        m_timer->stop();
        setProcessedAmount(KJob::Bytes, m_written);
        cleanUp();
        emitResult();
        return;
    }

    m_written += written;
    setProcessedAmount(KJob::Bytes, m_written);
}

void WipeFreeSpaceJob::cleanUp()
{
    if (m_fillPath.isEmpty()) {
        return;
    }
    QFile::remove(m_fillPath);
    m_fillPath.clear();
}
