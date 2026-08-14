#include "config/TomlWriter.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStringList>

namespace pf::config {
namespace {

/// The table a line opens, or empty if it does not open one.
///
/// Only top-level tables, because that is all Panefile's files use. A nested
/// `[a.b]` is returned whole and simply will not match a caller asking for `a`,
/// which is the safe outcome: nothing is edited rather than the wrong thing.
QString tableOpenedBy(const QString &line)
{
    const QString trimmed = line.trimmed();
    if (!trimmed.startsWith(QLatin1Char('[')) || !trimmed.endsWith(QLatin1Char(']'))) {
        return {};
    }
    // Array-of-tables syntax is not something these files use, and treating it
    // as an ordinary table would edit the wrong thing.
    if (trimmed.startsWith(QLatin1String("[["))) {
        return {};
    }
    return trimmed.mid(1, trimmed.size() - 2).trimmed();
}

/// The key a line assigns, or empty if it is a comment, a blank or a table
/// header. Quoted keys are not something these files use and are left alone.
QString keyAssignedBy(const QString &line)
{
    const QString trimmed = line.trimmed();
    if (trimmed.isEmpty() || trimmed.startsWith(QLatin1Char('#')) ||
        trimmed.startsWith(QLatin1Char('['))) {
        return {};
    }

    const qsizetype equals = trimmed.indexOf(QLatin1Char('='));
    if (equals <= 0) {
        return {};
    }
    return trimmed.left(equals).trimmed();
}

/// Replaces the value in an assignment, keeping the key, the spacing around the
/// `=`, and any trailing comment exactly as they were.
///
/// The trailing comment matters more than it looks: the default config explains
/// several keys with `# name | size | modified` after the value, and losing that
/// on the first edit would be the same failure as losing the block comments.
QString replaceValueIn(const QString &line, const QString &value)
{
    const qsizetype equals = line.indexOf(QLatin1Char('='));
    if (equals < 0) {
        return line;
    }

    const QString beforeValue = line.left(equals + 1);
    const QString afterEquals = line.mid(equals + 1);

    // A `#` inside a quoted value is not a comment. Walk the rest of the line
    // tracking quotes so the split happens in the right place.
    bool inString = false;
    QChar quoteChar;
    qsizetype commentAt = -1;
    for (qsizetype i = 0; i < afterEquals.size(); ++i) {
        const QChar c = afterEquals.at(i);
        if (inString) {
            if (c == QLatin1Char('\\')) {
                ++i;
            } else if (c == quoteChar) {
                inString = false;
            }
        } else if (c == QLatin1Char('"') || c == QLatin1Char('\'')) {
            inString = true;
            quoteChar = c;
        } else if (c == QLatin1Char('#')) {
            commentAt = i;
            break;
        }
    }

    const QString comment = commentAt >= 0 ? afterEquals.mid(commentAt) : QString();

    // One space after the `=`, and the original gap before any trailing
    // comment, so a column of aligned comments stays aligned.
    QString spacingBeforeComment;
    if (commentAt >= 0) {
        // The run of spaces immediately before the comment, so a column of
        // aligned trailing comments stays aligned after an edit.
        const QString between = afterEquals.left(commentAt);
        qsizetype spaces = 0;
        for (qsizetype i = between.size() - 1; i >= 0 && between.at(i).isSpace(); --i) {
            ++spaces;
        }
        spacingBeforeComment = QString(spaces, QLatin1Char(' '));
    }

    return beforeValue + QLatin1Char(' ') + value + spacingBeforeComment + comment;
}

TomlWriter::Result readLines(const QString &filePath, QStringList *lines, bool *existed)
{
    TomlWriter::Result result;
    QFile file(filePath);
    *existed = file.exists();

    if (!*existed) {
        result.ok = true;
        return result;
    }

    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        result.error = file.errorString();
        return result;
    }

    // Kept as split lines rather than a single string so a file with no
    // trailing newline round-trips unchanged.
    *lines = QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'));
    result.ok = true;
    return result;
}

TomlWriter::Result writeLines(const QString &filePath, const QStringList &lines)
{
    TomlWriter::Result result;

    QDir().mkpath(QFileInfo(filePath).absolutePath());

    // QSaveFile: a settings file half-written because the disk filled up is a
    // configuration the application may refuse to start with. This writes to a
    // temporary and renames, so the file is either the old one or the new one.
    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        result.error = file.errorString();
        return result;
    }

    file.write(lines.join(QLatin1Char('\n')).toUtf8());
    if (!file.commit()) {
        result.error = file.errorString();
        return result;
    }

    result.ok = true;
    result.changed = true;
    return result;
}

} // namespace

QString TomlWriter::quote(const QString &text)
{
    QString escaped = text;
    escaped.replace(QLatin1Char('\\'), QLatin1String("\\\\"));
    escaped.replace(QLatin1Char('"'), QLatin1String("\\\""));
    return QLatin1Char('"') + escaped + QLatin1Char('"');
}

QString TomlWriter::boolean(bool value)
{
    return value ? QStringLiteral("true") : QStringLiteral("false");
}

QString TomlWriter::number(long long value)
{
    return QString::number(value);
}

TomlWriter::Result TomlWriter::setValue(const QString &filePath, const QString &table,
                                        const QString &key, const QString &value)
{
    QStringList lines;
    bool existed = false;
    Result read = readLines(filePath, &lines, &existed);
    if (!read.ok) {
        return read;
    }

    // The top level is the "table" before any header, which is where
    // theme.toml keeps `name` and `follow_system`.
    QString currentTable;
    qsizetype tableEndsAt = -1;

    for (qsizetype i = 0; i < lines.size(); ++i) {
        const QString &line = lines.at(i);

        if (const QString opened = tableOpenedBy(line); !opened.isEmpty()) {
            // Leaving the table we wanted: remember where it ended, in case the
            // key has to be appended to it.
            if (currentTable == table && tableEndsAt < 0) {
                tableEndsAt = i;
            }
            currentTable = opened;
            continue;
        }

        if (currentTable != table || keyAssignedBy(line) != key) {
            continue;
        }

        const QString replaced = replaceValueIn(line, value);
        if (replaced == line) {
            Result unchanged;
            unchanged.ok = true;
            unchanged.changed = false;
            return unchanged;
        }

        lines[i] = replaced;
        return writeLines(filePath, lines);
    }

    // The key is not there. Append it to its table, or the table to the file.
    if (currentTable == table && tableEndsAt < 0) {
        tableEndsAt = lines.size();
    }

    if (tableEndsAt >= 0) {
        // Just before the blank lines that separate this table from the next,
        // so the file keeps its spacing rather than gaining a stray gap.
        qsizetype insertAt = tableEndsAt;
        while (insertAt > 0 && lines.at(insertAt - 1).trimmed().isEmpty()) {
            --insertAt;
        }
        lines.insert(insertAt, QStringLiteral("%1 = %2").arg(key, value));
        return writeLines(filePath, lines);
    }

    // A top-level key with nowhere to go belongs at the very top, above the
    // first table header — anywhere else and it would silently become a member
    // of whichever table preceded it.
    if (table.isEmpty()) {
        lines.prepend(QStringLiteral("%1 = %2").arg(key, value));
        return writeLines(filePath, lines);
    }

    if (!lines.isEmpty() && !lines.constLast().trimmed().isEmpty()) {
        lines.append(QString());
    }
    lines.append(QStringLiteral("[%1]").arg(table));
    lines.append(QStringLiteral("%1 = %2").arg(key, value));
    lines.append(QString());
    return writeLines(filePath, lines);
}

TomlWriter::Result TomlWriter::removeValue(const QString &filePath, const QString &table,
                                           const QString &key)
{
    QStringList lines;
    bool existed = false;
    const Result read = readLines(filePath, &lines, &existed);
    if (!read.ok || !existed) {
        Result absent;
        absent.ok = read.ok;
        absent.error = read.error;
        return absent;
    }

    QString currentTable;
    for (qsizetype i = 0; i < lines.size(); ++i) {
        if (const QString opened = tableOpenedBy(lines.at(i)); !opened.isEmpty()) {
            currentTable = opened;
            continue;
        }
        if (currentTable == table && keyAssignedBy(lines.at(i)) == key) {
            lines.removeAt(i);
            return writeLines(filePath, lines);
        }
    }

    Result unchanged;
    unchanged.ok = true;
    unchanged.changed = false;
    return unchanged;
}

} // namespace pf::config
