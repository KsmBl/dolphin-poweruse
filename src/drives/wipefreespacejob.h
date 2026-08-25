/*
 * SPDX-FileCopyrightText: 2026 dolphin-poweruse
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef WIPEFREESPACEJOB_H
#define WIPEFREESPACEJOB_H

#include <KJob>

#include <QByteArray>
#include <QFile>
#include <QString>
#include <QStringList>

class QTimer;

/**
 * @brief Fills the free space of a filesystem with zeros, then gives it back.
 *
 * What is left of a deleted file is its contents, still sitting in blocks the
 * filesystem no longer refers to. Writing zeros into every free block overwrites
 * them, and deleting the zeros afterwards returns the space.
 *
 * Existing files are never touched. The job only ever creates its own filling
 * files and deletes those again; it refuses to start if one of its names is
 * already taken rather than overwriting whatever is there.
 *
 * This is honest about what it can do: on an SSD it is not a guarantee. Wear
 * levelling means the blocks the drive hands out are not the blocks the old data
 * sits in, and over-provisioned areas are never visible from here at all. On
 * such a device, discarding (fstrim) is the tool that actually applies.
 */
class WipeFreeSpaceJob : public KJob
{
    Q_OBJECT

public:
    /*! @p mountPoint is the filesystem to fill. */
    explicit WipeFreeSpaceJob(const QString &mountPoint, QObject *parent = nullptr);
    ~WipeFreeSpaceJob() override;

    void start() override;

protected:
    bool doKill() override;

private:
    void writeChunk();
    bool openNextFile();
    void finish();
    void cleanUp();

    const QString m_mountPoint;
    /*! Every filling file created so far, so all of them are cleaned up. */
    QStringList m_fillPaths;
    QFile m_file;
    QByteArray m_zeros;
    QTimer *m_timer;
    qint64 m_written = 0;
    qint64 m_target = 0;
    int m_fileCount = 0;
    bool m_cancelled = false;
};

#endif
