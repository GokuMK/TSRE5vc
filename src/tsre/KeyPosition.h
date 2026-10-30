#pragma once
#include <QString>
#include <Qt>

class QKeyEvent;

// A key's place on the keyboard (task driving 07), as Open Rails binds its
// keys: its scan code (set 1, a US keyboard's places), 0x100 marking the
// grey keys (Insert, Delete, Home, End, Page Up and Down, the arrows, Pause),
// so the same hand positions drive on any layout. Named by a US keyboard's
// keys: "Shift+P", "Ctrl+;", "Insert".
namespace KeyPosition {

// The pressed key's place: the keys that mean the same on every layout
// (Space, Enter, the F keys, the grey keys) from Qt's key; the others from
// the platform's code (Linux's XKB keycode, Windows' scan code), else from
// Qt's key as a US keyboard places it. 0 unknown.
int of(const QKeyEvent &event);
// From Qt's key as a US keyboard places it, a shifted character at its key's
// place (`:` at `;`); 0 unknown.
int ofKey(int key);
// From the platform's code (QGuiApplication::platformName's platform) for
// the main block's keys; 0 for others or unknown.
int ofNative(quint32 scanCode, const QString &platform);
// Shift, Ctrl and Alt, the others (the keypad's, Meta) dropped.
Qt::KeyboardModifiers modifiers(Qt::KeyboardModifiers modifiers);

// A key with its modifiers.
struct Stroke {
    int position = 0;
    Qt::KeyboardModifiers modifiers;
    bool valid() const { return position != 0; }
    bool operator==(const Stroke &other) const {
        return position == other.position && modifiers == other.modifiers;
    }
};
// "Ctrl+Shift+P" (modifiers in any order); invalid for an unknown name.
Stroke parse(const QString &text);
// The modifiers as Ctrl+Alt+Shift+, then the key's name; "" unknown.
QString name(const Stroke &stroke);
QString name(int position);

}
