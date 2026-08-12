#pragma once

#include <QColor>
#include <QString>

namespace pf::ui {

/// The colours a delegate paints with (§9).
///
/// Delegates paint manually and cannot use QSS, so the theme is compiled into
/// both a stylesheet for the widgets and this struct for the painting code.
/// M3 loads it from theme.toml; until then these defaults are Catppuccin
/// Mocha, which is also the bundled default theme.
struct ThemePalette {
    QColor background{0x1e, 0x1e, 0x2e};
    QColor surface{0x31, 0x32, 0x44};
    QColor overlay{0x6c, 0x70, 0x86};
    QColor text{0xcd, 0xd6, 0xf4};
    QColor subtext{0xa6, 0xad, 0xc8};
    QColor accent{0x89, 0xb4, 0xfa};
    QColor selectionBackground{0x45, 0x47, 0x5a};
    QColor cursorBackground{0x58, 0x5b, 0x70};

    QColor directory{0x89, 0xb4, 0xfa};
    QColor executable{0xa6, 0xe3, 0xa1};
    QColor symlink{0x94, 0xe2, 0xd5};
    QColor broken{0xf3, 0x8b, 0xa8};
    QColor archive{0xfa, 0xb3, 0x87};
    QColor image{0xf9, 0xe2, 0xaf};

    QColor error{0xf3, 0x8b, 0xa8};
    QColor warning{0xfa, 0xb3, 0x87};
    QColor success{0xa6, 0xe3, 0xa1};

    QColor border{0x45, 0x47, 0x5a};
    QColor borderFocused{0x89, 0xb4, 0xfa};

    QString fontFamily;
    int fontSize = 10;

    /// §5.3: fixed per theme, so QListView::setUniformItemSizes(true) holds.
    int rowHeight = 24;
    int borderRadius = 6;
    int panelPadding = 8;
};

/// The palette in use. Replaced wholesale when the theme changes (M3).
const ThemePalette &currentPalette();
void setCurrentPalette(const ThemePalette &palette);

} // namespace pf::ui
