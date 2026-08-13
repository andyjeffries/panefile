#include "config/StyleSheetBuilder.h"
#include "config/Theme.h"
#include "model/DirectoryModel.h"
#include "ui/FilePanel.h"
#include "ui/PanelView.h"
#include "ui/ThemePalette.h"

#include <QApplication>
#include <QDir>
#include <QImage>
#include <QPainter>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <cmath>

using namespace pf;
using namespace pf::ui;

namespace {

void touch(const QString &path)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    [[maybe_unused]] const bool opened = file.open(QIODevice::WriteOnly);
    Q_ASSERT(opened);
    file.write("x");
    file.close();
}

/// The WCAG contrast ratio, which is how a reader perceives a difference —
/// unlike lightness, which several palettes deliberately hold constant while
/// changing hue.
double contrast(const QColor &a, const QColor &b)
{
    const auto luminance = [](const QColor &colour) {
        const auto channel = [](double value) {
            value /= 255.0;
            return value <= 0.03928 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
        };
        return (0.2126 * channel(colour.red())) + (0.7152 * channel(colour.green())) +
               (0.0722 * channel(colour.blue()));
    };

    const double first = luminance(a);
    const double second = luminance(b);
    return (std::max(first, second) + 0.05) / (std::min(first, second) + 0.05);
}

} // namespace

/// §9's visual rules, checked by rendering rather than by reading the code.
///
/// A delegate is the one part of the application whose output no unit test
/// normally sees. Painting it into a QImage and reading the pixels back is the
/// only way to assert that the focused panel really is distinguishable, that
/// the banding really alternates, and that a spacing change really moved
/// something.
class TestAppearance : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void initTestCase()
    {
        m_dir = std::make_unique<QTemporaryDir>();
        for (int i = 0; i < 8; ++i) {
            touch(m_dir->filePath(QStringLiteral("file%1.txt").arg(i)));
        }

        // The generated stylesheet, exactly as Application applies it. Without
        // it the widgets paint Qt's default palette rather than the theme's,
        // and every assertion below would be measuring the wrong program.
        qApp->setStyleSheet(config::buildStyleSheet(currentPalette()));

        m_panel = std::make_unique<FilePanel>();
        m_panel->resize(420, 300);

        // Shown, not merely sized. Without a show() the layout never runs, the
        // view's viewport keeps its default size, and render() produces an
        // image of the wrong shape full of unpainted pixels — which every
        // assertion below would then be making about nothing.
        m_panel->show();
        QVERIFY(QTest::qWaitForWindowExposed(m_panel.get()));

        m_panel->navigateTo(m_dir->path());
        QTest::qWait(300);
    }

    void cleanupTestCase()
    {
        m_panel.reset();
        m_dir.reset();
    }

    /// §9: banding has to alternate, and it has to be subtle. Both are
    /// assertions about pixels, and neither is visible in the source.
    void rowsAreBanded()
    {
        const config::Theme theme = currentPalette();
        QVERIFY(theme.alternatingRows);

        const QImage rendered = renderView();

        // Rows 1 and 2, not 0 and 1: row 0 holds the cursor, which paints its
        // own background and would swamp the banding being measured.
        const QRect first = m_panel->view()->visualRect(m_panel->view()->model()->index(1, 0));
        const QRect second = m_panel->view()->visualRect(m_panel->view()->model()->index(2, 0));
        QVERIFY(first.isValid() && second.isValid());

        // The most common colour in each row, not a single sample. A row holds
        // an icon, a filename and two right-aligned columns, and any fixed
        // sample point is one layout change away from landing on one of them —
        // whereas the background is, by a wide margin, the colour most of the
        // row is made of.
        const QRgb odd = dominantColour(rendered, first);
        const QRgb even = dominantColour(rendered, second);

        QVERIFY2(even != odd, "consecutive rows must not paint the same background");

        // Subtle: banding that announces itself is a distraction. Well under a
        // tenth of the range in every channel.
        const int delta =
            std::max({std::abs(qRed(even) - qRed(odd)), std::abs(qGreen(even) - qGreen(odd)),
                      std::abs(qBlue(even) - qBlue(odd))});
        QVERIFY2(delta < 26, qPrintable(QStringLiteral("banding delta %1 is too loud").arg(delta)));
        QVERIFY2(delta > 1, qPrintable(QStringLiteral("banding delta %1 is invisible").arg(delta)));
    }

    /// The derived banding must move *away* from the background in both
    /// directions, which a hard-coded lighter() cannot do.
    void bandingDirectionFollowsTheTheme()
    {
        config::Theme dark;
        dark.background = QColor(0x1e, 0x1e, 0x2e);
        QVERIFY(!dark.isLight());
        QVERIFY(dark.effectiveAlternateRowBackground().lightness() > dark.background.lightness());

        config::Theme light;
        light.background = QColor(0xff, 0xff, 0xff);
        light.text = QColor(0x1d, 0x1d, 0x1f);
        QVERIFY(light.isLight());
        QVERIFY(light.effectiveAlternateRowBackground().lightness() < light.background.lightness());
    }

    /// Every bundled theme must keep its cursor distinguishable from the
    /// banding.
    ///
    /// This is the test the banding should have shipped with. Introducing it in
    /// M11 made the cursor — the row telling you where you are — indiscernible
    /// in one-dark, tokyo-night-storm and rose-pine-dawn, whose published
    /// palettes put the cursor within a few percent of their background. The
    /// derivation now weakens the band until the cursor clearly wins, and gives
    /// up on banding entirely when no strength is weak enough.
    void everyBundledThemeKeepsItsCursorVisible()
    {
        const QDir themes(QStringLiteral(PF_THEMES_DIR));
        const QStringList files = themes.entryList({QStringLiteral("*.toml")}, QDir::Files);
        QVERIFY2(files.size() >= 20, "the bundled themes should be here");

        for (const QString &file : files) {
            QFile source(themes.absoluteFilePath(file));
            QVERIFY(source.open(QIODevice::ReadOnly));

            const config::Theme theme =
                config::parseTheme(QString::fromUtf8(source.readAll()), file).theme;
            const QColor band = theme.effectiveAlternateRowBackground();

            if (band == theme.background) {
                // No banding at all, which is the documented outcome for a
                // theme whose cursor is too faint to compete with any.
                continue;
            }

            const double separation = contrast(theme.cursorBackground, band);
            QVERIFY2(separation >= 1.12,
                     qPrintable(QStringLiteral("%1: cursor is %2 against the banding")
                                    .arg(file)
                                    .arg(separation, 0, 'f', 2)));

            // And the banding must still be visible at all, or it is a colour
            // computation nobody sees.
            QVERIFY2(contrast(band, theme.background) > 1.01, qPrintable(file));
        }
    }

    /// A theme that names the colour keeps it.
    void anExplicitBandingColourWins()
    {
        config::Theme theme;
        theme.alternateRowBackground = QColor(0x12, 0x34, 0x56);
        QCOMPARE(theme.effectiveAlternateRowBackground(), QColor(0x12, 0x34, 0x56));
    }

    /// §9: "The focused panel must be unmistakable… This is the single most
    /// important visual affordance in the app."
    void theFocusedPanelLooksDifferent()
    {
        // The whole panel, not its viewport: §9 puts the affordance on the
        // panel's border and background, which the list view never draws.
        m_panel->setActive(false);
        const QImage inactive = renderPanel();

        m_panel->setActive(true);
        const QImage active = renderPanel();

        QVERIFY2(inactive != active, "the focused panel must be visibly different");

        // And specifically at the border, which is where §9 puts the
        // difference.
        int differingBorderPixels = 0;
        for (int y = 0; y < active.height(); ++y) {
            if (active.pixel(0, y) != inactive.pixel(0, y)) {
                ++differingBorderPixels;
            }
        }
        QVERIFY2(differingBorderPixels > active.height() / 2,
                 "the focused border must run down the panel's edge");
    }

    /// The row spacing scales with the theme rather than being fixed, so a
    /// denser theme actually produces a denser list.
    void spacingFollowsThePanelPadding()
    {
        const config::Theme original = currentPalette();

        config::Theme tight = original;
        tight.panelPadding = 4;
        setCurrentPalette(tight);
        m_panel->refreshTheme();
        const QImage tightImage = renderView();

        config::Theme airy = original;
        airy.panelPadding = 16;
        setCurrentPalette(airy);
        m_panel->refreshTheme();
        const QImage airyImage = renderView();

        setCurrentPalette(original);
        m_panel->refreshTheme();

        QVERIFY2(tightImage != airyImage, "panel_padding must change the row layout");
    }

private:
    /// The colour most of `row` is painted in.
    static QRgb dominantColour(const QImage &image, const QRect &row)
    {
        QHash<QRgb, int> counts;
        const QRect bounded = row.intersected(image.rect());

        for (int y = bounded.top(); y <= bounded.bottom(); ++y) {
            for (int x = bounded.left(); x <= bounded.right(); ++x) {
                ++counts[image.pixel(x, y)];
            }
        }

        QRgb best = 0;
        int bestCount = 0;
        for (auto it = counts.constBegin(); it != counts.constEnd(); ++it) {
            if (it.value() > bestCount) {
                bestCount = it.value();
                best = it.key();
            }
        }
        return best;
    }

    QImage renderView()
    {
        QImage image(m_panel->view()->viewport()->size(), QImage::Format_ARGB32);
        image.fill(currentPalette().background);
        m_panel->view()->viewport()->render(&image);
        return image;
    }

    QImage renderPanel()
    {
        QImage image(m_panel->size(), QImage::Format_ARGB32);
        image.fill(currentPalette().background);
        m_panel->render(&image);
        return image;
    }

    std::unique_ptr<QTemporaryDir> m_dir;
    std::unique_ptr<FilePanel> m_panel;
};

QTEST_MAIN(TestAppearance)
#include "tst_appearance.moc"
