#pragma once

#include "ui/quicklook/QuickLookRenderer.h"

#include <QCoreApplication>

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
/// (§2), not a failure.
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

private:
    void toggleWrap();

    QPlainTextEdit *m_view = nullptr;
    QString m_status;
    bool m_wrap = false;
};

} // namespace pf::ui
