#include <tsre/tests/KeysTestSuite.h>

#include <QKeyEvent>
#include <QStringList>
#include <cmath>
#include <set>
#include <tsre/Game.h>
#include <tsre/GameObj.h>
#include <tsre/KeyPosition.h>
#include <tsre/KeyTaker.h>
#include <tsre/camera/CameraFree.h>
#include <tsre/sim/DrivingKeys.h>
#include <tsre/tests/TokenTestSupport.h>
#include <tsre/trains/Consist.h>

namespace {

// A key pressed or released, as Qt gives it without a platform's codes.
QKeyEvent press(int key, Qt::KeyboardModifiers modifiers = Qt::NoModifier, bool repeat = false) {
    return QKeyEvent(QEvent::KeyPress, key, modifiers, QString(), repeat);
}
QKeyEvent release(int key, Qt::KeyboardModifiers modifiers = Qt::NoModifier, bool repeat = false) {
    return QKeyEvent(QEvent::KeyRelease, key, modifiers, QString(), repeat);
}

struct Taker : KeyTaker {
    bool keyPress(const QKeyEvent &) override { return true; }
    bool keyRelease(const QKeyEvent &) override { return true; }
    void releaseKeys() override {}
};

// An object the camera rides, taking the keys or not.
struct Ridden : GameObj {
    Taker taker;
    bool takes = false;
    void getCameraPosition(float *out) override {
        const float place[8] = {0.0f, 0.0f, 100.0f, 50.0f, 200.0f, 0.0f, 0.0f, 0.0f};
        std::copy(place, place + 8, out);
    }
    KeyTaker *keyTaker() override { return takes ? &taker : nullptr; }
};

// The camera riding an object: the arrows move it while the object takes
// the keys, whatever the keypad setting (W, A, S and D are the train's),
// and not otherwise without that setting (the editor's tools have them).
void checkCamera(TokenTest::Suite &test) {
    GameObj plain;
    Consist consist;
    Taker taker;
    test.check(plain.keyTaker() == nullptr && consist.keyTaker() == nullptr
                       && (consist.keys = &taker, consist.keyTaker() == &taker),
               "an object takes no keys; a consist takes them with the driven train's keys given");
    consist.keys = nullptr;

    const bool keypad = Game::usenNumPad;
    Game::usenNumPad = false;
    float tile[2] = {0.0f, 0.0f};
    CameraFree camera(tile);
    Ridden ridden;
    camera.setCameraObject(&ridden);
    const auto moved = [&](bool takes) {
        ridden.takes = takes;
        camera.update(30.0f);
        const float before[3] = {camera.getPos()[0], camera.getPos()[1], camera.getPos()[2]};
        QKeyEvent up = press(Qt::Key_Up);
        camera.keyDown(&up);
        for (int i = 0; i < 10; ++i)
            camera.update(30.0f);
        QKeyEvent upReleased = release(Qt::Key_Up);
        camera.keyUp(&upReleased);
        camera.releaseKeys();
        const float *after = camera.getPos();
        return std::hypot(after[0] - before[0], after[2] - before[2]);
    };
    const float taking = moved(true), notTaking = moved(false);
    Game::usenNumPad = true;
    const float keypadOn = moved(false);
    Game::usenNumPad = keypad;
    camera.releaseObject();
    test.check(taking > 0.1f && notTaking == 0.0f && keypadOn > 0.1f,
               QString("the riding camera's Up arrow: %1 m riding a train taking the keys, %2 m riding one that "
                       "does not, %3 m with the keypad setting")
                       .arg(taking, 0, 'f', 2).arg(notTaking, 0, 'f', 2).arg(keypadOn, 0, 'f', 2));
}

// The keys' places: from Qt's keys as a US keyboard has them, from Linux's
// and Windows' codes, names both ways.
void checkPositions(TokenTest::Suite &test) {
    using namespace KeyPosition;
    test.check(ofKey(Qt::Key_A) == 0x1E && ofKey(Qt::Key_Semicolon) == 0x27 && ofKey(Qt::Key_Colon) == 0x27
                       && ofKey(Qt::Key_QuoteDbl) == 0x28 && ofKey(Qt::Key_Question) == 0x35
                       && ofKey(Qt::Key_Insert) == 0x152 && ofKey(Qt::Key_Space) == 0x39 && ofKey(Qt::Key_Shift) == 0,
               "Qt's keys at a US keyboard's places, shifted characters at their key's (: at ;)");
    // A German keyboard's Z is where a US keyboard has Y: its place is Y's.
    test.check(ofNative(21 + 8, "xcb") == 0x15 && ofNative(30 + 8, "wayland") == 0x1E && ofNative(0x1E, "windows") == 0x1E
                       && ofNative(102 + 8, "xcb") == 0 && ofNative(30, "offscreen") == 0 && ofNative(0, "xcb") == 0,
               "Linux's XKB keycodes (evdev + 8) and Windows' scan codes give the main block's places; the "
               "others none");
    const QKeyEvent colon(QEvent::KeyPress, Qt::Key_Colon, Qt::ShiftModifier | Qt::KeypadModifier);
    const QKeyEvent digit(QEvent::KeyPress, Qt::Key_1, Qt::KeypadModifier);
    test.check(of(colon) == 0x27 && modifiers(colon.modifiers()) == Qt::ShiftModifier && of(digit) == 0x02
                       && of(QKeyEvent(QEvent::KeyPress, Qt::Key_Enter, Qt::KeypadModifier)) == 0x11C,
               "a key event's place and modifiers (Shift+; arriving as :, the keypad's mark dropped)");
    const Stroke ctrlShiftP = parse("Ctrl+Shift+P"), shiftCtrlP = parse("shift+control+p");
    test.check(ctrlShiftP.position == 0x19 && ctrlShiftP.modifiers == (Qt::ControlModifier | Qt::ShiftModifier)
                       && ctrlShiftP == shiftCtrlP && name(ctrlShiftP) == "Ctrl+Shift+P"
                       && name(parse("Ctrl+Alt+H")) == "Ctrl+Alt+H" && name(parse("'")) == "'"
                       && !parse("Hyper+P").valid() && !parse("Ctrl+").valid() && !parse("NoKey").valid()
                       && name(parse("Insert")) == "Insert",
               "names read in any order and case, written as Ctrl+Alt+Shift+key; unknown ones refused");
}

// The driven train's keys: Open Rails' defaults, each key's commands, held
// keys' releases, auto-repeats, letting go.
void checkDrivingKeys(TokenTest::Suite &test) {
    Sim::DrivingKeys keys;
    QStringList sent;
    int pauses = 0;
    keys.sink.command = [&](const QString &name, const Sim::MessageValue &value) {
        sent << name + "=" + Sim::Message{name, value}.text();
    };
    keys.sink.pause = [&] { ++pauses; };

    std::set<QString> strokes;
    bool named = true;
    for (const Sim::DrivingKeys::Binding &b : keys.bindings()) {
        strokes.insert(KeyPosition::name(b.stroke));
        named = named && !KeyPosition::name(b.stroke).isEmpty();
    }
    test.check(keys.bindings().size() == 52 && strokes.size() == keys.bindings().size() && named,
               QString("%1 bindings, each on a key of its own").arg(keys.bindings().size()));

    const auto pressed = [&](const QKeyEvent &e) { sent.clear(); const bool used = keys.keyPress(e); return used ? sent.join(' ') : QString("unused"); };
    const auto released = [&](const QKeyEvent &e) { sent.clear(); const bool used = keys.keyRelease(e); return used ? sent.join(' ') : QString("unused"); };

    const QString dDown = pressed(press(Qt::Key_D)), dRepeat = pressed(press(Qt::Key_D, Qt::NoModifier, true));
    const QString dRepeatUp = released(release(Qt::Key_D, Qt::NoModifier, true)), dUp = released(release(Qt::Key_D));
    test.check(dDown == "throttleIncrease=true" && dRepeat.isEmpty() && dRepeatUp.isEmpty()
                       && dUp == "throttleIncrease=false",
               QString("D held: %1; its auto-repeats used, sending nothing; released: %2").arg(dDown, dUp));

    const QString rDown = pressed(press(Qt::Key_R, Qt::ShiftModifier)), rUp = released(release(Qt::Key_R));
    test.check(rDown == "firingRateDecrease=true" && rUp == "firingRateDecrease=false",
               "Shift+R held, Shift let go before R: R's release ends Shift+R's " + rUp);

    const QString o = pressed(press(Qt::Key_O)), oUp = released(release(Qt::Key_O));
    test.check(o == "circuitBreaker=close tractionCutOffRelay=close injector2=toggle"
                       && oUp == "circuitBreaker=released tractionCutOffRelay=released",
               "O, as Open Rails: " + o + "; released: " + oUp);

    const QString p = pressed(press(Qt::Key_P)), pUp = released(release(Qt::Key_P));
    const QString p4 = pressed(press(Qt::Key_P, Qt::ControlModifier | Qt::ShiftModifier));
    const QString h = pressed(press(Qt::Key_H, Qt::ShiftModifier)), ctrlA = pressed(press(Qt::Key_A, Qt::ControlModifier));
    const QString brake = pressed(press(Qt::Key_Apostrophe)), zeroBrake = pressed(press(Qt::Key_Semicolon, Qt::ControlModifier));
    const QString heat = pressed(press(Qt::Key_U, Qt::AltModifier));
    test.check(p == "pantograph1=toggle" && pUp.isEmpty() && p4 == "pantograph4=toggle" && h == "headlight=-"
                       && ctrlA == "throttle=0" && brake == "trainBrakeIncrease=true" && zeroBrake == "trainBrake=0"
                       && heat == "steamHeatIncrease=true",
               QString("P %1, Ctrl+Shift+P %2, Shift+H %3, Ctrl+A %4, ' %5, Ctrl+; %6, Alt+U %7")
                       .arg(p, p4, h, ctrlA, brake, zeroBrake, heat));

    // Still held: ', Alt+U. Let go: their releases.
    sent.clear();
    keys.releaseKeys();
    const QString letGo = sent.join(' ');
    const qsizetype letGoCount = sent.size();
    const QString after = released(release(Qt::Key_Apostrophe));
    test.check(letGo.contains("trainBrakeIncrease=false") && letGo.contains("steamHeatIncrease=false")
                       && letGoCount == 2 && after.isEmpty(),
               "let go: " + letGo + "; the release after sends nothing");

    const QString arrow = pressed(press(Qt::Key_Left)), unbound = pressed(press(Qt::Key_Z)), ctrlD = pressed(press(Qt::Key_D, Qt::ControlModifier));
    const QString pause = pressed(press(Qt::Key_Pause));
    test.check(arrow == "unused" && unbound == "unused" && ctrlD == "unused" && pause.isEmpty() && pauses == 1,
               "the arrows, Z (the alerter, later) and Ctrl+D unused (the camera's); Pause pauses or runs on");

    // A press without its release seen (the focus moved): ended first.
    pressed(press(Qt::Key_Space));
    const QString again = pressed(press(Qt::Key_Space));
    test.check(again == "horn=off horn=on", "Space pressed again without its release: " + again);
    keys.releaseKeys();
}

}

int TsreTests::runKeysSuite(bool verbose) {
    TokenTest::Suite test{"[tests:keys]", verbose};
    checkCamera(test);
    checkPositions(test);
    checkDrivingKeys(test);
    return test.finish();
}
