// pf-pdf: paged PDF rendering for Quick Look (§7.6).
//
// "Paged rendering via QtPdf (preferred) or poppler-qt6, with page navigation
// and zoom." The preference is reversed here. On Arch, Qt6Pdf ships inside
// qt6-webengine, which would put Chromium in a file manager's dependency tree,
// and CI and both PKGBUILDs already standardise on poppler-qt6; QtPdf is the
// fallback for a system without poppler. One backend is compiled per build,
// chosen by src/plugins/CMakeLists.txt.

#include "plugins/PluginInterfaces.h"

#include <QFutureWatcher>
#include <QKeyEvent>
#include <QLabel>
#include <QLoggingCategory>
#include <QObject>
#include <QPixmap>
#include <QScrollArea>
#include <QScrollBar>
#include <QThreadPool>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <atomic>
#include <memory>

#ifdef PF_PDF_POPPLER
#include <poppler-qt6.h>
#elif defined(PF_PDF_QTPDF)
#include <QPdfDocument>
#else
#error "pf-pdf needs PF_PDF_POPPLER or PF_PDF_QTPDF"
#endif

namespace pf::plugins {
namespace {

using ui::QuickLookContent;

#ifdef PF_PDF_POPPLER
// The plugin's own category rather than pf's: it links nothing from pf's
// layers (plugins/PluginInterfaces.h). Same panefile.* tree, so --verbose and
// QT_LOGGING_RULES treat it like the rest.
Q_LOGGING_CATEGORY(pfPdf, "panefile.plugins.pdf")
#endif

/// One open document. Used only from the renderer's single worker thread, one
/// job at a time, which is all either library promises to cope with.
class Document
{
public:
    /// Opens the file; returns an error message, or an empty string.
    QString open(const QString &path)
    {
#ifdef PF_PDF_POPPLER
        m_document = Poppler::Document::load(path);
        if (!m_document) {
            return QObject::tr("Not a readable PDF");
        }
        if (m_document->isLocked()) {
            m_document.reset();
            return QObject::tr("This PDF is password-protected");
        }
        m_document->setRenderHint(Poppler::Document::Antialiasing);
        m_document->setRenderHint(Poppler::Document::TextAntialiasing);
        return {};
#else
        m_document = std::make_unique<QPdfDocument>();
        const QPdfDocument::Error error = m_document->load(path);
        if (error == QPdfDocument::Error::IncorrectPassword) {
            m_document.reset();
            return QObject::tr("This PDF is password-protected");
        }
        if (error != QPdfDocument::Error::None) {
            m_document.reset();
            return QObject::tr("Not a readable PDF");
        }
        return {};
#endif
    }

    int pageCount() const
    {
#ifdef PF_PDF_POPPLER
        return m_document ? m_document->numPages() : 0;
#else
        return m_document ? m_document->pageCount() : 0;
#endif
    }

    /// Page size in points (1/72 inch).
    QSizeF pageSize(int page) const
    {
#ifdef PF_PDF_POPPLER
        const std::unique_ptr<Poppler::Page> p(m_document->page(page));
        return p ? p->pageSizeF() : QSizeF();
#else
        return m_document->pagePointSize(page);
#endif
    }

    QImage render(int page, double scale) const
    {
#ifdef PF_PDF_POPPLER
        const std::unique_ptr<Poppler::Page> p(m_document->page(page));
        if (!p) {
            return {};
        }
        const double dpi = 72.0 * scale;
        return p->renderToImage(dpi, dpi);
#else
        const QSize size = (m_document->pagePointSize(page) * scale).toSize();
        return m_document->render(page, size);
#endif
    }

private:
#ifdef PF_PDF_POPPLER
    std::unique_ptr<Poppler::Document> m_document;
#else
    std::unique_ptr<QPdfDocument> m_document;
#endif
};

/// What a render job was asked for, captured on the GUI thread.
struct Request {
    quint64 generation = 0;
    QString path; ///< non-empty when the document must be (re)opened
    int page = 0;
    double zoom = 1.0; ///< relative to fit-to-width
    int viewportWidth = 0;
    qreal devicePixelRatio = 1.0;
};

struct Result {
    quint64 generation = 0;
    QImage image;
    int page = 0;
    int pageCount = 0;
    QString error;
};

class PdfPageRenderer : public ui::QuickLookRenderer
{
public:
    PdfPageRenderer()
    {
        // One thread: jobs run in order and never touch the document at once,
        // and a backlog of stale ones (holding `j` through a directory of
        // PDFs) drains in microseconds because each checks its generation
        // before doing anything.
        m_pool.setMaxThreadCount(1);
        m_pool.setExpiryTimeout(5000);
    }

    ~PdfPageRenderer() override
    {
        m_generation->fetch_add(1);
        QObject::disconnect(&m_watcher, nullptr, &m_context, nullptr);
        m_pool.waitForDone();
    }

    QString id() const override { return QStringLiteral("pdf-pages"); }

    bool canRender(const QMimeType &mime, const FileEntry &entry) const override
    {
        Q_UNUSED(mime)
        Q_UNUSED(entry)
        return true;
    }

    QWidget *createWidget(QWidget *parent) override
    {
        if (m_scroll != nullptr) {
            return m_scroll;
        }

        m_scroll = new QScrollArea(parent);
        m_scroll->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
        m_scroll->setFocusPolicy(Qt::NoFocus);
        m_scroll->setWidgetResizable(false);

        m_page = new QLabel;
        m_page->setAlignment(Qt::AlignCenter);
        m_scroll->setWidget(m_page);

        QObject::connect(&m_watcher, &QFutureWatcher<Result>::finished, &m_context,
                         [this] { onRendered(m_watcher.result()); });
        return m_scroll;
    }

    void setContent(QuickLookContent &&content) override
    {
        if (m_scroll == nullptr) {
            return;
        }
        m_path = content.path;
        m_pageIndex = 0;
        m_pageCount = 0;
        m_zoom = 1.0;
        m_error.clear();
        m_page->setPixmap(QPixmap());
        m_page->setText(QObject::tr("Rendering…"));
        m_page->adjustSize();
        render(/*reopen=*/true);
    }

    void clear() override
    {
        m_generation->fetch_add(1);
        m_path.clear();
        m_pageCount = 0;
        m_error.clear();
        if (m_page != nullptr) {
            m_page->clear();
        }
    }

    QString statusText() const override
    {
        if (!m_error.isEmpty()) {
            return m_error;
        }
        if (m_pageCount == 0) {
            return {};
        }
        return QObject::tr("Page %1 of %2 · %3% · [ ] page · + − 0 zoom")
            .arg(m_pageIndex + 1)
            .arg(m_pageCount)
            .arg(qRound(m_zoom * 100));
    }

    bool handleKey(QKeyEvent *event) override
    {
        if (m_pageCount == 0) {
            return false;
        }
        const Qt::KeyboardModifiers modifiers =
            event->modifiers() & ~(Qt::ShiftModifier | Qt::KeypadModifier);
        if (modifiers != Qt::NoModifier) {
            return false;
        }

        // §7.6: `[` / `]` previous and next page, `+` / `-` / `0` zoom in, out
        // and fit. `=` is `+` without shift on most layouts.
        switch (event->key()) {
        case Qt::Key_BracketLeft:
            return goToPage(m_pageIndex - 1);
        case Qt::Key_BracketRight:
            return goToPage(m_pageIndex + 1);
        case Qt::Key_Plus:
        case Qt::Key_Equal:
            return setZoom(m_zoom * kZoomStep);
        case Qt::Key_Minus:
            return setZoom(m_zoom / kZoomStep);
        case Qt::Key_0:
            return setZoom(1.0);
        default:
            return false;
        }
    }

private:
    static constexpr double kZoomStep = 1.25;
    static constexpr double kMinZoom = 0.25;
    static constexpr double kMaxZoom = 8.0;

    bool goToPage(int page)
    {
        if (page < 0 || page >= m_pageCount) {
            return true; // consumed: at the first or last page, nothing to do
        }
        m_pageIndex = page;
        render(/*reopen=*/false);
        return true;
    }

    bool setZoom(double zoom)
    {
        m_zoom = std::clamp(zoom, kMinZoom, kMaxZoom);
        render(/*reopen=*/false);
        return true;
    }

    void render(bool reopen)
    {
        Request request;
        request.generation = m_generation->fetch_add(1) + 1;
        request.path = reopen ? m_path : QString();
        request.page = m_pageIndex;
        request.zoom = m_zoom;
        // The scroll bar is allowed for up front, so a page fitted to the
        // width does not grow a horizontal one as soon as it is tall enough
        // to need a vertical one.
        request.viewportWidth =
            m_scroll->viewport()->width() - m_scroll->verticalScrollBar()->sizeHint().width();
        request.devicePixelRatio = m_scroll->devicePixelRatioF();

        auto generation = m_generation;
        auto document = m_document;
        m_watcher.setFuture(QtConcurrent::run(&m_pool, [request, generation, document] {
            return renderPage(request, *generation, *document);
        }));
    }

    static Result renderPage(const Request &request, const std::atomic<quint64> &current,
                             Document &document)
    {
        Result result;
        result.generation = request.generation;
        if (request.generation != current.load()) {
            return result; // superseded before it started
        }

        if (!request.path.isEmpty()) {
            result.error = document.open(request.path);
            if (!result.error.isEmpty()) {
                return result;
            }
        }

        result.pageCount = document.pageCount();
        if (result.pageCount == 0) {
            result.error = QObject::tr("This PDF has no pages");
            return result;
        }
        result.page = std::clamp(request.page, 0, result.pageCount - 1);

        // Fit to width is zoom 1.0; a pane too narrow to measure yet (the
        // first file, before layout) falls back to a readable default.
        const QSizeF points = document.pageSize(result.page);
        const int width = request.viewportWidth > 100 ? request.viewportWidth : 800;
        const double fit = points.width() > 0 ? width / points.width() : 1.0;
        const double scale = fit * request.zoom * request.devicePixelRatio;

        if (request.generation != current.load()) {
            return result;
        }
        result.image = document.render(result.page, scale);
        result.image.setDevicePixelRatio(request.devicePixelRatio);
        return result;
    }

    void onRendered(const Result &result)
    {
        if (result.generation != m_generation->load() || m_page == nullptr) {
            return;
        }
        m_error = result.error;
        m_pageCount = result.pageCount;
        m_pageIndex = result.page;

        if (!m_error.isEmpty() || result.image.isNull()) {
            m_page->setPixmap(QPixmap());
            m_page->setText(m_error.isEmpty() ? QObject::tr("Could not render this page")
                                              : m_error);
        } else {
            m_page->setPixmap(QPixmap::fromImage(result.image));
        }
        m_page->adjustSize();
        notifyStatusChanged();
    }

    QObject m_context;

    QScrollArea *m_scroll = nullptr;
    QLabel *m_page = nullptr;

    QThreadPool m_pool;
    QFutureWatcher<Result> m_watcher;
    std::shared_ptr<std::atomic<quint64>> m_generation = std::make_shared<std::atomic<quint64>>(0);
    std::shared_ptr<Document> m_document = std::make_shared<Document>();

    QString m_path;
    QString m_error;
    int m_pageIndex = 0;
    int m_pageCount = 0;
    double m_zoom = 1.0;
};

} // namespace

class PdfPlugin : public QObject, public RendererPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID PF_RENDERER_PLUGIN_IID)
    Q_INTERFACES(pf::plugins::RendererPlugin)

public:
#ifdef PF_PDF_POPPLER
    PdfPlugin()
    {
        // poppler reports every malformed file it meets through qDebug by
        // default. A damaged PDF is ordinary input to a previewer, and the
        // user already sees "Not a readable PDF"; the detail belongs under
        // --verbose, not on stderr unconditionally.
        Poppler::setDebugErrorFunction(
            [](const QString &message, const QVariant &) { qCDebug(pfPdf) << message; },
            QVariant());
    }
#endif

    std::unique_ptr<ui::QuickLookRenderer> createRenderer() override
    {
        return std::make_unique<PdfPageRenderer>();
    }
};

} // namespace pf::plugins

#include "PdfPlugin.moc"
