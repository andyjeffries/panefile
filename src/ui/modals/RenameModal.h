#pragma once

#include "fs/RenamePlan.h"
#include "fs/RenameRule.h"
#include "ui/modals/Modal.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QStackedWidget;
class QTreeWidget;

namespace pf::ui {

/// The bulk rename sheet.
///
/// This replaces §7.9's `$EDITOR` round trip with macOS Finder's "Rename Finder
/// Items" sheet: three modes — Replace Text, Add Text, Format — with a live
/// example, applied to the whole selection at once.
///
/// The reasoning, since it is a deliberate departure: §7.9's steps 1–3 exist to
/// get a list of names in front of the user, let them edit it, and detect that
/// they broke it. A sheet with a live preview does all three without leaving
/// the application, and cannot produce the line-count mismatch step 3 has to
/// guard against — the names are generated, not retyped.
///
/// Everything §7.9 specifies underneath is kept exactly: the diff, the cycle
/// detection via temporary names, the confirmation listing every old → new
/// pair, and a single undoable job.
class RenameModal : public Modal
{
    Q_OBJECT

public:
    explicit RenameModal(QWidget *parent);

    /// The names to rename, in the order they were selected. Order matters:
    /// Format mode numbers them.
    void setNames(const QList<QString> &names);

    /// Every name in the directory, so a collision with an uninvolved file can
    /// be reported before anything is renamed.
    void setExistingNames(const QList<QString> &names);

    /// Shows the sheet, resetting the rule to Replace Text.
    void start();

    /// The rule as the controls currently describe it. Exposed for testing.
    fs::RenameRule currentRule() const;

Q_SIGNALS:
    /// A validated, cycle-free plan the caller can hand to a RenameJob.
    void renameRequested(const pf::fs::RenamePlan &plan);

protected:
    void accept() override;

private:
    QWidget *buildReplacePage();
    QWidget *buildAddPage();
    QWidget *buildFormatPage();

    void updatePreview();

    QComboBox *m_mode = nullptr;
    QStackedWidget *m_pages = nullptr;

    // Replace Text
    QLineEdit *m_find = nullptr;
    QLineEdit *m_replace = nullptr;
    QCheckBox *m_caseSensitive = nullptr;

    // Add Text
    QLineEdit *m_addText = nullptr;
    QComboBox *m_addPosition = nullptr;

    // Format
    QComboBox *m_nameFormat = nullptr;
    QLineEdit *m_customText = nullptr;
    QComboBox *m_formatPosition = nullptr;
    QSpinBox *m_startNumber = nullptr;

    /// §7.9 step 5's confirmation, folded into the sheet as a live preview:
    /// every old → new pair is visible before OK is pressed rather than in a
    /// second modal after it.
    QTreeWidget *m_preview = nullptr;

    QLabel *m_problem = nullptr;
    QPushButton *m_rename = nullptr;

    QList<QString> m_names;
    QList<QString> m_existingNames;
};

} // namespace pf::ui
