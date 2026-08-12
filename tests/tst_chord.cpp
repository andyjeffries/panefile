// Chord and binding parsing (§6.2, §8.2, §14).
//
// §14 asks for "keymap parsing and conflict detection" and for
// "Meta/Super alias normalisation". The case that drove the design is subtler
// than either: §6.3 binds `j` to list_down and `J` to select_down, and
// QKeySequence parses both to Key_J. Anything built directly on QKeySequence
// silently merges them.

#include "input/Chord.h"

#include <QTest>

using namespace pf::input;

class TestChord : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void bareLettersKeepTheirCase();
    void bareSymbolsAreTextForm();
    void namedKeysAreKeyForm();
    void modifiersUseKeyForm();
    void superIsAnAliasForMeta();
    void cmdAndCommandAreAliasesForMeta();
    void unparseableChordsAreRejected_data();
    void unparseableChordsAreRejected();

    void sequencesSplitOnWhitespace();
    void longSequencesAreRejected();
    void sequencesCannotStartWithEscape();
    void aBareEscapeIsAllowed();
    void emptyBindingIsRejected();

    void keyEventsMatchParsedChords_data();
    void keyEventsMatchParsedChords();
    void bareModifierPressesProduceNothing();
    void shiftedLettersMatchTheirUppercaseBinding();
    void whitespaceKeysUseKeyFormNotTextForm();
    void keypadModifierIsIgnored();

    void renderingRoundTrips();
};

void TestChord::bareLettersKeepTheirCase()
{
    const auto lower = parseChord(QStringLiteral("j"));
    const auto upper = parseChord(QStringLiteral("J"));

    QVERIFY(lower.has_value());
    QVERIFY(upper.has_value());
    QVERIFY(lower->isTextForm());
    QVERIFY(upper->isTextForm());

    // The whole reason the text form exists. §6.3 binds these to list_down and
    // select_down, so collapsing them would make one of the two unreachable.
    QVERIFY(*lower != *upper);
    QCOMPARE(lower->text, QStringLiteral("j"));
    QCOMPARE(upper->text, QStringLiteral("J"));
}

void TestChord::bareSymbolsAreTextForm()
{
    for (const QString &symbol : {QStringLiteral("?"), QStringLiteral("/"), QStringLiteral(":"),
                                  QStringLiteral(">"), QStringLiteral(".")}) {
        const auto chord = parseChord(symbol);
        QVERIFY2(chord.has_value(), qPrintable(symbol));
        QVERIFY2(chord->isTextForm(), qPrintable(symbol));
        QCOMPARE(chord->text, symbol);
    }
}

void TestChord::namedKeysAreKeyForm()
{
    const auto tab = parseChord(QStringLiteral("Tab"));
    QVERIFY(tab.has_value());
    QVERIFY(!tab->isTextForm());
    QCOMPARE(tab->key, int(Qt::Key_Tab));

    const auto space = parseChord(QStringLiteral("Space"));
    QVERIFY(space.has_value());
    QVERIFY(!space->isTextForm());
    QCOMPARE(space->key, int(Qt::Key_Space));
}

void TestChord::modifiersUseKeyForm()
{
    const auto chord = parseChord(QStringLiteral("Ctrl+C"));
    QVERIFY(chord.has_value());
    QVERIFY(!chord->isTextForm());
    QCOMPARE(chord->key, int(Qt::Key_C));
    QVERIFY(chord->modifiers.testFlag(Qt::ControlModifier));
}

void TestChord::superIsAnAliasForMeta()
{
    // §8.2: "Accept Super+C as an alias for Meta+C, because that's what users
    // will type."
    const auto super = parseChord(QStringLiteral("Super+C"));
    const auto meta = parseChord(QStringLiteral("Meta+C"));

    QVERIFY(super.has_value());
    QVERIFY(meta.has_value());
    QCOMPARE(*super, *meta);
}

void TestChord::cmdAndCommandAreAliasesForMeta()
{
    const auto meta = parseChord(QStringLiteral("Meta+K"));
    QVERIFY(meta.has_value());

    for (const QString &spelling : {QStringLiteral("Cmd+K"), QStringLiteral("Command+K"),
                                    QStringLiteral("super+k"), QStringLiteral("WIN+K")}) {
        const auto chord = parseChord(spelling);
        QVERIFY2(chord.has_value(), qPrintable(spelling));
        QCOMPARE(*chord, *meta);
    }
}

void TestChord::unparseableChordsAreRejected_data()
{
    QTest::addColumn<QString>("text");

    QTest::newRow("empty") << "";
    QTest::newRow("whitespace") << "   ";
    QTest::newRow("nonsense") << "NotAKey";
    // §8.2: Qt's own comma-separated multi-element syntax is not used here,
    // because whitespace is the sequence separator and the two would collide.
    QTest::newRow("qt sequence syntax") << "Ctrl+C,Ctrl+V";
    QTest::newRow("dangling modifier") << "Ctrl+";
}

void TestChord::unparseableChordsAreRejected()
{
    QFETCH(QString, text);
    QVERIFY(!parseChord(text).has_value());
}

void TestChord::sequencesSplitOnWhitespace()
{
    QString error;
    const auto binding = parseBinding(QStringLiteral("g h"), &error);

    QVERIFY2(binding.has_value(), qPrintable(error));
    QCOMPARE(binding->size(), 2);
    QCOMPARE(binding->at(0).text, QStringLiteral("g"));
    QCOMPARE(binding->at(1).text, QStringLiteral("h"));
}

void TestChord::longSequencesAreRejected()
{
    // §8.2 caps sequences at five chords.
    QString error;
    QVERIFY(parseBinding(QStringLiteral("a b c d e"), &error).has_value());

    error.clear();
    QVERIFY(!parseBinding(QStringLiteral("a b c d e f"), &error).has_value());
    QVERIFY2(error.contains(QStringLiteral("maximum")), qPrintable(error));
}

void TestChord::sequencesCannotStartWithEscape()
{
    // §8.2 rejects these because §6.2 step 4 makes Escape always clear the
    // pending buffer, so the sequence could never reach its second chord.
    QString error;
    QVERIFY(!parseBinding(QStringLiteral("Esc x"), &error).has_value());
    QVERIFY2(error.contains(QStringLiteral("Escape")), qPrintable(error));
}

void TestChord::aBareEscapeIsAllowed()
{
    // A single Escape is fine — §6.3 binds `quit` to it.
    QVERIFY(parseBinding(QStringLiteral("Esc")).has_value());
}

void TestChord::emptyBindingIsRejected()
{
    QString error;
    QVERIFY(!parseBinding(QStringLiteral("   "), &error).has_value());
    QVERIFY(!error.isEmpty());
}

void TestChord::keyEventsMatchParsedChords_data()
{
    QTest::addColumn<QString>("configText");
    QTest::addColumn<int>("key");
    QTest::addColumn<Qt::KeyboardModifiers>("modifiers");
    QTest::addColumn<QString>("eventText");

    QTest::newRow("j") << "j" << int(Qt::Key_J) << Qt::KeyboardModifiers(Qt::NoModifier) << "j";
    QTest::newRow("J") << "J" << int(Qt::Key_J) << Qt::KeyboardModifiers(Qt::ShiftModifier) << "J";
    QTest::newRow("?") << "?" << int(Qt::Key_Question) << Qt::KeyboardModifiers(Qt::ShiftModifier)
                       << "?";
    QTest::newRow("Ctrl+C") << "Ctrl+C" << int(Qt::Key_C)
                            << Qt::KeyboardModifiers(Qt::ControlModifier) << QString();
    QTest::newRow("Tab") << "Tab" << int(Qt::Key_Tab) << Qt::KeyboardModifiers(Qt::NoModifier)
                         << "\t";
    QTest::newRow("Space") << "Space" << int(Qt::Key_Space) << Qt::KeyboardModifiers(Qt::NoModifier)
                           << " ";
    QTest::newRow("Return") << "Return" << int(Qt::Key_Return)
                            << Qt::KeyboardModifiers(Qt::NoModifier) << "\r";
    QTest::newRow("Shift+Tab") << "Shift+Tab" << int(Qt::Key_Tab)
                               << Qt::KeyboardModifiers(Qt::ShiftModifier) << "\t";
    QTest::newRow("Alt+Home") << "Alt+Home" << int(Qt::Key_Home)
                              << Qt::KeyboardModifiers(Qt::AltModifier) << QString();
}

void TestChord::keyEventsMatchParsedChords()
{
    QFETCH(QString, configText);
    QFETCH(int, key);
    QFETCH(Qt::KeyboardModifiers, modifiers);
    QFETCH(QString, eventText);

    // The property the whole input system rests on: what a config file says and
    // what a key press produces must land on the same Chord, or no binding ever
    // fires.
    const auto parsed = parseChord(configText);
    const auto pressed = chordFromKeyEvent(key, modifiers, eventText);

    QVERIFY2(parsed.has_value(), qPrintable(configText));
    QVERIFY2(pressed.has_value(), qPrintable(configText));
    QCOMPARE(*pressed, *parsed);
}

void TestChord::bareModifierPressesProduceNothing()
{
    // §6.2 step 1. Without this, reaching for Ctrl in the middle of a sequence
    // would clear the pending buffer.
    for (const int key : {int(Qt::Key_Control), int(Qt::Key_Shift), int(Qt::Key_Alt),
                          int(Qt::Key_Meta), int(Qt::Key_CapsLock)}) {
        QVERIFY(!chordFromKeyEvent(key, Qt::NoModifier, {}).has_value());
    }
}

void TestChord::shiftedLettersMatchTheirUppercaseBinding()
{
    // Shift is folded into the character rather than recorded separately, so
    // `J` and `Shift+J` are one binding rather than two identical-looking ones.
    const auto pressed = chordFromKeyEvent(Qt::Key_J, Qt::ShiftModifier, QStringLiteral("J"));
    QVERIFY(pressed.has_value());
    QCOMPARE(pressed->modifiers, Qt::NoModifier);
    QCOMPARE(pressed->text, QStringLiteral("J"));
}

void TestChord::whitespaceKeysUseKeyFormNotTextForm()
{
    // Space produces " " as text, but a config names it `Space`. Letting it
    // take the text form would mean the two spellings never matched.
    const auto pressed = chordFromKeyEvent(Qt::Key_Space, Qt::NoModifier, QStringLiteral(" "));
    QVERIFY(pressed.has_value());
    QVERIFY(!pressed->isTextForm());
    QCOMPARE(pressed->key, int(Qt::Key_Space));
}

void TestChord::keypadModifierIsIgnored()
{
    // KeypadModifier says which physical key produced the event, not what the
    // user meant, and no config file ever writes it.
    const auto withKeypad =
        chordFromKeyEvent(Qt::Key_Home, Qt::AltModifier | Qt::KeypadModifier, {});
    const auto without = chordFromKeyEvent(Qt::Key_Home, Qt::AltModifier, {});

    QVERIFY(withKeypad.has_value());
    QVERIFY(without.has_value());
    QCOMPARE(*withKeypad, *without);
}

void TestChord::renderingRoundTrips()
{
    QCOMPARE(bindingToString(*parseBinding(QStringLiteral("g h"))), QStringLiteral("g h"));
    QVERIFY(!bindingToString(*parseBinding(QStringLiteral("Ctrl+C"))).isEmpty());
}

QTEST_MAIN(TestChord)
#include "tst_chord.moc"
