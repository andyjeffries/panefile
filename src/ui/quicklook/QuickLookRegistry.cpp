#include "ui/quicklook/QuickLookRegistry.h"

#include "core/Logging.h"
#include "ui/quicklook/renderers/ArchiveRenderer.h"
#include "ui/quicklook/renderers/DirectoryRenderer.h"
#include "ui/quicklook/renderers/HexRenderer.h"
#include "ui/quicklook/renderers/ImageRenderer.h"
#include "ui/quicklook/renderers/MediaRenderer.h"
#include "ui/quicklook/renderers/PdfRenderer.h"
#include "ui/quicklook/renderers/TextRenderer.h"

#include <algorithm>

namespace pf::ui {

QuickLookRegistry::QuickLookRegistry() = default;
QuickLookRegistry::~QuickLookRegistry() = default;

void QuickLookRegistry::add(std::unique_ptr<QuickLookRenderer> renderer)
{
    if (!renderer) {
        return;
    }

    // The hex renderer accepts everything, so it is remembered separately and
    // never allowed to win the ordinary search — otherwise it would shadow any
    // renderer registered after it.
    if (renderer->id() == QLatin1String("hex")) {
        m_fallback = renderer.get();
    }
    m_renderers.push_back(std::move(renderer));
}

QuickLookRenderer *QuickLookRegistry::rendererFor(const QMimeType &mime,
                                                  const FileEntry &entry) const
{
    // The index rather than the pointer, so the returned renderer keeps the
    // mutability its callers need — setContent() and handleKey() are not const —
    // while the selection loop itself only ever asks const questions.
    std::size_t best = m_renderers.size();

    for (std::size_t i = 0; i < m_renderers.size(); ++i) {
        const QuickLookRenderer *candidate = m_renderers[i].get();
        if (candidate == m_fallback || !candidate->canRender(mime, entry)) {
            continue;
        }
        // §7.6: "higher wins ties". Strictly greater, so that among equal
        // priorities the first registered wins and the order in
        // createDefault() is meaningful.
        if (best == m_renderers.size() || candidate->priority() > m_renderers[best]->priority()) {
            best = i;
        }
    }

    return best < m_renderers.size() ? m_renderers[best].get() : m_fallback;
}

QuickLookRenderer *QuickLookRegistry::fallback() const
{
    return m_fallback;
}

QuickLookRenderer *QuickLookRegistry::byId(const QString &id) const
{
    const auto found = std::ranges::find_if(
        m_renderers, [&id](const auto &renderer) { return renderer->id() == id; });
    return found == m_renderers.end() ? nullptr : found->get();
}

int QuickLookRegistry::count() const
{
    return static_cast<int>(m_renderers.size());
}

std::unique_ptr<QuickLookRegistry> QuickLookRegistry::createDefault()
{
    auto registry = std::make_unique<QuickLookRegistry>();

    // Registered most specific first, which decides ties at equal priority.
    registry->add(std::make_unique<DirectoryRenderer>());
    registry->add(std::make_unique<ImageRenderer>());
    registry->add(std::make_unique<MediaRenderer>());
    registry->add(std::make_unique<PdfRenderer>());
    registry->add(std::make_unique<ArchiveRenderer>());
    registry->add(std::make_unique<TextRenderer>());
    registry->add(std::make_unique<HexRenderer>());

    qCDebug(pfUi) << "quick look registry:" << registry->count() << "renderers";
    return registry;
}

} // namespace pf::ui
