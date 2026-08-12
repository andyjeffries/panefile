#include "core/Format.h"

#include <QCoreApplication>
#include <QDateTime>

#include <array>

#include <sys/stat.h>

namespace pf {

QString formatSize(quint64 bytes)
{
    static constexpr std::array<const char *, 6> kUnits{"B", "KiB", "MiB", "GiB", "TiB", "PiB"};
    static constexpr double kStep = 1024.0;

    if (bytes < 1024) {
        // Exact, and without a decimal point: "512 B", never "0.5 KiB".
        return QCoreApplication::translate("format", "%1 B").arg(bytes);
    }

    double value = static_cast<double>(bytes);
    std::size_t unit = 0;
    while (value >= kStep && unit + 1 < kUnits.size()) {
        value /= kStep;
        ++unit;
    }

    // One decimal below 10, none above: "9.4 MiB" but "94 MiB". The extra digit
    // stops mattering as the number grows, and dropping it keeps the column
    // narrow.
    const int precision = value < 10.0 ? 1 : 0;
    return QStringLiteral("%1 %2")
        .arg(value, 0, 'f', precision)
        .arg(QLatin1String(kUnits.at(unit)));
}

QString formatListTime(const QDateTime &when)
{
    if (!when.isValid()) {
        return {};
    }

    const QDateTime now = QDateTime::currentDateTime();
    const QDate today = now.date();
    const QDate date = when.date();

    if (date == today) {
        return when.toString(QStringLiteral("HH:mm"));
    }
    if (date.year() == today.year()) {
        return when.toString(QStringLiteral("dd MMM"));
    }
    return when.toString(QStringLiteral("MMM yyyy"));
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
        {S_IRUSR, S_IWUSR, S_IXUSR, S_ISUID, 's', 'S'},
        {S_IRGRP, S_IWGRP, S_IXGRP, S_ISGID, 's', 'S'},
        {S_IROTH, S_IWOTH, S_IXOTH, S_ISVTX, 't', 'T'},
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

}   // namespace pf
