#pragma once

#include "config/Config.h"
#include "ui/quicklook/QuickLookRenderer.h"

#include <QHash>
#include <QWidget>

#include <memory>

class QLabel;
class QStackedWidget;

namespace pf::ui {

class QuickLookLoader;
class QuickLookRegistry;

/// The Quick Look pane: chrome, renderer widgets and the loader (§7.6).
///
/// This is only the content. Where it appears — floating, docked, full-screen —
/// is MainWindow's business, because the dock modes are layout decisions about
/// the window and this widget has no opinion about them beyond its own size.
///
/// Two rules from §7.6 shape the structure:
///
///   * "Renderer widgets are created once and reused, because creating a
///     QPdfView per cursor movement will be visibly slow." Hence the stack of
///     widgets keyed on renderer id, built lazily and never torn down.
///   * "A renderer that throws or fails must degrade to HexRenderer with an
///     inline error note, never crash or blank." Hence every failure path ends
///     at the fallback renderer rather than at an empty pane.
class QuickLookView : public QWidget
{
    Q_OBJECT

public:
    explicit QuickLookView(QWidget *parent = nullptr);
    ~QuickLookView() override;

    /// Applies `[quicklook]` — chrome, debounce, read and decode caps.
    void applySettings(const config::Settings::QuickLook &settings);

    /// Re-reads the theme after a hot reload.
    void refreshTheme();

    /// Shows `path`. Supersedes anything in flight; the load is debounced and
    /// off-thread, so this is cheap enough to call on every cursor movement.
    void showFile(const QString &path, const FileEntry &entry);

    /// Empties the pane and drops the decoded content it was holding.
    void clear();

    /// The file currently shown, empty when there is none.
    QString currentPath() const;

    /// §7.6's per-renderer keys — `+`/`-`/`0`, `[`/`]`, `/`. Returns true when
    /// the renderer consumed the key, in which case it must not also move the
    /// panel cursor.
    bool handleKey(QKeyEvent *event);

    /// Exposed for the tests, which check §7.6's selection-by-priority and the
    /// hex fallback without needing a window.
    QuickLookRegistry *registry() const;
    QuickLookLoader *loader() const;
    QuickLookRenderer *currentRenderer() const;

Q_SIGNALS:
    /// The close affordance in the header, or a renderer asking to be dismissed.
    void closeRequested();

private:
    void onLoaded(const QuickLookContent &content, QuickLookRenderer *renderer);
    void onLoadingSlowly(const QString &path);

    /// The stack page for a renderer, created on first use.
    QWidget *pageFor(QuickLookRenderer *renderer);

    void updateChrome(const QuickLookContent &content);
    void showSkeleton();

    std::unique_ptr<QuickLookRegistry> m_registry;
    QuickLookLoader *m_loader = nullptr;

    QWidget *m_header = nullptr;
    QLabel *m_title = nullptr;
    QLabel *m_subtitle = nullptr;
    QWidget *m_footer = nullptr;
    QLabel *m_hint = nullptr;
    QStackedWidget *m_stack = nullptr;
    QLabel *m_skeleton = nullptr;

    /// Renderer id → its page in the stack. §7.6's "created once and reused".
    QHash<QString, QWidget *> m_pages;

    QuickLookRenderer *m_current = nullptr;
    QString m_currentPath;
    bool m_chrome = true;
};

} // namespace pf::ui
