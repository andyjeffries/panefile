#pragma once

#include "fs/Job.h"
#include "fs/Trash.h"

namespace pf::fs {

/// Converts images to another format, or rotates and flips them in place
/// (§7.14's file-type actions).
///
/// Conversions write a new file beside the original and leave the original
/// alone. Rotate and flip replace the file, but the original goes to the trash
/// first so Ctrl+Z brings it back: the new version is written to a temporary
/// file, the original is trashed, and only then is the temporary renamed into
/// place — so no step can leave the user with neither.
class ImageJob : public Job
{
    Q_OBJECT

public:
    enum class Op {
        ConvertPng,
        ConvertJpeg,
        ConvertWebp,
        ConvertAvif,
        RotateLeft,
        RotateRight,
        FlipHorizontal,
        FlipVertical,
    };

    ImageJob(Op op, QStringList paths, QObject *parent = nullptr);

    /// Uses a specific trash, for testing against a temporary directory.
    void setTrash(Trash trash);

    static bool isConversion(Op op);
    /// The target file extension ("webp") and Qt image-writer format name
    /// ("webp", "jpeg") of a conversion.
    static QString extensionFor(Op op);
    static QByteArray formatFor(Op op);

    QString description() const override;

    /// Files this job wrote that did not exist before (conversions).
    QStringList createdPaths() const;

    /// Files replaced in place, each with the trashed original (rotate, flip).
    QList<QPair<QString, TrashedItem>> replacedItems() const;

protected:
    bool enumerate() override;
    void execute() override;

private:
    void convertOne(const QString &path);
    void transformOne(const QString &path);

    Op m_op;
    QStringList m_paths;
    Trash m_trash;
    QStringList m_created;
    QList<QPair<QString, TrashedItem>> m_replaced;
};

} // namespace pf::fs
