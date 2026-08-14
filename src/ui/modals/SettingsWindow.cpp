#include "ui/modals/SettingsWindow.h"

#include "input/ActionRegistry.h"
#include "input/Chord.h"
#include "input/Keymap.h"
#include "config/Config.h"
#include "config/Theme.h"
#include "config/TomlWriter.h"
#include "platform/Paths.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QFile>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QPushButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace pf::ui {
namespace {

/// The tab strip's buttons. Checkable, flat, and laid out as a glyph over a
/// label — which is what makes the row read as a macOS preferences toolbar
/// rather than as a row of push buttons.
QPushButton *makeTabButton(const QString &glyph, const QString &title)
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

    auto *icon = new QLabel(glyph);
    icon->setObjectName(QStringLiteral("settingsTabGlyph"));
    icon->setAlignment(Qt::AlignCenter);
    layout->addWidget(icon);

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

    // Quick Look
    QComboBox *dock = nullptr;
    QSpinBox *floatSize = nullptr;
    QCheckBox *followCursor = nullptr;
    QCheckBox *closeOnPanelSwitch = nullptr;

    // Keys
    QTreeWidget *keys = nullptr;
    QLineEdit *keyFilter = nullptr;
};

SettingsWindow::SettingsWindow(input::ActionRegistry *registry, input::Keymap *keymap,
                               QWidget *parent)
    : Modal(parent), m_registry(registry), m_keymap(keymap), m_toolbar(buildToolbar()),
      m_tabs(new QButtonGroup(this)), m_pages(new QStackedWidget),
      m_controls(std::make_unique<Controls>())
{
    setSizePercent(72, 78);

    auto *layout = new QVBoxLayout(contentWidget());
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_tabs->setExclusive(true);
    layout->addWidget(m_toolbar);

    m_pages->setObjectName(QStringLiteral("settingsPages"));
    layout->addWidget(m_pages, 1);

    addTab(tr("Appearance"), QStringLiteral("◐"), buildAppearanceTab());
    addTab(tr("General"), QStringLiteral("⚙"), buildGeneralTab());
    addTab(tr("Quick Look"), QStringLiteral("◱"), buildQuickLookTab());
    addTab(tr("Keys"), QStringLiteral("⌘"), buildKeysTab());

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

void SettingsWindow::addTab(const QString &title, const QString &glyph, QWidget *page)
{
    auto *button = makeTabButton(glyph, title);
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
    form->setHorizontalSpacing(16);
    form->setVerticalSpacing(12);

    m_controls->fontSize = new QSpinBox;
    m_controls->fontSize->setRange(6, 32);
    m_controls->fontSize->setSuffix(tr(" pt"));
    connect(m_controls->fontSize, &QSpinBox::valueChanged, this, [this](int value) {
        if (!m_loading) {
            writeTheme(QStringLiteral("ui"), QStringLiteral("font_size"),
                       config::TomlWriter::number(value), tr("Font size: %1 pt").arg(value));
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

    return page;
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

    // Read-only for now, deliberately. The whole table, searchable, is most of
    // the value; a chord recorder that has to capture keys without the
    // dispatcher acting on them, and detect that a chord is already bound
    // elsewhere, is where the risk is.
    auto *note = new QLabel(tr("Edit hotkeys.toml to change these."));
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
    m_controls->keys->resizeColumnToContents(0);
    m_controls->keys->resizeColumnToContents(1);

    m_loading = false;
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
