#pragma once

#include "fs/Job.h"

#include <QStringList>

namespace pf::fs {

/// Copies or moves a set of paths into a destination directory (§7.4).
///
/// One class for both because they share almost everything: the same
/// enumeration, the same conflict handling, the same recursion, the same
/// symlink rules. A move differs in two places — it tries rename(2) first, and
/// it removes the source afterwards — and both are conditioned on one flag
/// rather than duplicated into a parallel class that would drift.
class TransferJob : public Job
{
    Q_OBJECT

public:
    enum class Mode {
        Copy,
        Move,
    };

    TransferJob(Mode mode, QStringList sources, const QString &destinationDirectory,
                QObject *parent = nullptr);

    QString description() const override;

    /// §7.4: "write to `name.pf-partial` and `rename` on completion", so a
    /// cancelled copy leaves no half-written file where a whole one should be.
    static constexpr QLatin1String kPartialSuffix{".pf-partial"};

    /// Paths successfully written, in order. The undo stack needs these, and a
    /// test needs them to check nothing else was created.
    QStringList createdPaths() const;

    /// For a move, the sources that were removed — what an undo has to put back.
    QStringList removedSources() const;

protected:
    bool enumerate() override;
    void execute() override;

private:
    struct Item {
        QString source;
        QString destination;
        quint64 size = 0;
        bool isDirectory = false;
        bool isSymlink = false;
    };

    bool collect(const QString &source, const QString &destinationDirectory);
    void transferOne(const Item &item);
    bool copyFileContents(const Item &item, const QString &partialPath);
    bool copySymlink(const Item &item);
    static void applyMetadata(const QString &source, const QString &destination);

    /// Resolves a conflicting destination to the path actually to be written,
    /// or empty when the item is to be skipped.
    QString resolveDestination(const Item &item);

    Mode m_mode;
    QStringList m_sources;
    QString m_destinationDirectory;

    QList<Item> m_items;
    QStringList m_createdPaths;
    QStringList m_removedSources;

    /// Directories created for the transfer, deepest last. A move removes its
    /// source directories after their contents, so they are walked in reverse.
    QStringList m_createdDirectories;
    QStringList m_sourceDirectories;
};

} // namespace pf::fs
