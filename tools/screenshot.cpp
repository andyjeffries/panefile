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
#include <array>
#include <QLinearGradient>
#include <QPainterPath>
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

namespace {

/// Wraps a rendered window in a macOS frame: title bar, traffic lights, rounded
/// corners and a shadow, on a transparent ground.
///
/// The application draws its own contents and nothing else — on Linux and under
/// the offscreen plugin there is no compositor to draw a frame, so a screenshot
/// is a bare rectangle of list. That is honest but it does not look like an
/// application, and the website's job is to show what running it looks like.
///
/// Drawn rather than captured so it is identical on both platforms and in CI,
/// where there is no window server to capture from.
QImage withWindowChrome(const QImage &content, const pf::config::Theme &theme,
                        const QString &title, int scale)
{
    const bool light = theme.isLight();

    // Points, multiplied up at the end, so the geometry reads as the design
    // describes it rather than as device pixels.
    constexpr int kTitleBar = 52;
    constexpr int kRadius = 12;
    constexpr int kMargin = 60;
    constexpr int kShadowBlur = 56;
    constexpr int kShadowDrop = 16;

    const int contentWidth = content.width() / scale;
    const int contentHeight = content.height() / scale;
    const int windowWidth = contentWidth;
    const int windowHeight = contentHeight + kTitleBar;

    QImage canvas(QSize(windowWidth + (kMargin * 2), windowHeight + (kMargin * 2)) * scale,
                  QImage::Format_ARGB32_Premultiplied);
    canvas.setDevicePixelRatio(scale);
    canvas.fill(Qt::transparent);

    QPainter painter(&canvas);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QRectF windowRect(kMargin, kMargin, windowWidth, windowHeight);

    // The shadow, as a stack of increasingly transparent rounded rectangles.
    // A real Gaussian blur would need QGraphicsEffect and an offscreen surface;
    // this is a handful of fills and is indistinguishable at these sizes.
    for (int step = kShadowBlur; step > 0; --step) {
        const double t = static_cast<double>(step) / kShadowBlur;
        // Quadratic falloff and a low per-step alpha: a linear ramp at a
        // visible alpha stacks into concentric bands rather than a blur.
        QColor shadow(18, 18, 24);
        shadow.setAlphaF(0.010 * (1.0 - t) * (1.0 - t));
        painter.setPen(Qt::NoPen);
        painter.setBrush(shadow);
        painter.drawRoundedRect(windowRect.adjusted(-step, -step + kShadowDrop, step,
                                                    step + kShadowDrop),
                                kRadius + step, kRadius + step);
    }

    // Clip everything that follows to the window's rounded outline, so the
    // content's square corners are cut by it rather than poking through.
    QPainterPath outline;
    outline.addRoundedRect(windowRect, kRadius, kRadius);
    painter.setClipPath(outline);

    // Title bar: a vertical gradient, as macOS draws it.
    QLinearGradient bar(windowRect.topLeft(), QPointF(windowRect.left(), windowRect.top() + kTitleBar));
    if (light) {
        bar.setColorAt(0, QColor(0xf7, 0xf7, 0xf9));
        bar.setColorAt(1, QColor(0xec, 0xec, 0xef));
    } else {
        bar.setColorAt(0, QColor(0x2a, 0x2b, 0x33));
        bar.setColorAt(1, QColor(0x25, 0x26, 0x2d));
    }
    painter.fillRect(QRectF(windowRect.left(), windowRect.top(), windowWidth, kTitleBar), bar);

    painter.drawImage(QRectF(windowRect.left(), windowRect.top() + kTitleBar, contentWidth,
                             contentHeight),
                      content);

    // The separator under the title bar, and the window's own hairline border.
    painter.setPen(QPen(light ? QColor(0, 0, 0, 30) : QColor(0, 0, 0, 115), 1));
    painter.drawLine(QPointF(windowRect.left(), windowRect.top() + kTitleBar - 0.5),
                     QPointF(windowRect.right(), windowRect.top() + kTitleBar - 0.5));

    // Traffic lights: 12pt across, 8pt apart, 16pt from the edge.
    const std::array<QColor, 3> lights{QColor(0xff, 0x5f, 0x57), QColor(0xfe, 0xbc, 0x2e),
                                       QColor(0x28, 0xc8, 0x40)};
    painter.setPen(Qt::NoPen);
    for (std::size_t i = 0; i < lights.size(); ++i) {
        painter.setBrush(lights.at(i));
        painter.drawEllipse(
            QRectF(windowRect.left() + 16 + (static_cast<double>(i) * 20),
                   windowRect.top() + ((kTitleBar - 12) / 2.0), 12, 12));
    }

    // The title, centred, with nothing else on the bar — macOS titles are
    // centred and are a name rather than a path.
    QFont titleFont = QApplication::font();
    titleFont.setPointSize(13);
    titleFont.setWeight(QFont::DemiBold);
    painter.setFont(titleFont);
    painter.setPen(light ? QColor(0x1c, 0x1c, 0x1e) : QColor(0xf2, 0xf2, 0xf5));
    painter.drawText(QRectF(windowRect.left(), windowRect.top(), windowWidth, kTitleBar),
                     Qt::AlignCenter, title);

    painter.setClipping(false);
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(light ? QColor(0, 0, 0, 28) : QColor(255, 255, 255, 20), 1));
    painter.drawRoundedRect(windowRect.adjusted(0.5, 0.5, -0.5, -0.5), kRadius, kRadius);

    return canvas;
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
    const QCommandLineOption chromeOption("chrome",
                                          "Draw a macOS window frame and shadow around it.");
    const QCommandLineOption titleOption("title", "Title shown in the chrome.", "text",
                                         "Panefile");
    parser.addOptions({themeOption, outOption, widthOption, heightOption, scaleOption, pathsOption,
                       chromeOption, titleOption});
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

    if (parser.isSet(chromeOption)) {
        image = withWindowChrome(image, theme.theme, parser.value(titleOption), scale);
    }

    if (!image.save(parser.value(outOption), "PNG")) {
        std::fprintf(stderr, "could not write %s\n", qPrintable(parser.value(outOption)));
        return 1;
    }

    std::fprintf(stderr, "wrote %s (%dx%d at %gx)\n", qPrintable(parser.value(outOption)), width,
                 height, scale);
    return 0;
}
