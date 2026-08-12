#pragma once

#include <QList>
#include <QString>
#include <QWidget>

class QListWidget;
class QListWidgetItem;

namespace pf::ui {

/// Home, XDG user directories, pinned directories and mounted volumes (§5.1).
///
/// §3.4 requires this to be constructed empty and populated on idle: resolving
/// XDG user directories reads a config file, and enumerating mounts is worse.
/// None of it is needed to draw the first panel, so populate() is called from
/// the deferred startup queue rather than from the constructor.
class Sidebar : public QWidget
{
    Q_OBJECT

public:
    explicit Sidebar(QWidget *parent = nullptr);

    /// Fills in the standard places. Safe to call more than once; later calls
    /// refresh rather than duplicate.
    void populate();

    /// §6.3's `pinned_directory` (`P`): pins or unpins, returning what it did
    /// so the caller can report it.
    bool togglePin(const QString &path);
    bool isPinned(const QString &path) const;

    QStringList pinnedPaths() const;
    void setPinnedPaths(const QStringList &paths);

    /// The path under the sidebar's own cursor, or empty.
    QString currentPath() const;

Q_SIGNALS:
    /// The user chose a place. The panel controller decides which panel it
    /// opens in; the sidebar deliberately does not know.
    void placeActivated(const QString &path);

    void pinnedPathsChanged();

private:
    void addHeading(const QString &title);
    void addPlace(const QString &title, const QString &path);

    QListWidget *m_list = nullptr;
    QStringList m_pinned;
    bool m_populated = false;
};

} // namespace pf::ui
