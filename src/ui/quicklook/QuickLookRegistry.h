#pragma once

#include "ui/quicklook/QuickLookRenderer.h"

#include <memory>
#include <vector>

namespace pf::ui {

/// Chooses the renderer for a file (§7.6).
///
/// "QuickLookRegistry picks the highest-priority renderer whose canRender
/// returns true, falling back to HexRenderer."
///
/// §3.4 requires that no renderer is instantiated until Quick Look is first
/// opened — "This matters most for KSyntaxHighlighting: loading its syntax
/// definition repository costs tens of milliseconds and must never happen at
/// startup." The registry is therefore built on first use, and each renderer's
/// *widget* is built later still, on the first file it is asked to show.
class QuickLookRegistry
{
public:
    QuickLookRegistry();
    ~QuickLookRegistry();

    QuickLookRegistry(const QuickLookRegistry &) = delete;
    QuickLookRegistry &operator=(const QuickLookRegistry &) = delete;

    void add(std::unique_ptr<QuickLookRenderer> renderer);

    /// The best renderer for this file. Never null: the hex renderer accepts
    /// anything, which is what makes §7.6's "falling back to HexRenderer" work
    /// without a special case.
    QuickLookRenderer *rendererFor(const QMimeType &mime, const FileEntry &entry) const;

    /// The fallback, for a renderer that failed (§7.6: "A renderer that throws
    /// or fails must degrade to HexRenderer with an inline error note").
    QuickLookRenderer *fallback() const;

    QuickLookRenderer *byId(const QString &id) const;

    int count() const;

    /// Builds the registry with every renderer available in this build.
    /// Optional ones — syntax highlighting, media, PDF — are added only when
    /// their dependency was found, and their absence is a graceful degradation
    /// rather than a missing feature (§2).
    static std::unique_ptr<QuickLookRegistry> createDefault();

private:
    std::vector<std::unique_ptr<QuickLookRenderer>> m_renderers;
    QuickLookRenderer *m_fallback = nullptr;
};

} // namespace pf::ui
