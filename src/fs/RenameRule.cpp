#include "fs/RenameRule.h"

#include <QDateTime>

namespace pf::fs {
namespace {

/// Finder's date suffix: `2026-08-13 at 09.41`. Dots rather than colons in the
/// time, because a colon is a path separator on some filesystems and a
/// perfectly ordinary character on others — the safe spelling is the one Finder
/// already uses.
QString dateStamp()
{
    return QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd 'at' HH.mm"));
}

/// The counter's zero-padded form, `00001`.
QString counter(int value)
{
    return QStringLiteral("%1").arg(value, 5, 10, QLatin1Char('0'));
}

} // namespace

QString RenameRule::apply(const QString &name, int index) const
{
    switch (mode) {
    case RenameMode::ReplaceText: {
        if (find.isEmpty()) {
            // Replacing nothing would otherwise insert the replacement between
            // every character, which is never what anyone means by an empty
            // Find box.
            return name;
        }
        QString result = name;
        return result.replace(find, replaceWith,
                              caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive);
    }

    case RenameMode::AddText:
        if (addText.isEmpty()) {
            return name;
        }
        return addPosition == AddPosition::Before ? addText + name : name + addText;

    case RenameMode::Format: {
        // An empty custom text degrades to numbering the original names rather
        // than producing a directory of files called "1", "2", "3" — which is
        // what Finder does, and is the recoverable behaviour.
        const QString base = customText.isEmpty() ? name : customText;

        QString token;
        switch (nameFormat) {
        case NameFormat::NameAndIndex:
            token = QString::number(startNumber + index);
            break;
        case NameFormat::NameAndCounter:
            token = counter(startNumber + index);
            break;
        case NameFormat::NameAndDate:
            token = dateStamp();
            break;
        }

        // Built rather than returned from the conditional: QT_USE_QSTRINGBUILDER
        // gives each branch its own expression-template type, and the two are
        // not the same type even though both become a QString.
        if (formatPosition == FormatPosition::AfterName) {
            return base + QLatin1Char(' ') + token;
        }
        return token + QLatin1Char(' ') + base;
    }
    }

    return name;
}

QList<QString> RenameRule::applyAll(const QList<QString> &names) const
{
    QList<QString> result;
    result.reserve(names.size());

    for (int i = 0; i < names.size(); ++i) {
        result.append(apply(names.at(i), i));
    }
    return result;
}

QString RenameRule::exampleFor(const QString &name) const
{
    // Always the first item's result, because that is the one whose number the
    // user is choosing when they set the starting number.
    return apply(name, 0);
}

} // namespace pf::fs
