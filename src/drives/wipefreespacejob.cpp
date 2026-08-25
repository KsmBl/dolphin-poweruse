/*
 * SPDX-FileCopyrightText: 2026 dolphin-poweruse
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "wipefreespacejob.h"

#include <KLocalizedString>

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStorageInfo>
#include <QTimer>

#include <unistd.h>

namespace
{
/*! Written per pass; big enough to be quick, small enough to stay responsive. */
constexpr qint64 s_chunkSize = 32 * 1024 * 1024;

/*!
 * A filling file never grows past this, so the job rolls over to a new one
 * instead of running into a filesystem's maximum file size. FAT32 stops at
 * 4 GiB, and a write refused for that reason looks exactly like a full disk.
 */
constexpr qint64 s_maxFileSize = 1024 * 1024 * 1024;

/*!
 * Left free, so the filesystem is never actually filled to the last byte: a
 * full filesystem breaks whatever else is writing to it at the time. Scaled to
 * the size of what is being wiped, so a small card is not mostly skipped.
 */
qint64 reserveFor(qint64 available)
{
    return qBound<qint64>(qint64(1024) * 1024, available / 20, qint64(64) * 1024 * 1024);
}
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
    // Never leave a filling file behind, whatever happened.
    cleanUp();
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

    const qint64 available = info.bytesAvailable();
    m_target = qMax<qint64>(0, available - reserveFor(available));
    if (m_target <= 0) {
        setError(KJob::UserDefinedError);
        setErrorText(i18nc("@info", "There is no free space to overwrite on %1.", m_mountPoint));
        emitResult();
        return;
    }

    m_zeros = QByteArray(s_chunkSize, '\0');

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

bool WipeFreeSpaceJob::openNextFile()
{
    const QString path =
        QDir(m_mountPoint).filePath(QStringLiteral(".dolphin-wipe-free-space-%1-%2").arg(QCoreApplication::applicationPid()).arg(++m_fileCount));

    // Only ever delete what this job made. If the name is somehow taken, stop
    // rather than destroy a file that belongs to someone else.
    if (QFileInfo::exists(path)) {
        m_timer->stop();
        setError(KJob::UserDefinedError);
        setErrorText(xi18nc("@info", "<filename>%1</filename> already exists, so the free space was left alone.", path));
        cleanUp();
        emitResult();
        return false;
    }

    m_file.setFileName(path);
    if (!m_file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
        m_timer->stop();
        setError(KJob::UserDefinedError);
        setErrorText(i18nc("@info", "Could not write to %1: %2", m_mountPoint, m_file.errorString()));
        cleanUp();
        emitResult();
        return false;
    }

    m_fillPaths.append(path);
    return true;
}

void WipeFreeSpaceJob::writeChunk()
{
    if (m_cancelled) {
        return;
    }

    if (!m_file.isOpen() && !openNextFile()) {
        return; // openNextFile() has already reported why.
    }

    // Roll over before a filesystem's maximum file size can refuse the write,
    // which would be indistinguishable from having run out of space.
    const qint64 room = s_maxFileSize - m_file.size();
    const qint64 wanted = qMin(qMin(m_target - m_written, s_chunkSize), room);
    if (wanted <= 0) {
        m_file.close();
        return;
    }

    const qint64 written = m_file.write(m_zeros.constData(), wanted);

    // Push it out to the device. Pages of a file that is deleted while they are
    // still only in the page cache are dropped and never written at all, and
    // then nothing was overwritten -- which is the whole point of the job.
    const bool flushed = m_file.flush() && ::fsync(m_file.handle()) == 0;

    if (written > 0) {
        m_written += written;
        setProcessedAmount(KJob::Bytes, m_written);
    }

    if (written < wanted || !flushed) {
        const QFileDevice::FileError reason = m_file.error();
        const QString reasonText = m_file.errorString();
        m_file.close();

        // Running out of space is the expected way to finish: the free blocks
        // that held deleted data now hold zeros.
        if (reason != QFileDevice::NoError && reason != QFileDevice::ResourceError) {
            m_timer->stop();
            setError(KJob::UserDefinedError);
            setErrorText(i18nc("@info", "Could not write to %1: %2", m_mountPoint, reasonText));
            cleanUp();
            emitResult();
            return;
        }
        finish();
        return;
    }

    if (m_written >= m_target) {
        finish();
    }
}

void WipeFreeSpaceJob::finish()
{
    m_timer->stop();
    m_file.close();
    cleanUp();
    emitResult();
}

void WipeFreeSpaceJob::cleanUp()
{
    m_zeros.clear();
    if (m_file.isOpen()) {
        m_file.close();
    }
    for (const QString &path : std::as_const(m_fillPaths)) {
        QFile::remove(path);
    }
    m_fillPaths.clear();
}
