#pragma once

#include "config/Theme.h"

namespace pf::ui {

/// The colours and metrics the delegate paints with (§9).
///
/// §9 compiles a theme into two things: a stylesheet for the widgets, and this
/// for the painting code, "because delegates paint manually and can't use QSS".
/// Both come from one config::Theme, which is what stops them disagreeing.
using ThemePalette = config::Theme;

/// The theme in use. Replaced wholesale on a theme change or a hot reload.
const ThemePalette &currentPalette();
void setCurrentPalette(const ThemePalette &palette);

} // namespace pf::ui
