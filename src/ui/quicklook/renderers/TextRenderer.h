#pragma once

#include "ui/quicklook/QuickLookRenderer.h"

#include <QCoreApplication>
#include <QPointer>
#include <QSyntaxHighlighter>

class QPlainTextEdit;

namespace pf::ui {

/// Text and source files (§7.6).
///
/// "Full file up to the read cap, syntax highlighted via KSyntaxHighlighting,
/// line numbers, wrap toggle, in-content search."
///
/// Syntax highlighting arrives through the optional plugin of §3.4 — loading
/// KSyntaxHighlighting's definition repository costs tens of milliseconds and
/// must never happen at startup, so this renderer works without it and asks for
/// it only once it has text to highlight. Plain text is the documented fallback
/// (§2), not a failure — both when the pf-syntax plugin is not installed and
/// when the file is too long to highlight without stalling (kHighlightLimit).
class TextRenderer : public QuickLookRenderer
{
    // tr() without QObject: a renderer implements an interface and has no
    // need of the meta-object system otherwise.
    Q_DECLARE_TR_FUNCTIONS(TextRenderer)

public:
    QString id() const override { return QStringLiteral("text"); }

    bool canRender(const QMimeType &mime, const FileEntry &entry) const override;
    int priority() const override { return 10; }

    /// §7.6's `max_read_bytes` (default 64 MiB) caps this; the loader applies
    /// the configured value, and this is the renderer's own upper bound.
    qint64 desiredReadBytes() const override { return 64LL * 1024 * 1024; }

    QWidget *createWidget(QWidget *parent) override;
    void setContent(QuickLookContent &&content) override;
    void clear() override;
    QString statusText() const override;
    bool handleKey(QKeyEvent *event) override;

    /// Whether a MIME type is text as far as this renderer is concerned.
    /// Broader than `text/*`: JSON, XML, shell scripts and most source files
    /// are `application/*` by MIME yet plainly text to a reader.
    static bool isTextual(const QMimeType &mime);

    /// Above this many characters the text is shown plain. QSyntaxHighlighter
    /// highlights the whole document synchronously on the GUI thread, so the
    /// cap is what keeps §11's "Quick Look open → first paint, 4 MB text file,
    /// < 120 ms" true with the plugin installed.
    ///
    /// Measured on C++ (release, Arch, 2026): highlighting adds about 100 ms
    /// per million characters on top of the plain-text layout — 26 ms at 256 K,
    /// 107 ms at 1 M. At 1 M the highlight alone would spend the whole budget;
    /// at 256 K it is a fifth of it, and few hand-written source files are
    /// longer.
    static constexpr qsizetype kHighlightLimit = qsizetype{256} * 1024;

    /// Whether a highlighter is attached to the current file.
    bool isHighlighted() const;

private:
    void toggleWrap();
    void highlight(const QuickLookContent &content);

    QPlainTextEdit *m_view = nullptr;
    QPointer<QSyntaxHighlighter> m_highlighter;
    QString m_status;
    bool m_wrap = false;
};

} // namespace pf::ui
