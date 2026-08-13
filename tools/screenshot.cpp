// Renders the real application to a PNG, for the website.
//
// Not a mock. This builds an actual MainWindow with actual FilePanels over an
// actual directory, applies an actual theme through the same StyleSheetBuilder
// the application uses, and asks Qt to paint it — so what comes out is what the
// program looks like, by construction rather than by resemblance.
//
// It runs under the offscreen platform, which is what lets it work on a CI
// runner and in a terminal session with no window server permissions.

#include "config/Config.h"
#include "config/StyleSheetBuilder.h"
#include "config/Theme.h"
#include "ui/FilePanel.h"
#include "ui/MainWindow.h"
#include "ui/PanelStrip.h"
#include "ui/Sidebar.h"
#include "ui/ThemePalette.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDir>
#include <QImage>
#include <QPainter>
#include <QTimer>

#include <cstdio>

namespace {

/// Waits for the scans to land. The panels scan on a worker thread, so a render
/// taken immediately would catch empty lists — which is exactly the sort of
/// "screenshot that isn't the program" this tool exists to avoid.
void settle(int milliseconds)
{
    QEventLoop loop;
    QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit);
    loop.exec();
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);

    QCommandLineParser parser;
    parser.setApplicationDescription("Renders Panefile to a PNG.");
    parser.addHelpOption();

    const QCommandLineOption themeOption({"t", "theme"}, "Theme name.", "name", "macos-light");
    const QCommandLineOption outOption({"o", "output"}, "Output PNG.", "path", "panefile.png");
    const QCommandLineOption widthOption("width", "Window width.", "px", "1180");
    const QCommandLineOption heightOption("height", "Window height.", "px", "700");
    const QCommandLineOption scaleOption("scale", "Device pixel ratio.", "n", "2");
    const QCommandLineOption pathsOption({"p", "paths"}, "Directories, comma separated.", "list",
                                         QDir::homePath());
    parser.addOptions(
        {themeOption, outOption, widthOption, heightOption, scaleOption, pathsOption});
    parser.process(app);

    // The theme, through the same path the application uses: a Theme compiled
    // into a stylesheet, plus the palette the delegate paints from.
    const pf::config::ThemeLoadResult theme =
        pf::config::loadThemeByName(parser.value(themeOption));
    for (const pf::config::ConfigIssue &issue : theme.issues) {
        std::fprintf(stderr, "theme: %s\n", qPrintable(issue.message));
    }
    pf::ui::setCurrentPalette(theme.theme);
    app.setStyleSheet(pf::config::buildStyleSheet(theme.theme));

    const qreal scale = parser.value(scaleOption).toDouble();
    const int width = parser.value(widthOption).toInt();
    const int height = parser.value(heightOption).toInt();

    pf::ui::MainWindow window;
    window.resize(width, height);
    window.show();

    const QStringList paths = parser.value(pathsOption).split(QLatin1Char(','), Qt::SkipEmptyParts);
    for (const QString &path : paths) {
        window.panelStrip()->addPanel(path);
    }
    window.sidebar()->populate();

    // Two settles: one for the scans, one for the layout that follows them.
    settle(900);
    window.panelStrip()->equalise();
    settle(300);

    // Focus the first panel, since §9 calls the focused-panel treatment the
    // single most important visual affordance and a screenshot without it shows
    // the application in a state it is never actually in.
    if (pf::ui::FilePanel *first = window.panelStrip()->panelAt(0); first != nullptr) {
        window.panelStrip()->setFocusedPanel(first);
    }
    // The offscreen platform leaves the pointer at the window's origin, which
    // sits on the first sidebar row — so every render came out with a spurious
    // hover highlight on "Home" that looked like a selection and was reported
    // as one. A Leave event to each widget clears WA_UnderMouse, which is what
    // moving a real pointer away would do.
    for (QWidget *widget : window.findChildren<QWidget *>()) {
        QEvent leave(QEvent::Leave);
        QApplication::sendEvent(widget, &leave);
    }
    settle(200);

    QImage image(QSize(width, height) * scale, QImage::Format_ARGB32);
    image.setDevicePixelRatio(scale);
    image.fill(theme.theme.background);

    window.render(&image);

    if (!image.save(parser.value(outOption), "PNG")) {
        std::fprintf(stderr, "could not write %s\n", qPrintable(parser.value(outOption)));
        return 1;
    }

    std::fprintf(stderr, "wrote %s (%dx%d at %gx)\n", qPrintable(parser.value(outOption)), width,
                 height, scale);
    return 0;
}
