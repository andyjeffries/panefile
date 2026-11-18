// A type filter such as image/* stands for well over a hundred globs. Refiltering
// a big folder with it must cost about what refiltering with no filter does.

#include "model/DirectoryModel.h"
#include "model/FileEntry.h"
#include "model/FilterSortProxy.h"
#include "platform/FileChooserRequest.h"

#include <QAbstractListModel>
#include <QElapsedTimer>
#include <QTest>

using namespace pf;

namespace {

class Model : public QAbstractListModel
{
public:
    explicit Model(int count)
    {
        static const char *ext[] = {"txt", "pdf", "jpg", "png", "cpp", "md", "log", "json"};
        for (int i = 0; i < count; ++i) {
            FileEntry entry;
            entry.name = QStringLiteral("file_%1.%2").arg(i).arg(ext[i % 8]);
            m_entries << entry;
        }
    }
    int rowCount(const QModelIndex &parent = {}) const override
    {
        return parent.isValid() ? 0 : static_cast<int>(m_entries.size());
    }
    QVariant data(const QModelIndex &index, int role) const override
    {
        const FileEntry &entry = m_entries.at(index.row());
        switch (role) {
        case Qt::DisplayRole:
        case DirectoryModel::NameRole:
            return entry.name;
        case DirectoryModel::IsDirRole:
            return entry.isDir;
        case DirectoryModel::EntryRole:
            return QVariant::fromValue(entry);
        default:
            return {};
        }
    }

private:
    QList<FileEntry> m_entries;
};

qint64 bestRefilter(FilterSortProxy &proxy)
{
    qint64 best = std::numeric_limits<qint64>::max();
    for (const QString &text : {QStringLiteral("f"), QStringLiteral("fi"), QStringLiteral("fil")}) {
        QElapsedTimer timer;
        timer.start();
        proxy.setFilterText(text);
        best = std::min(best, timer.nsecsElapsed());
    }
    return best;
}

} // namespace

class TestPerfChooser : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void aTypeFilterRefiltersAboutAsFastAsNone()
    {
        Model model(20000);

        FilterSortProxy plain;
        plain.setSourceModel(&model);
        const qint64 none = bestRefilter(plain);

        platform::FileFilter images;
        images.patterns = {{1u, QStringLiteral("image/*")}};
        FilterSortProxy filtered;
        filtered.setSourceModel(&model);
        filtered.setNameFilters(platform::globsForFilter(images));
        const qint64 with = bestRefilter(filtered);

        QVERIFY2(with < none * 5, qPrintable(QStringLiteral("none %1 ms, image/* %2 ms")
                                                 .arg(none / 1e6)
                                                 .arg(with / 1e6)));
    }
};

QTEST_MAIN(TestPerfChooser)
#include "tst_perf_chooser.moc"
