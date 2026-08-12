#pragma once

#include <QString>

namespace pf::config {

struct Theme;

/// Compiles a theme into a Qt stylesheet (§9).
///
/// §3.4 requires the result to be applied to the application *before any widget
/// is constructed*: "Applying a stylesheet after widgets exist forces a full
/// restyle pass over the widget tree." So this is a pure function of a theme,
/// callable before there is anything to style.
///
/// §9 splits the theme in two on purpose. Widgets are styled from this string;
/// the delegate reads the Theme struct directly, because a delegate paints
/// manually and a stylesheet cannot reach it. Both come from the same source,
/// which is what stops them disagreeing.
QString buildStyleSheet(const Theme &theme);

} // namespace pf::config
