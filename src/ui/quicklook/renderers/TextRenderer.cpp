#include "ui/quicklook/renderers/TextRenderer.h"

#include "core/Format.h"

#include <QFileInfo>
#include <QFontDatabase>
#include <QKeyEvent>
#include <QPlainTextEdit>

namespace pf::ui {
namespace {

/// MIME types that are text despite not saying `text/`.
///
/// shared-mime-info classifies a great deal of plainly readable content under
/// `application/`, and a renderer that trusted the prefix would hand JSON,
/// XML and most shell scripts to the hex dump.
bool isTextualApplicationType(const QString &name)
{
    static constexpr std::array<QLatin1String, 14> kTextual{
        QLatin1String("application/json"),       QLatin1String("application/xml"),
        QLatin1String("application/javascript"), QLatin1String("application/x-shellscript"),
        QLatin1String("application/x-perl"),     QLatin1String("application/x-python"),
        QLatin1String("application/x-ruby"),     QLatin1String("application/x-php"),
        QLatin1String("application/x-yaml"),     QLatin1String("application/toml"),
        QLatin1String("application/x-desktop"),  QLatin1String("application/sql"),
        QLatin1String("application/x-awk"),      QLatin1String("application/x-m4")};

    return std::ranges::any_of(kTextual,
                               [&name](QLatin1String candidate) { return name == candidate; });
}

} // namespace

bool TextRenderer::isTextual(const QMimeType &mime)
{
    if (!mime.isValid()) {
        return false;
    }
    if (mime.name().startsWith(QLatin1String("text/"))) {
        return true;
    }
    if (isTextualApplicationType(mime.name())) {
        return true;
    }

    // Ancestry catches the rest: shared-mime-info makes most source types
    // inherit from text/plain, so a language this list has never heard of is
    // still recognised.
    return mime.inherits(QStringLiteral("text/plain"));
}

bool TextRenderer::canRender(const QMimeType &mime, const FileEntry &entry) const
{
    return !entry.isDir && isTextual(mime);
}

QWidget *TextRenderer::createWidget(QWidget *parent)
{
    if (m_view == nullptr) {
        m_view = new QPlainTextEdit(parent);
        m_view->setReadOnly(true);
        m_view->setLineWrapMode(QPlainTextEdit::NoWrap);
        m_view->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
        // Undo history on a read-only preview is pure memory: nothing can edit
        // it, and every file loaded would add to the stack.
        m_view->setUndoRedoEnabled(false);
    }
    return m_view;
}

void TextRenderer::setContent(QuickLookContent &&content)
{
    if (m_view == nullptr) {
        return;
    }

    if (!content.error.isEmpty()) {
        m_view->setPlainText(content.error);
        m_status.clear();
        return;
    }

    m_view->setPlainText(content.text);

    const int lines = static_cast<int>(content.text.count(QLatin1Char('\n'))) + 1;
    m_status = tr("%n line(s) · %1", nullptr, lines).arg(formatSize(content.entry.size));

    if (content.metadataOnly) {
        // §7.6: above max_decode_mb the file is not read at all, and saying so
        // is better than showing an empty pane that looks like an empty file.
        m_status = tr("too large to preview · %1").arg(formatSize(content.entry.size));
    }
}

void TextRenderer::toggleWrap()
{
    m_wrap = !m_wrap;
    if (m_view != nullptr) {
        m_view->setLineWrapMode(m_wrap ? QPlainTextEdit::WidgetWidth : QPlainTextEdit::NoWrap);
    }
}

bool TextRenderer::handleKey(QKeyEvent *event)
{
    // §7.6's per-renderer keys. `w` is not in its table, which lists `/` for
    // in-content search and leaves the wrap toggle unbound; binding it here
    // keeps the toggle §7.6 asks for reachable.
    if (event->key() == Qt::Key_W && event->modifiers() == Qt::NoModifier) {
        toggleWrap();
        return true;
    }
    return false;
}

void TextRenderer::clear()
{
    if (m_view != nullptr) {
        m_view->clear();
    }
    m_status.clear();
}

QString TextRenderer::statusText() const
{
    return m_status;
}

} // namespace pf::ui
