#include "ui/ThemePalette.h"

namespace pf::ui {
namespace {

ThemePalette &mutablePalette()
{
    // Function-local static: no namespace-scope object with a non-trivial
    // constructor, per §3.4.
    static ThemePalette palette;
    return palette;
}

} // namespace

const ThemePalette &currentPalette()
{
    return mutablePalette();
}

void setCurrentPalette(const ThemePalette &palette)
{
    mutablePalette() = palette;
}

} // namespace pf::ui
