#include "ui/quicklook/QuickLookView.h"

#include "core/Format.h"
#include "ui/ThemePalette.h"
#include "ui/quicklook/QuickLookLoader.h"
#include "ui/quicklook/QuickLookRegistry.h"

#include <QFileInfo>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMimeDatabase>
#include <QPushButton>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace pf::ui {
namespace {

/// §7.6: "The header bar shows filename, size, MIME description and a close
/// affordance." Assembled here so the header stays one line even when the
/// description is long.
QString subtitleFor(const QuickLookContent &content)
{
    QStringList parts;
    if (content.entry.size > 0 || !content.entry.isDir) {
        parts << formatSize(static_cast<quint64>(content.entry.size));
    }
    if (content.mimeType.isValid()) {
        parts << content.mimeType.comment();
    }
    return parts.join(QStringLiteral("  ·  "));
}

} // namespace

QuickLookView::QuickLookView(QWidget *parent)
    : QWidget(parent), m_loader(new QuickLookLoader(this)), m_header(new QWidget(this)),
      m_title(new QLabel(m_header)), m_subtitle(new QLabel(m_header)), m_footer(new QWidget(this)),
      m_hint(new QLabel(m_footer)), m_stack(new QStackedWidget(this)), m_skeleton(new QLabel)
{
    setObjectName(QStringLiteral("quickLook"));
    setAutoFillBackground(true);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // ---------------------------------------------------------------- header
    m_header->setObjectName(QStringLiteral("quickLookHeader"));
    auto *headerLayout = new QHBoxLayout(m_header);
    headerLayout->setContentsMargins(12, 8, 8, 8);
    headerLayout->setSpacing(12);

    m_title->setObjectName(QStringLiteral("quickLookTitle"));
    m_title->setTextFormat(Qt::PlainText);
    // Ignored rather than merely expanding: a long filename must not be able to
    // set a floor under the pane's width, the same trap the panel header hit.
    m_title->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    headerLayout->addWidget(m_title, 1);

    m_subtitle->setObjectName(QStringLiteral("quickLookSubtitle"));
    m_subtitle->setTextFormat(Qt::PlainText);
    m_subtitle->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    headerLayout->addWidget(m_subtitle, 0);

    auto *close = new QPushButton(QStringLiteral("✕"), m_header);
    close->setObjectName(QStringLiteral("quickLookClose"));
    close->setFlat(true);
    close->setCursor(Qt::PointingHandCursor);
    close->setFocusPolicy(Qt::NoFocus);
    close->setFixedSize(22, 22);
    connect(close, &QPushButton::clicked, this, &QuickLookView::closeRequested);
    headerLayout->addWidget(close, 0);

    layout->addWidget(m_header);

    // ---------------------------------------------------------------- content
    m_stack->setObjectName(QStringLiteral("quickLookStack"));
    layout->addWidget(m_stack, 1);

    // §7.6: "Show a skeleton/spinner state after 200 ms of loading, never
    // before." One page, reused, so no widget is built at the moment the user
    // is already waiting.
    m_skeleton->setObjectName(QStringLiteral("quickLookSkeleton"));
    m_skeleton->setAlignment(Qt::AlignCenter);
    m_skeleton->setText(tr("Loading…"));
    m_stack->addWidget(m_skeleton);

    // ---------------------------------------------------------------- footer
    m_footer->setObjectName(QStringLiteral("quickLookFooter"));
    auto *footerLayout = new QHBoxLayout(m_footer);
    footerLayout->setContentsMargins(12, 6, 12, 6);

    m_hint->setObjectName(QStringLiteral("quickLookHint"));
    m_hint->setTextFormat(Qt::PlainText);
    m_hint->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    footerLayout->addWidget(m_hint, 1);

    layout->addWidget(m_footer);

    connect(m_loader, &QuickLookLoader::loaded, this, &QuickLookView::onLoaded);
    connect(m_loader, &QuickLookLoader::loadingSlowly, this, &QuickLookView::onLoadingSlowly);

    refreshTheme();
}

QuickLookView::~QuickLookView() = default;

void QuickLookView::applySettings(const config::Settings::QuickLook &settings)
{
    m_chrome = settings.chrome;
    m_header->setVisible(m_chrome);
    m_footer->setVisible(m_chrome);

    m_loader->setDebounceInterval(settings.debounceMs);
    m_loader->setMaxReadBytes(settings.maxReadBytes);
    m_loader->setMaxDecodeMegabytes(settings.maxDecodeMb);
}

void QuickLookView::refreshTheme()
{
    const ThemePalette &palette = currentPalette();
    QPalette widgetPalette = this->palette();
    widgetPalette.setColor(QPalette::Window, palette.background);
    widgetPalette.setColor(QPalette::WindowText, palette.text);
    setPalette(widgetPalette);

    m_subtitle->setStyleSheet(
        QStringLiteral("color: %1;").arg(palette.subtext.name(QColor::HexRgb)));
    m_hint->setStyleSheet(QStringLiteral("color: %1;").arg(palette.subtext.name(QColor::HexRgb)));
    m_skeleton->setStyleSheet(
        QStringLiteral("color: %1;").arg(palette.subtext.name(QColor::HexRgb)));
}

QuickLookRegistry *QuickLookView::registry() const
{
    // §3.4: no renderer is instantiated until Quick Look is first opened, and
    // the registry is what instantiates them — so it is built here, on the
    // first file, rather than in the constructor.
    if (!m_registry) {
        const_cast<QuickLookView *>(this)->m_registry = QuickLookRegistry::createDefault();
    }
    return m_registry.get();
}

QuickLookLoader *QuickLookView::loader() const
{
    return m_loader;
}

QuickLookRenderer *QuickLookView::currentRenderer() const
{
    return m_current;
}

QString QuickLookView::currentPath() const
{
    return m_currentPath;
}

void QuickLookView::showFile(const QString &path, const FileEntry &entry)
{
    if (path.isEmpty()) {
        clear();
        return;
    }

    m_currentPath = path;

    // The renderer is chosen here, before the load, because it is the renderer
    // that decides what the loader should read: bytes, an image, or nothing at
    // all (§7.6's desiredReadBytes and wantsImage).
    static const QMimeDatabase database;
    const QMimeType mime = entry.isDir ? database.mimeTypeForName(QStringLiteral("inode/directory"))
                                       : database.mimeTypeForFile(path);

    QuickLookRenderer *renderer = registry()->rendererFor(mime, entry);

    m_title->setText(QFileInfo(path).fileName());
    m_subtitle->setText(QString());

    m_loader->request(path, entry, renderer);
}

void QuickLookView::clear()
{
    m_loader->cancel();
    m_loader->clearCache();
    m_currentPath.clear();

    if (m_current != nullptr) {
        m_current->clear();
        m_current = nullptr;
    }

    m_title->setText(QString());
    m_subtitle->setText(QString());
    m_hint->setText(QString());
    m_stack->setCurrentWidget(m_skeleton);
}

QWidget *QuickLookView::pageFor(QuickLookRenderer *renderer)
{
    const QString id = renderer->id();
    if (QWidget *existing = m_pages.value(id); existing != nullptr) {
        return existing;
    }

    QWidget *page = renderer->createWidget(m_stack);
    m_stack->addWidget(page);
    m_pages.insert(id, page);
    return page;
}

void QuickLookView::showSkeleton()
{
    m_stack->setCurrentWidget(m_skeleton);
}

void QuickLookView::onLoadingSlowly(const QString &path)
{
    if (path != m_currentPath) {
        return;
    }
    showSkeleton();
}

void QuickLookView::onLoaded(const QuickLookContent &content, QuickLookRenderer *renderer)
{
    if (content.path != m_currentPath) {
        // A load that finished after the cursor moved on. Dropping it here
        // rather than in the loader keeps the loader's cache warm: the content
        // is still worth remembering, just not worth showing.
        return;
    }

    QuickLookContent copy = content;
    QuickLookRenderer *target = renderer;

    // §7.6: "A renderer that throws or fails must degrade to HexRenderer with
    // an inline error note, never crash or blank."
    if (!content.error.isEmpty() && renderer != registry()->fallback()) {
        target = registry()->fallback();
    }

    if (target == nullptr) {
        return;
    }

    m_stack->setCurrentWidget(pageFor(target));

    if (m_current != nullptr && m_current != target) {
        m_current->clear();
    }
    m_current = target;

    target->setContent(std::move(copy));

    updateChrome(content);
}

void QuickLookView::updateChrome(const QuickLookContent &content)
{
    m_title->setText(QFileInfo(content.path).fileName());
    m_subtitle->setText(subtitleFor(content));

    QString hint = m_current != nullptr ? m_current->statusText() : QString();
    if (!content.error.isEmpty()) {
        // Inline rather than modal: §7.6 wants the fallback content *plus* the
        // note, not a dialog interrupting a preview the user only glanced at.
        hint = content.error;
    }
    m_hint->setText(hint);
}

bool QuickLookView::handleKey(QKeyEvent *event)
{
    if (m_current == nullptr) {
        return false;
    }
    if (m_current->handleKey(event)) {
        // The renderer may have changed page or zoom, which the footer reports.
        m_hint->setText(m_current->statusText());
        return true;
    }
    return false;
}

} // namespace pf::ui
