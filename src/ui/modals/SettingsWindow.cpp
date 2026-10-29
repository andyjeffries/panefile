#include "ui/modals/SettingsWindow.h"

#include "input/ActionRegistry.h"
#include "input/Chord.h"
#include "input/DefaultKeymap.h"
#include "input/Keymap.h"
#include "config/Config.h"
#include "config/Theme.h"
#include "config/TomlWriter.h"
#include "core/WorkerPools.h"
#include "platform/DefaultFileManager.h"
#include "platform/Paths.h"
#include "ui/SymbolicWidgets.h"
#include "ui/ThemePalette.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QFile>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QPointer>
#include <QPushButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QThreadPool>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <optional>

namespace pf::ui {
namespace {

/// The tab strip's buttons. Checkable, flat, and laid out as a glyph over a
/// label — which is what makes the row read as a macOS preferences toolbar
/// rather than as a row of push buttons.
QPushButton *makeTabButton(const QString &iconName, const QString &title)
{
    auto *button = new QPushButton;
    button->setObjectName(QStringLiteral("settingsTab"));
    button->setCheckable(true);
    button->setFlat(true);
    button->setCursor(Qt::PointingHandCursor);

    // A QPushButton does not size itself to a layout placed inside it — its
    // size hint comes from its (empty) text — so without a minimum the glyph
    // and the label overlap and both get clipped.
    button->setMinimumSize(88, 58);
    button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

    auto *layout = new QVBoxLayout(button);
    layout->setContentsMargins(10, 7, 10, 6);
    layout->setSpacing(1);

    // A symbolic glyph, in the accent when its tab is the open one. It was a
    // Unicode character, which rendered in whatever font happened to have it,
    // at whatever size and weight that font drew it.
    auto *icon = new SymbolicLabel(
        iconName, 20,
        [button] {
            return button->isChecked() ? currentPalette().accent : currentPalette().subtext;
        },
        button);
    icon->setObjectName(QStringLiteral("settingsTabGlyph"));
    layout->addWidget(icon, 0, Qt::AlignHCenter);

    auto *label = new QLabel(title);
    label->setObjectName(QStringLiteral("settingsTabLabel"));
    label->setAlignment(Qt::AlignCenter);
    layout->addWidget(label);

    return button;
}

/// A form row's label, so every tab's labels line up and read the same.
QWidget *formPage(QFormLayout **formOut)
{
    auto *page = new QWidget;
    auto *outer = new QVBoxLayout(page);
    outer->setContentsMargins(28, 24, 28, 24);
    outer->setSpacing(0);

    auto *form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    form->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
    form->setHorizontalSpacing(16);
    form->setVerticalSpacing(12);
    form->setFieldGrowthPolicy(QFormLayout::FieldsStayAtSizeHint);

    outer->addLayout(form);
    outer->addStretch(1);

    *formOut = form;
    return page;
}

/// A button that records a shortcut: click it, press the chord.
///
/// While recording it carries the `pfCapturesKeys` property, which tells the
/// application's key handling to leave its key presses alone — without that,
/// pressing Ctrl+K to record it would open the palette it is recording.
class KeyRecorder : public QPushButton
{
    Q_OBJECT

public:
    explicit KeyRecorder(QWidget *parent = nullptr) : QPushButton(parent)
    {
        setObjectName(QStringLiteral("settingsKeyRecorder"));
        setCheckable(false);
        connect(this, &QPushButton::clicked, this, [this] { startRecording(); });
    }

    bool isRecording() const { return m_recording; }

    void setShownBinding(const QString &text)
    {
        m_shown = text;
        if (!m_recording) {
            setText(m_shown.isEmpty() ? tr("Not set") : m_shown);
        }
    }

Q_SIGNALS:
    void chordRecorded(const pf::input::Chord &chord);
    /// A bare key with no modifier was pressed; recording continues.
    void rejected(const QString &reason);

protected:
    void keyPressEvent(QKeyEvent *event) override
    {
        if (!m_recording) {
            QPushButton::keyPressEvent(event);
            return;
        }
        if (event->key() == Qt::Key_Escape) {
            stopRecording();
            event->accept();
            return;
        }
        const auto chord =
            input::chordFromKeyEvent(event->key(), event->modifiers(), event->text());
        event->accept();
        if (!chord) {
            return; // a modifier on its own
        }
        // A bare printable key would hijack typing everywhere. Named keys such
        // as F-keys are fine alone.
        if (chord->isTextForm() || (chord->modifiers == Qt::NoModifier &&
                                    !event->text().isEmpty() && event->text().front().isPrint())) {
            Q_EMIT rejected(tr("Use a modifier, e.g. Ctrl"));
            return;
        }
        stopRecording();
        Q_EMIT chordRecorded(*chord);
    }

    void focusOutEvent(QFocusEvent *event) override
    {
        stopRecording();
        QPushButton::focusOutEvent(event);
    }

private:
    void startRecording()
    {
        m_recording = true;
        setProperty("pfCapturesKeys", true);
        setText(tr("Press a shortcut…"));
        setFocus();
    }

    void stopRecording()
    {
        if (!m_recording) {
            return;
        }
        m_recording = false;
        setProperty("pfCapturesKeys", false);
        setText(m_shown.isEmpty() ? tr("Not set") : m_shown);
    }

    bool m_recording = false;
    QString m_shown;
};

/// A chord as hotkeys.toml spells it: portable, so it parses on every
/// platform (Ctrl is ⌘ on macOS).
QString portableChord(const input::Chord &chord)
{
    if (chord.isTextForm()) {
        return chord.text;
    }
    return QKeySequence(QKeyCombination(chord.modifiers, static_cast<Qt::Key>(chord.key)))
        .toString(QKeySequence::PortableText);
}

QString portableBinding(const input::Binding &binding)
{
    QStringList parts;
    for (const input::Chord &chord : binding) {
        parts << portableChord(chord);
    }
    return parts.join(QLatin1Char(' '));
}

constexpr auto kPaletteAction = "open_panefile_prompt";

struct LayerTable {
    input::KeymapLayer layer;
    const char *table;
};
/// The layers a global shortcut can collide in, and their hotkeys.toml tables.
constexpr LayerTable kConflictLayers[] = {
    {.layer = input::KeymapLayer::Global, .table = "global"},
    {.layer = input::KeymapLayer::Normal, .table = "normal"},
    {.layer = input::KeymapLayer::Selection, .table = "selection"},
};

} // namespace

/// Every control, in one place, so loadValues and the writers can reach them
/// without a web of members on the class itself.
struct SettingsWindow::Controls {
    // Appearance
    QListWidget *themes = nullptr;
    QCheckBox *followSystem = nullptr;
    QSpinBox *fontSize = nullptr;
    QSpinBox *rowHeight = nullptr;

    // General
    QLineEdit *newPanelPath = nullptr;
    QCheckBox *restoreSession = nullptr;
    QCheckBox *confirmOnQuit = nullptr;
    QCheckBox *singleInstance = nullptr;
    QSpinBox *defaultCount = nullptr;
    QSpinBox *maxCount = nullptr;
    QCheckBox *directoriesFirst = nullptr;
    QComboBox *defaultSort = nullptr;
    QCheckBox *showHidden = nullptr;

    // Default file manager — Linux only, and absent rather than disabled on
    // macOS, where there is nothing to choose.
    QCheckBox *offerDefaultFileManager = nullptr;
    QLabel *defaultFileManagerStatus = nullptr;
    QPushButton *makeDefaultFileManager = nullptr;

    // Quick Look
    QComboBox *dock = nullptr;
    QSpinBox *floatSize = nullptr;
    QCheckBox *followCursor = nullptr;
    QCheckBox *closeOnPanelSwitch = nullptr;

    // Keys
    QTreeWidget *keys = nullptr;
    QLineEdit *keyFilter = nullptr;
    KeyRecorder *paletteRecorder = nullptr;
    QLabel *paletteProblem = nullptr;
    QWidget *conflictBar = nullptr;
    QLabel *conflictText = nullptr;

    /// A recorded chord waiting for "Replace" because another action has it.
    std::optional<input::Chord> pendingChord;
};

SettingsWindow::SettingsWindow(input::ActionRegistry *registry, input::Keymap *keymap,
                               QWidget *parent)
    : Modal(parent), m_registry(registry), m_keymap(keymap), m_toolbar(buildToolbar()),
      m_tabs(new QButtonGroup(this)), m_pages(new QStackedWidget),
      m_controls(std::make_unique<Controls>())
{
    setSizePercent(72, 78);

    // Inset by the card's 1px border, so the toolbar and the pages sit inside
    // it rather than over it; they round their own outer corners to match.
    auto *layout = new QVBoxLayout(contentWidget());
    layout->setContentsMargins(1, 1, 1, 1);
    layout->setSpacing(0);

    m_tabs->setExclusive(true);
    layout->addWidget(m_toolbar);

    m_pages->setObjectName(QStringLiteral("settingsPages"));
    layout->addWidget(m_pages, 1);

    addTab(tr("Appearance"), QStringLiteral("swatch"), buildAppearanceTab());
    addTab(tr("General"), QStringLiteral("cog-6-tooth"), buildGeneralTab());
    addTab(tr("Quick Look"), QStringLiteral("eye"), buildQuickLookTab());
    addTab(tr("Keys"), QStringLiteral("command-line"), buildKeysTab());

    connect(m_tabs, &QButtonGroup::idClicked, m_pages, &QStackedWidget::setCurrentIndex);

    if (auto *first = m_tabs->button(0); first != nullptr) {
        first->setChecked(true);
    }
}

SettingsWindow::~SettingsWindow() = default;

QWidget *SettingsWindow::buildToolbar()
{
    auto *bar = new QWidget;
    bar->setObjectName(QStringLiteral("settingsToolbar"));
    bar->setAttribute(Qt::WA_StyledBackground, true);

    auto *layout = new QHBoxLayout(bar);
    layout->setContentsMargins(12, 10, 12, 10);
    layout->setSpacing(4);
    layout->addStretch(1);
    layout->addStretch(1);

    return bar;
}

void SettingsWindow::addTab(const QString &title, const QString &iconName, QWidget *page)
{
    auto *button = makeTabButton(iconName, title);
    const int index = m_pages->addWidget(page);
    m_tabs->addButton(button, index);

    // Between the two stretches the toolbar was built with, so the row stays
    // centred however many tabs there are.
    auto *layout = qobject_cast<QHBoxLayout *>(m_toolbar->layout());
    layout->insertWidget(layout->count() - 1, button);
}

QWidget *SettingsWindow::buildAppearanceTab()
{
    auto *page = new QWidget;
    auto *outer = new QVBoxLayout(page);
    outer->setContentsMargins(28, 20, 28, 20);
    outer->setSpacing(14);

    auto *heading = new QLabel(tr("Theme"));
    heading->setObjectName(QStringLiteral("settingsHeading"));
    outer->addWidget(heading);

    m_controls->themes = new QListWidget;
    m_controls->themes->setObjectName(QStringLiteral("settingsThemeList"));
    // The point of the list is that picking one shows you it, so a single click
    // is the whole interaction.
    connect(m_controls->themes, &QListWidget::currentItemChanged, this,
            [this](QListWidgetItem *current) {
                if (m_loading || current == nullptr) {
                    return;
                }
                const QString name = current->data(Qt::UserRole).toString();
                Q_EMIT themePreviewRequested(name);
                writeTheme(QString(), QStringLiteral("name"), config::TomlWriter::quote(name),
                           tr("Theme: %1").arg(current->text()));

                // Choosing a theme is choosing, so stop following the desktop.
                if (m_controls->followSystem->isChecked()) {
                    m_controls->followSystem->setChecked(false);
                }
            });
    outer->addWidget(m_controls->themes, 1);

    m_controls->followSystem = new QCheckBox(tr("Follow the desktop's light and dark setting"));
    connect(m_controls->followSystem, &QCheckBox::toggled, this, [this](bool on) {
        if (m_loading) {
            return;
        }
        writeTheme(QString(), QStringLiteral("follow_system"), config::TomlWriter::boolean(on),
                   on ? tr("Following the desktop appearance") : tr("Using the chosen theme"));
        if (on) {
            Q_EMIT themePreviewRequested(QString());
        }
    });
    outer->addWidget(m_controls->followSystem);

    auto *form = new QFormLayout;
    // Numbers want a field the width of a number, not of the window.
    form->setFieldGrowthPolicy(QFormLayout::FieldsStayAtSizeHint);
    form->setHorizontalSpacing(16);
    form->setVerticalSpacing(12);

    m_controls->fontSize = new QSpinBox;
    m_controls->fontSize->setRange(6, 32);
    m_controls->fontSize->setSuffix(tr(" px"));
    connect(m_controls->fontSize, &QSpinBox::valueChanged, this, [this](int value) {
        if (!m_loading) {
            writeTheme(QStringLiteral("ui"), QStringLiteral("font_size"),
                       config::TomlWriter::number(value), tr("Font size: %1 px").arg(value));
        }
    });
    form->addRow(tr("Font size"), m_controls->fontSize);

    m_controls->rowHeight = new QSpinBox;
    m_controls->rowHeight->setRange(14, 64);
    m_controls->rowHeight->setSuffix(tr(" px"));
    connect(m_controls->rowHeight, &QSpinBox::valueChanged, this, [this](int value) {
        if (!m_loading) {
            writeTheme(QStringLiteral("ui"), QStringLiteral("row_height"),
                       config::TomlWriter::number(value), tr("Row height: %1 px").arg(value));
        }
    });
    form->addRow(tr("Row height"), m_controls->rowHeight);

    outer->addLayout(form);
    return page;
}

QWidget *SettingsWindow::buildGeneralTab()
{
    QFormLayout *form = nullptr;
    QWidget *page = formPage(&form);

    m_controls->newPanelPath = new QLineEdit;
    m_controls->newPanelPath->setMinimumWidth(260);
    connect(m_controls->newPanelPath, &QLineEdit::editingFinished, this, [this] {
        if (!m_loading) {
            writeConfig(QStringLiteral("general"), QStringLiteral("new_panel_path"),
                        config::TomlWriter::quote(m_controls->newPanelPath->text()),
                        tr("New panels open at %1").arg(m_controls->newPanelPath->text()));
        }
    });
    form->addRow(tr("New panel path"), m_controls->newPanelPath);

    const auto boolRow = [this, form](QCheckBox **control, const QString &label,
                                      const QString &table, const QString &key) {
        *control = new QCheckBox;
        // Named after the key it writes, so a test — or an accessibility tool —
        // can find it by the thing it actually controls.
        (*control)->setObjectName(QStringLiteral("setting_%1_%2").arg(table, key));
        (*control)->setAccessibleName(label);
        connect(*control, &QCheckBox::toggled, this, [this, table, key, label](bool on) {
            if (!m_loading) {
                writeConfig(table, key, config::TomlWriter::boolean(on), label);
            }
        });
        form->addRow(label, *control);
    };

    boolRow(&m_controls->restoreSession, tr("Restore the last session"), QStringLiteral("general"),
            QStringLiteral("restore_session"));
    boolRow(&m_controls->confirmOnQuit, tr("Confirm on quit"), QStringLiteral("general"),
            QStringLiteral("confirm_on_quit"));
    boolRow(&m_controls->singleInstance, tr("Single instance"), QStringLiteral("general"),
            QStringLiteral("single_instance"));

    m_controls->defaultCount = new QSpinBox;
    m_controls->defaultCount->setRange(1, 10);
    connect(m_controls->defaultCount, &QSpinBox::valueChanged, this, [this](int value) {
        if (!m_loading) {
            writeConfig(QStringLiteral("panels"), QStringLiteral("default_count"),
                        config::TomlWriter::number(value), tr("Panels at startup: %1").arg(value));
        }
    });
    form->addRow(tr("Panels at startup"), m_controls->defaultCount);

    m_controls->maxCount = new QSpinBox;
    m_controls->maxCount->setRange(1, 10);
    connect(m_controls->maxCount, &QSpinBox::valueChanged, this, [this](int value) {
        if (!m_loading) {
            writeConfig(QStringLiteral("panels"), QStringLiteral("max_count"),
                        config::TomlWriter::number(value), tr("Maximum panels: %1").arg(value));
        }
    });
    form->addRow(tr("Maximum panels"), m_controls->maxCount);

    boolRow(&m_controls->directoriesFirst, tr("Directories first"), QStringLiteral("panels"),
            QStringLiteral("directories_first"));

    m_controls->defaultSort = new QComboBox;
    for (const auto &pair :
         {std::pair{"name", tr("Name")}, std::pair{"size", tr("Size")},
          std::pair{"modified", tr("Date modified")}, std::pair{"type", tr("Kind")}}) {
        m_controls->defaultSort->addItem(pair.second, QString::fromLatin1(pair.first));
    }
    connect(m_controls->defaultSort, &QComboBox::currentIndexChanged, this, [this](int) {
        if (!m_loading) {
            const QString value = m_controls->defaultSort->currentData().toString();
            writeConfig(QStringLiteral("panels"), QStringLiteral("default_sort"),
                        config::TomlWriter::quote(value),
                        tr("Sort by %1").arg(m_controls->defaultSort->currentText()));
        }
    });
    form->addRow(tr("Sort by"), m_controls->defaultSort);

    boolRow(&m_controls->showHidden, tr("Show hidden files"), QStringLiteral("panels"),
            QStringLiteral("show_hidden"));

    if (platform::defaultFileManagerSupported()) {
        boolRow(&m_controls->offerDefaultFileManager,
                tr("Offer to become the default file manager"), QStringLiteral("general"),
                QStringLiteral("offer_default_file_manager"));

        m_controls->defaultFileManagerStatus = new QLabel;
        m_controls->defaultFileManagerStatus->setObjectName(
            QStringLiteral("settingsDefaultFileManagerStatus"));
        m_controls->defaultFileManagerStatus->setTextFormat(Qt::PlainText);
        m_controls->defaultFileManagerStatus->setTextInteractionFlags(Qt::TextSelectableByMouse);

        m_controls->makeDefaultFileManager = new QPushButton(tr("Make default"));
        m_controls->makeDefaultFileManager->setObjectName(
            QStringLiteral("settingsMakeDefaultFileManager"));
        connect(m_controls->makeDefaultFileManager, &QPushButton::clicked, this, [this] {
            m_controls->makeDefaultFileManager->setEnabled(false);
            m_controls->defaultFileManagerStatus->setText(
                tr("Making Panefile your default file manager…"));
            Q_EMIT makeDefaultFileManagerRequested();
        });

        auto *row = new QWidget;
        auto *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->setSpacing(12);
        rowLayout->addWidget(m_controls->defaultFileManagerStatus, 1);
        rowLayout->addWidget(m_controls->makeDefaultFileManager);
        form->addRow(tr("Default file manager"), row);
    }

    return page;
}

void SettingsWindow::refreshDefaultFileManagerStatus()
{
    if (m_controls->defaultFileManagerStatus == nullptr) {
        return;
    }

    // Finding the default reads a dozen files, which is not work for the
    // thread that is painting the window being opened.
    const QPointer<SettingsWindow> self(this);
    WorkerPools::acquire("pf-default-fm", 1)->start([self] {
        const platform::DefaultFileManagerStatus status = platform::queryDefaultFileManager();
        QMetaObject::invokeMethod(
            self,
            [self, status] {
                if (self.isNull()) {
                    return;
                }
                QString text = status.handlerDescription();
                if (status.handlerId == QLatin1String(platform::kPanefileDesktopId) &&
                    !status.isDefault()) {
                    text += tr(" — “Show in folder” is not set up");
                } else if (!status.panefileInstalled) {
                    text += tr(" — panefile.desktop is not installed");
                }
                self->m_controls->defaultFileManagerStatus->setText(text);
                self->m_controls->makeDefaultFileManager->setEnabled(!status.isDefault() &&
                                                                     status.panefileInstalled);
            },
            Qt::QueuedConnection);
    });
}

QWidget *SettingsWindow::buildQuickLookTab()
{
    QFormLayout *form = nullptr;
    QWidget *page = formPage(&form);

    m_controls->dock = new QComboBox;
    for (const auto &pair :
         {std::pair{"float", tr("Floating")}, std::pair{"right", tr("Right")},
          std::pair{"left", tr("Left")}, std::pair{"bottom", tr("Bottom")},
          std::pair{"panel", tr("As a panel")}, std::pair{"full", tr("Full window")}}) {
        m_controls->dock->addItem(pair.second, QString::fromLatin1(pair.first));
    }
    connect(m_controls->dock, &QComboBox::currentIndexChanged, this, [this](int) {
        if (!m_loading) {
            const QString value = m_controls->dock->currentData().toString();
            writeConfig(QStringLiteral("quicklook"), QStringLiteral("dock"),
                        config::TomlWriter::quote(value),
                        tr("Quick Look: %1").arg(m_controls->dock->currentText()));
        }
    });
    form->addRow(tr("Position"), m_controls->dock);

    m_controls->floatSize = new QSpinBox;
    m_controls->floatSize->setRange(20, 100);
    m_controls->floatSize->setSuffix(tr(" %"));
    connect(m_controls->floatSize, &QSpinBox::valueChanged, this, [this](int value) {
        if (!m_loading) {
            writeConfig(QStringLiteral("quicklook"), QStringLiteral("float_size_percent"),
                        config::TomlWriter::number(value), tr("Quick Look size: %1%").arg(value));
        }
    });
    form->addRow(tr("Floating size"), m_controls->floatSize);

    const auto boolRow = [this, form](QCheckBox **control, const QString &label,
                                      const QString &key) {
        *control = new QCheckBox;
        (*control)->setObjectName(QStringLiteral("setting_quicklook_%1").arg(key));
        (*control)->setAccessibleName(label);
        connect(*control, &QCheckBox::toggled, this, [this, key, label](bool on) {
            if (!m_loading) {
                writeConfig(QStringLiteral("quicklook"), key, config::TomlWriter::boolean(on),
                            label);
            }
        });
        form->addRow(label, *control);
    };

    boolRow(&m_controls->followCursor, tr("Follow the cursor"), QStringLiteral("follow_cursor"));
    boolRow(&m_controls->closeOnPanelSwitch, tr("Close when switching panels"),
            QStringLiteral("close_on_panel_switch"));

    return page;
}

QWidget *SettingsWindow::buildKeysTab()
{
    auto *page = new QWidget;
    auto *outer = new QVBoxLayout(page);
    outer->setContentsMargins(28, 20, 28, 20);
    outer->setSpacing(12);

    // The one binding that can be changed here: the command palette's.
    auto *paletteRow = new QHBoxLayout;
    auto *paletteLabels = new QVBoxLayout;
    paletteLabels->setSpacing(2);
    auto *paletteTitle = new QLabel(tr("Command palette"));
    paletteTitle->setObjectName(QStringLiteral("settingsKeyPaletteTitle"));
    auto *paletteHint = new QLabel(tr("Opens from anywhere except a text field, where Ctrl+K keeps "
                                      "deleting to the end of the line."));
    paletteHint->setObjectName(QStringLiteral("settingsNote"));
    paletteHint->setWordWrap(true);
    paletteLabels->addWidget(paletteTitle);
    paletteLabels->addWidget(paletteHint);
    paletteRow->addLayout(paletteLabels, 1);

    m_controls->paletteRecorder = new KeyRecorder;
    m_controls->paletteRecorder->setMinimumWidth(170);
    paletteRow->addWidget(m_controls->paletteRecorder);
    auto *reset = new QPushButton(tr("Reset"));
    reset->setObjectName(QStringLiteral("settingsKeyReset"));
    reset->setFlat(true);
    paletteRow->addWidget(reset);
    outer->addLayout(paletteRow);

    m_controls->paletteProblem = new QLabel;
    m_controls->paletteProblem->setObjectName(QStringLiteral("modalProblem"));
    m_controls->paletteProblem->hide();
    outer->addWidget(m_controls->paletteProblem);

    m_controls->conflictBar = new QWidget;
    m_controls->conflictBar->setObjectName(QStringLiteral("settingsKeyConflict"));
    auto *conflictLayout = new QHBoxLayout(m_controls->conflictBar);
    conflictLayout->setContentsMargins(0, 0, 0, 0);
    m_controls->conflictText = new QLabel;
    m_controls->conflictText->setObjectName(QStringLiteral("settingsKeyConflictText"));
    m_controls->conflictText->setTextFormat(Qt::PlainText);
    m_controls->conflictText->setWordWrap(true);
    conflictLayout->addWidget(m_controls->conflictText, 1);
    auto *cancel = new QPushButton(tr("Cancel"));
    cancel->setObjectName(QStringLiteral("settingsKeyConflictCancel"));
    auto *replace = new QPushButton(tr("Replace"));
    replace->setObjectName(QStringLiteral("settingsKeyConflictReplace"));
    conflictLayout->addWidget(cancel);
    conflictLayout->addWidget(replace);
    m_controls->conflictBar->hide();
    outer->addWidget(m_controls->conflictBar);

    connect(m_controls->paletteRecorder, &KeyRecorder::chordRecorded, this,
            [this](const input::Chord &chord) { recordPaletteShortcut(chord); });
    connect(m_controls->paletteRecorder, &KeyRecorder::rejected, this,
            [this](const QString &reason) {
                m_controls->paletteProblem->setText(reason);
                m_controls->paletteProblem->show();
            });
    connect(replace, &QPushButton::clicked, this, [this] {
        if (m_controls->pendingChord) {
            savePaletteShortcut(*m_controls->pendingChord, true);
        }
    });
    connect(cancel, &QPushButton::clicked, this, [this] {
        m_controls->pendingChord.reset();
        m_controls->conflictBar->hide();
    });
    connect(reset, &QPushButton::clicked, this, [this] { resetPaletteShortcut(); });

    m_controls->keyFilter = new QLineEdit;
    m_controls->keyFilter->setPlaceholderText(tr("Search actions"));
    m_controls->keyFilter->setClearButtonEnabled(true);
    outer->addWidget(m_controls->keyFilter);

    m_controls->keys = new QTreeWidget;
    m_controls->keys->setObjectName(QStringLiteral("settingsKeyTable"));
    m_controls->keys->setColumnCount(3);
    m_controls->keys->setHeaderLabels({tr("Action"), tr("Keys"), tr("Description")});
    m_controls->keys->setRootIsDecorated(false);
    m_controls->keys->setUniformRowHeights(true);
    m_controls->keys->setAlternatingRowColors(false);
    m_controls->keys->header()->setSectionResizeMode(2, QHeaderView::Stretch);
    outer->addWidget(m_controls->keys, 1);

    connect(m_controls->keyFilter, &QLineEdit::textChanged, this, [this](const QString &text) {
        for (int i = 0; i < m_controls->keys->topLevelItemCount(); ++i) {
            QTreeWidgetItem *item = m_controls->keys->topLevelItem(i);
            const bool matches = text.isEmpty() ||
                                 item->text(0).contains(text, Qt::CaseInsensitive) ||
                                 item->text(1).contains(text, Qt::CaseInsensitive) ||
                                 item->text(2).contains(text, Qt::CaseInsensitive);
            item->setHidden(!matches);
        }
    });

    auto *note = new QLabel(tr("Edit hotkeys.toml to change the others."));
    note->setObjectName(QStringLiteral("settingsNote"));
    outer->addWidget(note);

    return page;
}

QString SettingsWindow::configPath()
{
    return platform::configDir() + QStringLiteral("/config.toml");
}

QString SettingsWindow::themePath()
{
    return platform::configDir() + QStringLiteral("/theme.toml");
}

void SettingsWindow::present()
{
    loadValues();
    refreshDefaultFileManagerStatus();
    showModal();
}

void SettingsWindow::loadValues()
{
    // Every control writes on change. Without this, showing the window would
    // write every key in the file back with the value it already had — which
    // would also mean a fresh install gained a fully-populated config just for
    // having looked at it.
    m_loading = true;

    const config::Settings settings = config::loadConfig(configPath()).settings;

    m_controls->newPanelPath->setText(settings.general.newPanelPath);
    m_controls->restoreSession->setChecked(settings.general.restoreSession);
    m_controls->confirmOnQuit->setChecked(settings.general.confirmOnQuit);
    m_controls->singleInstance->setChecked(settings.general.singleInstance);
    m_controls->defaultCount->setValue(settings.panels.defaultCount);
    m_controls->maxCount->setValue(settings.panels.maxCount);
    m_controls->directoriesFirst->setChecked(settings.panels.directoriesFirst);
    m_controls->showHidden->setChecked(settings.panels.showHidden);
    if (m_controls->offerDefaultFileManager != nullptr) {
        m_controls->offerDefaultFileManager->setChecked(settings.general.offerDefaultFileManager);
    }

    if (const int index = m_controls->defaultSort->findData(settings.panels.defaultSort);
        index >= 0) {
        m_controls->defaultSort->setCurrentIndex(index);
    }
    if (const int index = m_controls->dock->findData(settings.quicklook.dock); index >= 0) {
        m_controls->dock->setCurrentIndex(index);
    }
    m_controls->floatSize->setValue(settings.quicklook.floatSizePercent);
    m_controls->followCursor->setChecked(settings.quicklook.followCursor);
    m_controls->closeOnPanelSwitch->setChecked(settings.quicklook.closeOnPanelSwitch);

    // Themes.
    const config::ThemeLoadResult active = config::loadActiveTheme(themePath());
    m_controls->themes->clear();
    for (const QString &name : config::availableThemeNames()) {
        const config::ThemeLoadResult loaded = config::loadThemeByName(name);
        auto *item = new QListWidgetItem(loaded.theme.name.isEmpty() ? name : loaded.theme.name);
        item->setData(Qt::UserRole, name);

        // A swatch, so the list can be read rather than guessed at. Three
        // colours is enough to tell Nord from Gruvbox at a glance.
        QPixmap swatch(48, 18);
        swatch.fill(loaded.theme.background);
        {
            QPainter painter(&swatch);
            painter.fillRect(QRect(0, 0, 16, 18), loaded.theme.surface);
            painter.fillRect(QRect(32, 4, 12, 10), loaded.theme.accent);
        }
        item->setIcon(QIcon(swatch));

        m_controls->themes->addItem(item);
        if (loaded.theme.name == active.theme.name) {
            m_controls->themes->setCurrentItem(item);
        }
    }

    m_controls->fontSize->setValue(active.theme.fontSize);
    m_controls->rowHeight->setValue(active.theme.rowHeight);

    // follow_system is true by default when there is no theme.toml at all,
    // which is exactly the state a fresh install is in.
    bool followSystem = !QFile::exists(themePath());
    if (!followSystem) {
        QFile file(themePath());
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            const QString text = QString::fromUtf8(file.readAll());
            followSystem = text.contains(QStringLiteral("follow_system")) &&
                           text.contains(QStringLiteral("follow_system = true"));
        }
    }
    m_controls->followSystem->setChecked(followSystem);

    // Keys.
    m_controls->keys->clear();
    for (const QString &id : m_registry->ids()) {
        QStringList bindings;
        for (const input::KeymapLayer layer :
             {input::KeymapLayer::Global, input::KeymapLayer::Normal, input::KeymapLayer::Selection,
              input::KeymapLayer::Modal, input::KeymapLayer::Typing}) {
            for (const input::Binding &binding : m_keymap->bindingsFor(layer, id)) {
                const QString text = input::bindingToString(binding);
                if (!bindings.contains(text)) {
                    bindings << text;
                }
            }
        }

        auto *item = new QTreeWidgetItem(m_controls->keys);
        item->setText(0, id);
        item->setText(1, bindings.join(QStringLiteral("   ")));
        const input::Action *action = m_registry->find(id);
        item->setText(2, action != nullptr ? action->description : QString());
    }
    m_controls->keys->sortItems(0, Qt::AscendingOrder);
    m_controls->paletteRecorder->setShownBinding(paletteBindingText(*m_keymap));
    m_controls->paletteProblem->hide();
    m_controls->conflictBar->hide();
    m_controls->pendingChord.reset();
    m_controls->keys->resizeColumnToContents(0);
    m_controls->keys->resizeColumnToContents(1);

    m_loading = false;
}

QString SettingsWindow::hotkeysPath()
{
    return platform::configDir() + QStringLiteral("/hotkeys.toml");
}

QString SettingsWindow::paletteBindingText(const input::Keymap &keymap)
{
    QStringList texts;
    for (const input::Binding &binding :
         keymap.bindingsFor(input::KeymapLayer::Global, QLatin1String(kPaletteAction))) {
        texts << input::bindingToString(binding);
    }
    return texts.join(QStringLiteral("   "));
}

void SettingsWindow::recordPaletteShortcut(const input::Chord &chord)
{
    m_controls->paletteProblem->hide();
    m_controls->conflictBar->hide();
    m_controls->pendingChord.reset();

    // Another action on the same chord, in a layer that is live alongside the
    // global one. Binding it here would make one of the two unreachable.
    const input::Binding wanted{chord};
    for (const LayerTable &entry : kConflictLayers) {
        for (const QString &id : m_registry->ids()) {
            if (id == QLatin1String(kPaletteAction)) {
                continue;
            }
            const QList<input::Binding> bindings = m_keymap->bindingsFor(entry.layer, id);
            if (bindings.contains(wanted)) {
                m_controls->pendingChord = chord;
                m_controls->conflictText->setText(
                    tr("%1 is used by %2 — replace it? That action would have no shortcut.")
                        .arg(input::chordToString(chord), id));
                m_controls->conflictBar->show();
                return;
            }
        }
        // Actions the registry does not know (a hotkeys.toml typo) still hold
        // the chord, so the keymap is consulted for them through the ids it
        // reports as well; the registry covers every real action.
    }

    savePaletteShortcut(chord, false);
}

void SettingsWindow::savePaletteShortcut(const input::Chord &chord, bool replaceConflicts)
{
    m_controls->conflictBar->hide();
    m_controls->pendingChord.reset();

    const input::Binding wanted{chord};
    if (replaceConflicts) {
        // Whoever held the chord keeps their other bindings; mentioning an
        // action in hotkeys.toml replaces all of them, so the rest are written
        // back — an empty list when this was the only one.
        for (const LayerTable &entry : kConflictLayers) {
            for (const QString &id : m_registry->ids()) {
                if (id == QLatin1String(kPaletteAction)) {
                    continue;
                }
                const QList<input::Binding> bindings = m_keymap->bindingsFor(entry.layer, id);
                if (!bindings.contains(wanted)) {
                    continue;
                }
                QStringList remaining;
                for (const input::Binding &binding : bindings) {
                    if (binding != wanted) {
                        remaining << portableBinding(binding);
                    }
                }
                const auto result =
                    config::TomlWriter::setValue(hotkeysPath(), QLatin1String(entry.table), id,
                                                 config::TomlWriter::stringArray(remaining));
                if (!result.ok) {
                    Q_EMIT settingsChanged(tr("Could not save: %1").arg(result.error));
                    return;
                }
            }
        }
    }

    const auto result = config::TomlWriter::setValue(
        hotkeysPath(), QStringLiteral("global"), QLatin1String(kPaletteAction),
        config::TomlWriter::stringArray({portableChord(chord)}));
    if (!result.ok) {
        Q_EMIT settingsChanged(tr("Could not save: %1").arg(result.error));
        return;
    }
    m_controls->paletteRecorder->setShownBinding(input::chordToString(chord));
    Q_EMIT settingsChanged(
        tr("Command palette shortcut set to %1").arg(input::chordToString(chord)));
}

void SettingsWindow::resetPaletteShortcut()
{
    m_controls->conflictBar->hide();
    m_controls->paletteProblem->hide();
    m_controls->pendingChord.reset();

    const auto result = config::TomlWriter::removeValue(hotkeysPath(), QStringLiteral("global"),
                                                        QLatin1String(kPaletteAction));
    if (!result.ok) {
        Q_EMIT settingsChanged(tr("Could not save: %1").arg(result.error));
        return;
    }

    // The keymap is rebuilt when the watcher sees the file change; show the
    // default now rather than the stale binding until then.
    input::Keymap defaults;
    input::installDefaultKeymap(defaults);
    m_controls->paletteRecorder->setShownBinding(paletteBindingText(defaults));
    Q_EMIT settingsChanged(tr("Command palette shortcut reset"));
}

void SettingsWindow::writeConfig(const QString &table, const QString &key, const QString &value,
                                 const QString &description)
{
    const auto result = config::TomlWriter::setValue(configPath(), table, key, value);
    if (!result.ok) {
        Q_EMIT settingsChanged(tr("Could not save: %1").arg(result.error));
        return;
    }
    if (result.changed) {
        Q_EMIT settingsChanged(description);
    }
}

void SettingsWindow::writeTheme(const QString &table, const QString &key, const QString &value,
                                const QString &description)
{
    const auto result = config::TomlWriter::setValue(themePath(), table, key, value);
    if (!result.ok) {
        Q_EMIT settingsChanged(tr("Could not save: %1").arg(result.error));
        return;
    }
    if (result.changed) {
        Q_EMIT settingsChanged(description);
    }
}

} // namespace pf::ui

#include "SettingsWindow.moc"
