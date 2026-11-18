#pragma once

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>

namespace pf::platform {

/// A file-type filter from the portal: a label and the patterns it matches. A
/// pattern's kind is 0 for a glob and 1 for a MIME type.
struct FileFilter {
    QString name;
    QList<QPair<uint, QString>> patterns;

    bool operator==(const FileFilter &) const = default;
};

/// An option the application wants in the dialog: a checkbox when `options` is
/// empty, otherwise a menu of (value, label) pairs.
struct FileChoice {
    QString id;
    QString label;
    QList<QPair<QString, QString>> options;
    QString initial;

    bool operator==(const FileChoice &) const = default;
};

/// What an application asked the file chooser for, independent of how it got
/// here: the D-Bus broker builds one, the chooser window reads one.
struct FileChooserRequest {
    enum class Mode { Open, Save, SaveFiles };

    Mode mode = Mode::Open;
    QString appId;
    QString title;
    QString acceptLabel;
    bool multiple = false;
    bool directory = false;
    QList<FileFilter> filters;
    int currentFilter = -1; ///< an index into `filters`, or -1
    QList<FileChoice> choices;
    QString currentName;
    QString currentFolder;
    QString currentFile;
    QStringList files; ///< SaveFiles: the names the application will write

    QByteArray toJson() const;
    static FileChooserRequest fromJson(const QByteArray &json);
};

/// What the user picked.
struct FileChooserResult {
    QStringList paths;
    int filterIndex = -1;
    QList<QPair<QString, QString>> choices; ///< id → chosen value

    QByteArray toJson() const;

    /// Returns false when `json` is not a result document.
    static bool fromJson(const QByteArray &json, FileChooserResult *result);
};

/// The name patterns a filter stands for. Globs pass through; a MIME type
/// becomes the globs the shared MIME database knows for it, and `type/*` the
/// globs of every type under that prefix; its subtypes count too. `*/*` and
/// application/octet-stream mean every file ("*"). A MIME type the database
/// doesn't know gives nothing, which is a filter that matches no file.
QStringList globsForFilter(const FileFilter &filter);

} // namespace pf::platform
