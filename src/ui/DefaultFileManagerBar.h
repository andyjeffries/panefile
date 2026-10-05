#pragma once

#include <QWidget>

class QLabel;
class QPushButton;
class QTimer;

namespace pf::ui {

/// "Make Panefile your default file manager?" — a slim strip along the top of
/// the window.
///
/// Not a dialog. A modal question at launch stands between the user and the
/// folder they opened Panefile to look at, and answering it would be the first
/// thing anyone did with the application. The strip asks without blocking:
/// panels keep their focus and their keys, and ignoring it is an answer.
///
/// The buttons never take focus, for the same reason. The choices are actions
/// in the registry, bound to keys the user can remap, which is how they are
/// reachable without a mouse; the bar shows those keys beside each button.
///
/// The bar only displays. What each choice does is the composition root's
/// business, which is where the files are written and the bus is spoken to.
class DefaultFileManagerBar : public QWidget
{
    Q_OBJECT

public:
    /// How long an outcome stays before the bar goes. Long enough to read a
    /// sentence naming a file, short enough not to become furniture.
    static constexpr int kResultMs = 6000;

    explicit DefaultFileManagerBar(QWidget *parent = nullptr);

    /// The question, with its three answers.
    void showOffer();

    /// Work is under way: the text, and no buttons to press twice.
    void showBusy(const QString &text);

    /// The outcome, which hides itself after kResultMs. Calling it again while
    /// one is showing replaces the text and restarts the clock.
    void showResult(const QString &text, bool success);

    /// Keys shown beside each button, e.g. "Alt+Y". Empty hides a hint, which
    /// is what a user who unbound the action should see.
    void setKeyHints(const QString &yes, const QString &notNow, const QString &never);

    /// Showing the question, as opposed to an outcome or nothing.
    bool isOffering() const;

    /// Showing anything at all.
    bool isShowingResult() const;

    QString text() const;

Q_SIGNALS:
    void accepted();
    void dismissed();
    void declined();

    /// An outcome finished showing and the bar hid itself.
    void finished();

private:
    void setButtonsVisible(bool visible);

    QLabel *m_text = nullptr;
    QPushButton *m_yes = nullptr;
    QPushButton *m_notNow = nullptr;
    QPushButton *m_never = nullptr;
    QTimer *m_hideTimer = nullptr;
    bool m_offering = false;
    bool m_showingResult = false;
};

} // namespace pf::ui
