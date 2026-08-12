// Keymap resolution and dispatch (§6.2, §14).
//
// §14's list for this module is unusually specific, and every item on it is a
// way the input model can be subtly wrong in a way a user would experience as
// "the keyboard sometimes does nothing": multiple simultaneous bindings for one
// action, sequence matching, prefix ambiguity, timeout expiry, a mistyped
// sequence swallowing the key rather than leaking it, the buffer clearing on
// focus change, and Meta/Super normalisation.

#include "input/ActionRegistry.h"
#include "input/Chord.h"
#include "input/DefaultKeymap.h"
#include "input/Keymap.h"
#include "app/KeyDispatcher.h"

#include <QKeyEvent>
#include <QSignalSpy>
#include <QTest>

using namespace pf;
using namespace pf::input;

class TestKeymap : public QObject
{
    Q_OBJECT

private:
    Keymap m_keymap;
    ActionRegistry m_registry;
    QStringList m_fired;

    Binding parse(const QString &text) const
    {
        const auto binding = parseBinding(text);
        Q_ASSERT(binding.has_value());
        return *binding;
    }

    void bindTo(KeymapLayer layer, const QString &binding, const QString &actionId)
    {
        m_keymap.bind(layer, parse(binding), actionId);
    }

    void defineAction(const QString &id)
    {
        m_registry.registerAction(id, id, ActionCategory::General, [this, id] { m_fired << id; });
    }

    /// Sends a key press through a dispatcher, as the application filter would.
    static bool press(KeyDispatcher &dispatcher, int key, Qt::KeyboardModifiers modifiers,
                      const QString &text)
    {
        QKeyEvent event(QEvent::KeyPress, key, modifiers, text);
        return dispatcher.handleKeyPress(&event);
    }

    /// Sends a bare printable character.
    static bool press(KeyDispatcher &dispatcher, const QString &character)
    {
        return press(dispatcher, character.at(0).toUpper().unicode(),
                     character.at(0).isUpper() ? Qt::ShiftModifier : Qt::NoModifier, character);
    }

private Q_SLOTS:
    void init();

    // Trie behaviour
    void exactMatchResolvesToTheAction();
    void partialMatchIsDistinctFromNoMatch();
    void oneActionCanHaveManyBindings();
    void ambiguousPrefixIsReported();
    void unbindRemovesEveryBinding();
    void conflictingBindingsKeepTheFirst();
    void precedenceFollowsLayerOrder();
    void partialMatchInAHigherLayerWins();

    // Dispatch behaviour
    void singleChordFires();
    void sequenceFiresOnCompletion();
    void pendingPrefixIsShown();
    void mistypedSequenceSwallowsTheKey();
    void unboundKeyWithNoBufferFallsThrough();
    void escapeClearsThePendingBuffer();
    void focusChangeClearsThePendingBuffer();
    void sequenceTimeoutClearsTheBuffer();
    void ambiguityTimeoutFiresTheShorterBinding();
    void ambiguityResolvedByTheNextKey();
    void disabledActionsDoNotFire();

    // The shipped defaults
    void defaultKeymapBindsTheDocumentedKeys();
    void defaultKeymapHasNoConflicts();
    void defaultKeymapKeepsLowerAndUpperCaseDistinct();
};

void TestKeymap::init()
{
    m_keymap.clear();
    m_registry.clear();
    m_fired.clear();
}

void TestKeymap::exactMatchResolvesToTheAction()
{
    bindTo(KeymapLayer::Normal, QStringLiteral("j"), QStringLiteral("list_down"));

    const Keymap::Match match = m_keymap.lookup(KeymapLayer::Normal, parse(QStringLiteral("j")));

    QCOMPARE(match.type, Keymap::MatchType::ExactMatch);
    QCOMPARE(match.actionId, QStringLiteral("list_down"));
    QVERIFY(!match.hasLongerBinding);
}

void TestKeymap::partialMatchIsDistinctFromNoMatch()
{
    // The reason for a trie rather than a flat hash. Without this distinction a
    // mistyped `g` followed by a stray key would leak the stray key into the
    // widget underneath.
    bindTo(KeymapLayer::Normal, QStringLiteral("g h"), QStringLiteral("go_home"));

    QCOMPARE(m_keymap.lookup(KeymapLayer::Normal, parse(QStringLiteral("g"))).type,
             Keymap::MatchType::PartialMatch);
    QCOMPARE(m_keymap.lookup(KeymapLayer::Normal, parse(QStringLiteral("z"))).type,
             Keymap::MatchType::NoMatch);
}

void TestKeymap::oneActionCanHaveManyBindings()
{
    // §6.2: "An action may have any number of bindings, and they are fully
    // independent — so copy_items can be bound to Ctrl+C, Super+C and yy
    // simultaneously, all active at once."
    bindTo(KeymapLayer::Global, QStringLiteral("Ctrl+C"), QStringLiteral("copy_items"));
    bindTo(KeymapLayer::Global, QStringLiteral("Super+C"), QStringLiteral("copy_items"));
    bindTo(KeymapLayer::Global, QStringLiteral("y y"), QStringLiteral("copy_items"));

    for (const QString &binding :
         {QStringLiteral("Ctrl+C"), QStringLiteral("Meta+C"), QStringLiteral("y y")}) {
        const Keymap::Match match = m_keymap.lookup(KeymapLayer::Global, parse(binding));
        QCOMPARE(match.type, Keymap::MatchType::ExactMatch);
        QCOMPARE(match.actionId, QStringLiteral("copy_items"));
    }

    QCOMPARE(m_keymap.bindingsFor(KeymapLayer::Global, QStringLiteral("copy_items")).size(), 3);
}

void TestKeymap::ambiguousPrefixIsReported()
{
    // §6.2's awkward case: `g` bound *and* `g h` bound.
    bindTo(KeymapLayer::Normal, QStringLiteral("g"), QStringLiteral("go_somewhere"));
    bindTo(KeymapLayer::Normal, QStringLiteral("g h"), QStringLiteral("go_home"));

    const Keymap::Match match = m_keymap.lookup(KeymapLayer::Normal, parse(QStringLiteral("g")));

    QCOMPARE(match.type, Keymap::MatchType::ExactMatch);
    QCOMPARE(match.actionId, QStringLiteral("go_somewhere"));
    QVERIFY(match.hasLongerBinding);
}

void TestKeymap::unbindRemovesEveryBinding()
{
    // §8.2's `unbind` pseudo-action and `open_zoxide = []`.
    bindTo(KeymapLayer::Global, QStringLiteral("Ctrl+C"), QStringLiteral("copy_items"));
    bindTo(KeymapLayer::Global, QStringLiteral("y y"), QStringLiteral("copy_items"));

    m_keymap.unbind(KeymapLayer::Global, QStringLiteral("copy_items"));

    QCOMPARE(m_keymap.lookup(KeymapLayer::Global, parse(QStringLiteral("Ctrl+C"))).type,
             Keymap::MatchType::NoMatch);
    // The `y` prefix must go too, or a stray `y` would still be swallowed while
    // the application waited for a second chord that can no longer complete.
    QCOMPARE(m_keymap.lookup(KeymapLayer::Global, parse(QStringLiteral("y"))).type,
             Keymap::MatchType::NoMatch);
}

void TestKeymap::conflictingBindingsKeepTheFirst()
{
    // §6.2: "Two actions bound to the identical sequence within the same layer
    // is a config error: log it with both action ids, keep the one declared
    // first, and list the conflict in the help modal."
    QVERIFY(m_keymap.bind(KeymapLayer::Normal, parse(QStringLiteral("x")),
                          QStringLiteral("first_action")));
    QVERIFY(!m_keymap.bind(KeymapLayer::Normal, parse(QStringLiteral("x")),
                           QStringLiteral("second_action")));

    QCOMPARE(m_keymap.lookup(KeymapLayer::Normal, parse(QStringLiteral("x"))).actionId,
             QStringLiteral("first_action"));

    QCOMPARE(m_keymap.conflicts().size(), 1);
    QCOMPARE(m_keymap.conflicts().first().keptActionId, QStringLiteral("first_action"));
    QCOMPARE(m_keymap.conflicts().first().rejectedActionId, QStringLiteral("second_action"));
}

void TestKeymap::precedenceFollowsLayerOrder()
{
    bindTo(KeymapLayer::Normal, QStringLiteral("v"), QStringLiteral("enter_selection"));
    bindTo(KeymapLayer::Selection, QStringLiteral("v"), QStringLiteral("leave_selection"));

    // §6.2: Selection before Normal.
    const Keymap::Match inSelection =
        m_keymap.lookup({KeymapLayer::Selection, KeymapLayer::Normal}, parse(QStringLiteral("v")));
    QCOMPARE(inSelection.actionId, QStringLiteral("leave_selection"));

    const Keymap::Match inNormal =
        m_keymap.lookup({KeymapLayer::Normal, KeymapLayer::Global}, parse(QStringLiteral("v")));
    QCOMPARE(inNormal.actionId, QStringLiteral("enter_selection"));
}

void TestKeymap::partialMatchInAHigherLayerWins()
{
    // A partial match above must beat an exact match below, or a modal's `g h`
    // would be shadowed by a global `g` and could never reach its second chord.
    bindTo(KeymapLayer::Modal, QStringLiteral("g h"), QStringLiteral("modal_go_home"));
    bindTo(KeymapLayer::Global, QStringLiteral("g"), QStringLiteral("global_g"));

    const Keymap::Match match =
        m_keymap.lookup({KeymapLayer::Modal, KeymapLayer::Global}, parse(QStringLiteral("g")));

    QCOMPARE(match.type, Keymap::MatchType::PartialMatch);
}

void TestKeymap::singleChordFires()
{
    defineAction(QStringLiteral("list_down"));
    bindTo(KeymapLayer::Normal, QStringLiteral("j"), QStringLiteral("list_down"));

    KeyDispatcher dispatcher(&m_registry, &m_keymap);
    dispatcher.setActiveLayers({KeymapLayer::Normal});

    QVERIFY(press(dispatcher, QStringLiteral("j")));
    QCOMPARE(m_fired, QStringList{"list_down"});
    QVERIFY(!dispatcher.hasPending());
}

void TestKeymap::sequenceFiresOnCompletion()
{
    defineAction(QStringLiteral("go_home"));
    bindTo(KeymapLayer::Normal, QStringLiteral("g h"), QStringLiteral("go_home"));

    KeyDispatcher dispatcher(&m_registry, &m_keymap);
    dispatcher.setActiveLayers({KeymapLayer::Normal});

    QVERIFY(press(dispatcher, QStringLiteral("g")));
    QVERIFY(m_fired.isEmpty());
    QVERIFY(dispatcher.hasPending());

    QVERIFY(press(dispatcher, QStringLiteral("h")));
    QCOMPARE(m_fired, QStringList{"go_home"});
    QVERIFY(!dispatcher.hasPending());
}

void TestKeymap::pendingPrefixIsShown()
{
    // §6.2 step 3: "show the pending prefix in the footer (`g-`)". The trailing
    // dash is what tells the user the application is waiting rather than having
    // ignored the key.
    bindTo(KeymapLayer::Normal, QStringLiteral("g h"), QStringLiteral("go_home"));

    KeyDispatcher dispatcher(&m_registry, &m_keymap);
    dispatcher.setActiveLayers({KeymapLayer::Normal});

    QSignalSpy pending(&dispatcher, &KeyDispatcher::pendingChanged);
    press(dispatcher, QStringLiteral("g"));

    QCOMPARE(dispatcher.pendingText(), QStringLiteral("g-"));
    QCOMPARE(pending.count(), 1);
    QCOMPARE(pending.first().first().toString(), QStringLiteral("g-"));
}

void TestKeymap::mistypedSequenceSwallowsTheKey()
{
    // §6.2 step 3: "if the buffer had content, clear it and swallow the event
    // (a mistyped sequence must not leak keys into the app)." Leaking would
    // mean a mistyped `g` followed by `q` quitting the application.
    defineAction(QStringLiteral("go_home"));
    bindTo(KeymapLayer::Normal, QStringLiteral("g h"), QStringLiteral("go_home"));

    KeyDispatcher dispatcher(&m_registry, &m_keymap);
    dispatcher.setActiveLayers({KeymapLayer::Normal});

    QVERIFY(press(dispatcher, QStringLiteral("g")));
    QVERIFY(press(dispatcher, QStringLiteral("z"))); // consumed, not leaked

    QVERIFY(m_fired.isEmpty());
    QVERIFY(!dispatcher.hasPending());
}

void TestKeymap::unboundKeyWithNoBufferFallsThrough()
{
    // The other half of step 3: with an empty buffer the event was never ours,
    // so it must reach the widget underneath — otherwise typing in a text field
    // would be impossible.
    KeyDispatcher dispatcher(&m_registry, &m_keymap);
    dispatcher.setActiveLayers({KeymapLayer::Normal});

    QVERIFY(!press(dispatcher, QStringLiteral("z")));
}

void TestKeymap::escapeClearsThePendingBuffer()
{
    // §6.2 step 4.
    bindTo(KeymapLayer::Normal, QStringLiteral("g h"), QStringLiteral("go_home"));

    KeyDispatcher dispatcher(&m_registry, &m_keymap);
    dispatcher.setActiveLayers({KeymapLayer::Normal});

    press(dispatcher, QStringLiteral("g"));
    QVERIFY(dispatcher.hasPending());

    QVERIFY(press(dispatcher, Qt::Key_Escape, Qt::NoModifier, {}));
    QVERIFY(!dispatcher.hasPending());

    // With nothing pending, Escape is no longer ours and falls through to
    // whatever binding or widget wants it.
    QVERIFY(!press(dispatcher, Qt::Key_Escape, Qt::NoModifier, {}));
}

void TestKeymap::focusChangeClearsThePendingBuffer()
{
    // §6.2 step 5: "Any pointer click, focus change or panel switch clears the
    // pending buffer." A half-typed sequence completing after a panel switch
    // would act on a panel the user is no longer looking at.
    bindTo(KeymapLayer::Normal, QStringLiteral("g h"), QStringLiteral("go_home"));

    KeyDispatcher dispatcher(&m_registry, &m_keymap);
    dispatcher.setActiveLayers({KeymapLayer::Normal});

    press(dispatcher, QStringLiteral("g"));
    QVERIFY(dispatcher.hasPending());

    dispatcher.clearPending();
    QVERIFY(!dispatcher.hasPending());
    QVERIFY(dispatcher.pendingText().isEmpty());
}

void TestKeymap::sequenceTimeoutClearsTheBuffer()
{
    bindTo(KeymapLayer::Normal, QStringLiteral("g h"), QStringLiteral("go_home"));

    KeyDispatcher dispatcher(&m_registry, &m_keymap);
    dispatcher.setActiveLayers({KeymapLayer::Normal});
    dispatcher.setSequenceTimeout(30);

    press(dispatcher, QStringLiteral("g"));
    QVERIFY(dispatcher.hasPending());

    QTRY_VERIFY_WITH_TIMEOUT(!dispatcher.hasPending(), 2000);
    QVERIFY(m_fired.isEmpty());
}

void TestKeymap::ambiguityTimeoutFiresTheShorterBinding()
{
    // §6.2: "If the timer fires, invoke the shorter action."
    defineAction(QStringLiteral("go_somewhere"));
    defineAction(QStringLiteral("go_home"));
    bindTo(KeymapLayer::Normal, QStringLiteral("g"), QStringLiteral("go_somewhere"));
    bindTo(KeymapLayer::Normal, QStringLiteral("g h"), QStringLiteral("go_home"));

    KeyDispatcher dispatcher(&m_registry, &m_keymap);
    dispatcher.setActiveLayers({KeymapLayer::Normal});
    dispatcher.setAmbiguityTimeout(30);
    dispatcher.setSequenceTimeout(2000);

    QVERIFY(press(dispatcher, QStringLiteral("g")));
    QVERIFY(m_fired.isEmpty()); // held, not fired

    QTRY_COMPARE_WITH_TIMEOUT(m_fired, QStringList{"go_somewhere"}, 2000);
}

void TestKeymap::ambiguityResolvedByTheNextKey()
{
    // "If another key arrives first, continue resolving."
    defineAction(QStringLiteral("go_somewhere"));
    defineAction(QStringLiteral("go_home"));
    bindTo(KeymapLayer::Normal, QStringLiteral("g"), QStringLiteral("go_somewhere"));
    bindTo(KeymapLayer::Normal, QStringLiteral("g h"), QStringLiteral("go_home"));

    KeyDispatcher dispatcher(&m_registry, &m_keymap);
    dispatcher.setActiveLayers({KeymapLayer::Normal});
    dispatcher.setAmbiguityTimeout(5000);

    press(dispatcher, QStringLiteral("g"));
    press(dispatcher, QStringLiteral("h"));

    QCOMPARE(m_fired, QStringList{"go_home"});

    // The shorter action must not fire afterwards when its timer would have
    // expired — the ambiguity was already resolved.
    QTest::qWait(50);
    QCOMPARE(m_fired, QStringList{"go_home"});
}

void TestKeymap::disabledActionsDoNotFire()
{
    m_registry.registerAction(
        QStringLiteral("close_file_panel"), QStringLiteral("Close"), ActionCategory::Panels,
        [this] { m_fired << QStringLiteral("close_file_panel"); }, [] { return false; });

    bindTo(KeymapLayer::Normal, QStringLiteral("w"), QStringLiteral("close_file_panel"));

    KeyDispatcher dispatcher(&m_registry, &m_keymap);
    dispatcher.setActiveLayers({KeymapLayer::Normal});

    // Consumed — the key *is* bound — but the handler does not run.
    QVERIFY(press(dispatcher, QStringLiteral("w")));
    QVERIFY(m_fired.isEmpty());
}

void TestKeymap::defaultKeymapBindsTheDocumentedKeys()
{
    installDefaultKeymap(m_keymap);

    struct Expectation {
        KeymapLayer layer;
        const char *binding;
        const char *actionId;
    };

    // A representative row from each table of §6.3.
    constexpr Expectation kExpectations[] = {
        {KeymapLayer::Normal, "j", "list_down"},
        {KeymapLayer::Normal, "k", "list_up"},
        {KeymapLayer::Normal, "h", "parent_directory"},
        {KeymapLayer::Normal, "l", "confirm"},
        {KeymapLayer::Normal, "g g", "list_top"},
        {KeymapLayer::Normal, "g h", "go_home"},
        {KeymapLayer::Normal, "g t", "go_trash"},
        {KeymapLayer::Normal, "n", "create_new_file_panel"},
        {KeymapLayer::Normal, "N", "split_file_panel"},
        {KeymapLayer::Normal, "w", "close_file_panel"},
        {KeymapLayer::Global, "Tab", "next_file_panel"},
        {KeymapLayer::Global, "Ctrl+C", "copy_items"},
        {KeymapLayer::Global, "Ctrl+V", "paste_items"},
        {KeymapLayer::Normal, "?", "open_help_menu"},
        {KeymapLayer::Selection, "J", "select_down"},
    };

    for (const Expectation &expected : kExpectations) {
        const Keymap::Match match =
            m_keymap.lookup(expected.layer, parse(QString::fromLatin1(expected.binding)));
        QVERIFY2(
            match.type == Keymap::MatchType::ExactMatch,
            qPrintable(QStringLiteral("%1 is not bound").arg(QLatin1String(expected.binding))));
        QCOMPARE(match.actionId, QLatin1String(expected.actionId));
    }
}

void TestKeymap::defaultKeymapHasNoConflicts()
{
    installDefaultKeymap(m_keymap);

    if (!m_keymap.conflicts().isEmpty()) {
        for (const KeymapConflict &conflict : m_keymap.conflicts()) {
            qWarning("%s is bound to both %s and %s", qPrintable(bindingToString(conflict.binding)),
                     qPrintable(conflict.keptActionId), qPrintable(conflict.rejectedActionId));
        }
    }
    // A conflict in the application's own defaults is a bug in the table, not
    // in a user's config.
    QCOMPARE(m_keymap.conflicts().size(), 0);
}

void TestKeymap::defaultKeymapKeepsLowerAndUpperCaseDistinct()
{
    installDefaultKeymap(m_keymap);

    // The property the text-form chord exists for. If these collapsed, one of
    // the two actions would be unreachable.
    QCOMPARE(m_keymap.lookup(KeymapLayer::Normal, parse(QStringLiteral("n"))).actionId,
             QStringLiteral("create_new_file_panel"));
    QCOMPARE(m_keymap.lookup(KeymapLayer::Normal, parse(QStringLiteral("N"))).actionId,
             QStringLiteral("split_file_panel"));

    QCOMPARE(m_keymap.lookup(KeymapLayer::Normal, parse(QStringLiteral("e"))).actionId,
             QStringLiteral("open_file_with_editor"));
    QCOMPARE(m_keymap.lookup(KeymapLayer::Normal, parse(QStringLiteral("E"))).actionId,
             QStringLiteral("open_current_directory_with_editor"));
}

QTEST_MAIN(TestKeymap)
#include "tst_keymap.moc"
