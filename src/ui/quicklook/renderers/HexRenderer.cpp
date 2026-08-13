#include "ui/quicklook/renderers/HexRenderer.h"

#include "core/Format.h"
#include "ui/ThemePalette.h"

#include <QFileInfo>
#include <QFontDatabase>
#include <QPlainTextEdit>

namespace pf::ui {
namespace {

constexpr int kBytesPerLine = 16;

} // namespace

bool HexRenderer::canRender(const QMimeType &mime, const FileEntry &entry) const
{
    Q_UNUSED(mime)
    Q_UNUSED(entry)
    // Everything. That is the point: §7.6 makes this the fallback, and a
    // fallback that could decline would leave files with no renderer at all.
    return true;
}

QString HexRenderer::formatDump(const QByteArray &bytes, qint64 offset)
{
    QString out;
    // Three characters per byte in the hex column, one in the ASCII column,
    // plus the offset and separators — reserving avoids a reallocation per line
    // on a 4 KiB dump.
    out.reserve((bytes.size() * 4) + ((bytes.size() / kBytesPerLine) * 16));

    for (qsizetype line = 0; line < bytes.size(); line += kBytesPerLine) {
        out += QStringLiteral("%1  ").arg(offset + line, 8, 16, QLatin1Char('0'));

        QString ascii;
        for (int column = 0; column < kBytesPerLine; ++column) {
            const qsizetype index = line + column;

            if (index < bytes.size()) {
                const auto byte = static_cast<unsigned char>(bytes.at(index));
                out += QStringLiteral("%1 ").arg(byte, 2, 16, QLatin1Char('0'));
                // Printable ASCII only. Anything else becomes a dot, including
                // the upper half — a byte is not a character until something
                // says which encoding it is in, and a hex dump is what you look
                // at when nothing can.
                ascii += (byte >= 0x20 && byte < 0x7f) ? QLatin1Char(static_cast<char>(byte))
                                                       : QLatin1Char('.');
            } else {
                // Padded, so the ASCII column of a short final line still lines
                // up with every line above it.
                out += QStringLiteral("   ");
            }

            if (column == (kBytesPerLine / 2) - 1) {
                out += QLatin1Char(' ');
            }
        }

        out += QStringLiteral(" |") + ascii + QStringLiteral("|\n");
    }

    return out;
}

QWidget *HexRenderer::createWidget(QWidget *parent)
{
    if (m_view == nullptr) {
        m_view = new QPlainTextEdit(parent);
        m_view->setReadOnly(true);
        m_view->setLineWrapMode(QPlainTextEdit::NoWrap);
        // A fixed-pitch font is not decoration here: the columns only line up
        // if every character is the same width.
        m_view->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    }
    return m_view;
}

void HexRenderer::setContent(QuickLookContent &&content)
{
    if (m_view == nullptr) {
        return;
    }

    QString header;
    if (!content.error.isEmpty()) {
        // §7.6: a renderer that fails "must degrade to HexRenderer with an
        // inline error note". The note goes at the top, where it is read before
        // the dump rather than after it.
        header += content.error + QStringLiteral("\n\n");
    }

    header += QStringLiteral("%1\n%2, %3\n\n")
                  .arg(QFileInfo(content.path).fileName(),
                       content.mimeType.isValid() ? content.mimeType.comment()
                                                  : QStringLiteral("unknown type"),
                       formatSize(content.entry.size));

    m_view->setPlainText(header + formatDump(content.bytes));

    m_status =
        content.entry.size > static_cast<quint64>(kDumpBytes)
            ? tr("first %1 of %2").arg(formatSize(kDumpBytes), formatSize(content.entry.size))
            : formatSize(content.entry.size);
}

void HexRenderer::clear()
{
    if (m_view != nullptr) {
        m_view->clear();
    }
    m_status.clear();
}

QString HexRenderer::statusText() const
{
    return m_status;
}

} // namespace pf::ui
