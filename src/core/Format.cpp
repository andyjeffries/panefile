#include "core/Format.h"

#include <QCoreApplication>
#include <QDateTime>

#include <array>

#include <sys/stat.h>

namespace pf {

QString formatSize(quint64 bytes)
{
    static constexpr std::array<const char *, 6> kUnits{"B", "KB", "MB", "GB", "TB", "PB"};
    static constexpr double kStep = 1000.0;

    // Decimal units, not binary. Finder has shown KB/MB since 10.6, and "KiB"
    // is the one string in the window that most plainly says it was written by
    // a systems programmer rather than for the person reading it.
    if (bytes < static_cast<quint64>(kStep)) {
        // Exact, and without a decimal point: "512 B", never "0.5 KB".
        return QCoreApplication::translate("format", "%1 B").arg(bytes);
    }

    auto value = static_cast<double>(bytes);
    std::size_t unit = 0;
    while (value >= kStep && unit + 1 < kUnits.size()) {
        value /= kStep;
        ++unit;
    }

    // One decimal below 10, none above: "9.4 MB" but "94 MB". The extra digit
    // stops mattering as the number grows, and dropping it keeps the column
    // narrow.
    const int precision = value < 10.0 ? 1 : 0;
    return QStringLiteral("%1 %2")
        .arg(value, 0, 'f', precision)
        .arg(QLatin1String(kUnits.at(unit)));
}

QString counted(int count, const QString &singular, const QString &plural)
{
    return QStringLiteral("%1 %2").arg(QString::number(count), count == 1 ? singular : plural);
}

QString formatListTime(const QDateTime &when)
{
    if (!when.isValid()) {
        return {};
    }

    const QDateTime now = QDateTime::currentDateTime();
    const QDate today = now.date();
    const QDate date = when.date();

    // One shape for the whole column, so its right edge stays straight and the
    // eye can compare two rows without re-reading the format.
    //
    // It used to mix "07 Apr", "17:21" and "Nov 2025" — three different things
    // in one column, the last of which quietly threw the day away, so a file
    // from last November and one from the November before were indistinguishable.
    if (date == today) {
        return when.toString(QStringLiteral("HH:mm"));
    }
    if (date.year() == today.year()) {
        // No leading zero: "7 Apr", not "07 Apr". The column is right-aligned,
        // so the zero pads nothing and only adds noise.
        return when.toString(QStringLiteral("d MMM"));
    }
    return when.toString(QStringLiteral("d MMM yy"));
}

QString formatFullTime(const QDateTime &when)
{
    if (!when.isValid()) {
        return {};
    }
    return when.toString(QStringLiteral("yyyy-MM-dd HH:mm"));
}

QString formatPermissions(mode_t mode)
{
    QString result;
    result.reserve(10);

    if (S_ISDIR(mode)) {
        result += QLatin1Char('d');
    } else if (S_ISLNK(mode)) {
        result += QLatin1Char('l');
    } else if (S_ISCHR(mode)) {
        result += QLatin1Char('c');
    } else if (S_ISBLK(mode)) {
        result += QLatin1Char('b');
    } else if (S_ISFIFO(mode)) {
        result += QLatin1Char('p');
    } else if (S_ISSOCK(mode)) {
        result += QLatin1Char('s');
    } else {
        result += QLatin1Char('-');
    }

    struct Triple {
        mode_t read;
        mode_t write;
        mode_t execute;
        mode_t special;
        char specialExecute;   // shown in place of x when both bits are set
        char specialNoExecute; // shown in place of - when only the special bit is
    };

    static constexpr std::array<Triple, 3> kTriples{{
        {.read = S_IRUSR,
         .write = S_IWUSR,
         .execute = S_IXUSR,
         .special = S_ISUID,
         .specialExecute = 's',
         .specialNoExecute = 'S'},
        {.read = S_IRGRP,
         .write = S_IWGRP,
         .execute = S_IXGRP,
         .special = S_ISGID,
         .specialExecute = 's',
         .specialNoExecute = 'S'},
        {.read = S_IROTH,
         .write = S_IWOTH,
         .execute = S_IXOTH,
         .special = S_ISVTX,
         .specialExecute = 't',
         .specialNoExecute = 'T'},
    }};

    for (const Triple &triple : kTriples) {
        result += (mode & triple.read) != 0 ? QLatin1Char('r') : QLatin1Char('-');
        result += (mode & triple.write) != 0 ? QLatin1Char('w') : QLatin1Char('-');

        const bool executable = (mode & triple.execute) != 0;
        if ((mode & triple.special) != 0) {
            result += QLatin1Char(executable ? triple.specialExecute : triple.specialNoExecute);
        } else {
            result += executable ? QLatin1Char('x') : QLatin1Char('-');
        }
    }

    return result;
}

} // namespace pf
