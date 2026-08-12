#include "model/DirectoryModel.h"

#include "core/Logging.h"
#include "fs/DirectoryScanner.h"
#include <utility>

namespace pf {

DirectoryModel::DirectoryModel(QObject *parent)
    : QAbstractListModel(parent), m_scanner(std::make_unique<fs::DirectoryScanner>())
{
    connect(m_scanner.get(), &fs::DirectoryScanner::entriesReady, this,
            &DirectoryModel::onEntriesReady);
    connect(m_scanner.get(), &fs::DirectoryScanner::finished, this,
            &DirectoryModel::onScanFinished);
    connect(m_scanner.get(), &fs::DirectoryScanner::failed, this, &DirectoryModel::onScanFailed);
}

DirectoryModel::~DirectoryModel() = default;

QString DirectoryModel::path() const
{
    return m_path;
}

void DirectoryModel::setPath(const QString &path)
{
    m_path = path;
    refresh();
}

void DirectoryModel::refresh()
{
    // Clearing before the scan starts, rather than swapping the contents in
    // when it completes, is what makes a slow directory feel like it is loading
    // rather than like the application has frozen on the previous one.
    beginResetModel();
    m_entries.clear();
    m_error.clear();
    endResetModel();

    m_scanning = true;
    Q_EMIT scanStarted(m_path);
    m_scanner->scan(m_path);
}

void DirectoryModel::onEntriesReady(const QString &path, const QList<FileEntry> &entries)
{
    if (path != m_path || entries.isEmpty()) {
        return;
    }

    // §4.2: append with begin/endInsertRows so the view stays responsive rather
    // than resetting — a reset would throw away the user's scroll position and
    // selection on every one of the 200 batches of a 100,000-entry directory.
    const int first = static_cast<int>(m_entries.size());
    const int last = first + static_cast<int>(entries.size()) - 1;

    beginInsertRows({}, first, last);
    m_entries.reserve(m_entries.size() + static_cast<std::size_t>(entries.size()));
    for (const FileEntry &entry : entries) {
        m_entries.push_back(entry);
    }
    endInsertRows();

    Q_EMIT scanProgress(static_cast<int>(m_entries.size()));
}

void DirectoryModel::onScanFinished(const QString &path, int total)
{
    if (path != m_path) {
        return;
    }
    m_scanning = false;
    qCDebug(pfFs) << "scan finished" << path << total << "entries";
    Q_EMIT scanFinished(path, total);
}

void DirectoryModel::onScanFailed(const QString &path, const QString &reason)
{
    if (path != m_path) {
        return;
    }
    m_scanning = false;
    m_error = reason;
    qCDebug(pfFs) << "scan failed" << path << reason;
    Q_EMIT scanFailed(path, reason);
}

int DirectoryModel::rowCount(const QModelIndex &parent) const
{
    // A list model has no children below the root; without this a view can ask
    // for the row count of a valid index and get a nonsensical answer.
    if (parent.isValid()) {
        return 0;
    }
    return static_cast<int>(m_entries.size());
}

const FileEntry *DirectoryModel::entryAt(int row) const
{
    if (row < 0 || std::cmp_greater_equal(row, m_entries.size())) {
        return nullptr;
    }
    return &m_entries[static_cast<std::size_t>(row)];
}

int DirectoryModel::indexOfName(const QString &name) const
{
    for (std::size_t i = 0; i < m_entries.size(); ++i) {
        if (m_entries[i].name == name) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

QVariant DirectoryModel::data(const QModelIndex &index, int role) const
{
    const FileEntry *entry = entryAt(index.row());
    if (entry == nullptr) {
        return {};
    }

    switch (role) {
    case Qt::DisplayRole:
    case NameRole:
        return entry->name;
    case EntryRole:
        return QVariant::fromValue(*entry);
    case SizeRole:
        return entry->size;
    case ModifiedRole:
        return entry->modified;
    case IsDirRole:
        return entry->isDir;
    case Qt::ToolTipRole:
        if (entry->isSymlink && !entry->linkTarget.isEmpty()) {
            // QT_USE_QSTRINGBUILDER makes a concatenation an expression
            // template, not a QString, so the conversion has to be explicit.
            return QString(entry->name + QStringLiteral(" → ") + entry->linkTarget);
        }
        return entry->name;
    default:
        return {};
    }
}

QHash<int, QByteArray> DirectoryModel::roleNames() const
{
    QHash<int, QByteArray> names = QAbstractListModel::roleNames();
    names[EntryRole] = "entry";
    names[IconRole] = "icon";
    names[ThumbnailRole] = "thumbnail";
    names[MatchSpansRole] = "matchSpans";
    names[NameRole] = "name";
    names[SizeRole] = "size";
    names[ModifiedRole] = "modified";
    names[IsDirRole] = "isDir";
    return names;
}

bool DirectoryModel::isScanning() const
{
    return m_scanning;
}

QString DirectoryModel::errorMessage() const
{
    return m_error;
}

} // namespace pf
