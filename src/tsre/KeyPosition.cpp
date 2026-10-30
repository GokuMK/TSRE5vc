#include <tsre/KeyPosition.h>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QStringList>

namespace KeyPosition {

namespace {

// The grey keys' mark.
constexpr int Grey = 0x100;

// A US keyboard's keys: the place, its name, Qt's keys there (unshifted,
// shifted).
struct Place {
    int position;
    const char *name;
    int key, shifted;
};

const Place Places[] = {
    {0x01, "Esc", Qt::Key_Escape, 0},
    {0x02, "1", Qt::Key_1, Qt::Key_Exclam},
    {0x03, "2", Qt::Key_2, Qt::Key_At},
    {0x04, "3", Qt::Key_3, Qt::Key_NumberSign},
    {0x05, "4", Qt::Key_4, Qt::Key_Dollar},
    {0x06, "5", Qt::Key_5, Qt::Key_Percent},
    {0x07, "6", Qt::Key_6, Qt::Key_AsciiCircum},
    {0x08, "7", Qt::Key_7, Qt::Key_Ampersand},
    {0x09, "8", Qt::Key_8, Qt::Key_Asterisk},
    {0x0A, "9", Qt::Key_9, Qt::Key_ParenLeft},
    {0x0B, "0", Qt::Key_0, Qt::Key_ParenRight},
    {0x0C, "-", Qt::Key_Minus, Qt::Key_Underscore},
    {0x0D, "=", Qt::Key_Equal, Qt::Key_Plus},
    {0x0E, "Backspace", Qt::Key_Backspace, 0},
    {0x0F, "Tab", Qt::Key_Tab, Qt::Key_Backtab},
    {0x10, "Q", Qt::Key_Q, 0},
    {0x11, "W", Qt::Key_W, 0},
    {0x12, "E", Qt::Key_E, 0},
    {0x13, "R", Qt::Key_R, 0},
    {0x14, "T", Qt::Key_T, 0},
    {0x15, "Y", Qt::Key_Y, 0},
    {0x16, "U", Qt::Key_U, 0},
    {0x17, "I", Qt::Key_I, 0},
    {0x18, "O", Qt::Key_O, 0},
    {0x19, "P", Qt::Key_P, 0},
    {0x1A, "[", Qt::Key_BracketLeft, Qt::Key_BraceLeft},
    {0x1B, "]", Qt::Key_BracketRight, Qt::Key_BraceRight},
    {0x1C, "Enter", Qt::Key_Return, 0},
    {0x1E, "A", Qt::Key_A, 0},
    {0x1F, "S", Qt::Key_S, 0},
    {0x20, "D", Qt::Key_D, 0},
    {0x21, "F", Qt::Key_F, 0},
    {0x22, "G", Qt::Key_G, 0},
    {0x23, "H", Qt::Key_H, 0},
    {0x24, "J", Qt::Key_J, 0},
    {0x25, "K", Qt::Key_K, 0},
    {0x26, "L", Qt::Key_L, 0},
    {0x27, ";", Qt::Key_Semicolon, Qt::Key_Colon},
    {0x28, "'", Qt::Key_Apostrophe, Qt::Key_QuoteDbl},
    {0x29, "`", Qt::Key_QuoteLeft, Qt::Key_AsciiTilde},
    {0x2B, "\\", Qt::Key_Backslash, Qt::Key_Bar},
    {0x2C, "Z", Qt::Key_Z, 0},
    {0x2D, "X", Qt::Key_X, 0},
    {0x2E, "C", Qt::Key_C, 0},
    {0x2F, "V", Qt::Key_V, 0},
    {0x30, "B", Qt::Key_B, 0},
    {0x31, "N", Qt::Key_N, 0},
    {0x32, "M", Qt::Key_M, 0},
    {0x33, ",", Qt::Key_Comma, Qt::Key_Less},
    {0x34, ".", Qt::Key_Period, Qt::Key_Greater},
    {0x35, "/", Qt::Key_Slash, Qt::Key_Question},
    {0x39, "Space", Qt::Key_Space, 0},
    {0x3B, "F1", Qt::Key_F1, 0},
    {0x3C, "F2", Qt::Key_F2, 0},
    {0x3D, "F3", Qt::Key_F3, 0},
    {0x3E, "F4", Qt::Key_F4, 0},
    {0x3F, "F5", Qt::Key_F5, 0},
    {0x40, "F6", Qt::Key_F6, 0},
    {0x41, "F7", Qt::Key_F7, 0},
    {0x42, "F8", Qt::Key_F8, 0},
    {0x43, "F9", Qt::Key_F9, 0},
    {0x44, "F10", Qt::Key_F10, 0},
    {0x57, "F11", Qt::Key_F11, 0},
    {0x58, "F12", Qt::Key_F12, 0},
    {Grey | 0x1C, "KeypadEnter", Qt::Key_Enter, 0},
    {Grey | 0x45, "Pause", Qt::Key_Pause, 0},
    {Grey | 0x47, "Home", Qt::Key_Home, 0},
    {Grey | 0x48, "Up", Qt::Key_Up, 0},
    {Grey | 0x49, "PageUp", Qt::Key_PageUp, 0},
    {Grey | 0x4B, "Left", Qt::Key_Left, 0},
    {Grey | 0x4D, "Right", Qt::Key_Right, 0},
    {Grey | 0x4F, "End", Qt::Key_End, 0},
    {Grey | 0x50, "Down", Qt::Key_Down, 0},
    {Grey | 0x51, "PageDown", Qt::Key_PageDown, 0},
    {Grey | 0x52, "Insert", Qt::Key_Insert, 0},
    {Grey | 0x53, "Delete", Qt::Key_Delete, 0},
};

const Place *placeOf(int position) {
    for (const Place &p : Places)
        if (p.position == position)
            return &p;
    return nullptr;
}

// Keys that mean the same on every layout: found by Qt's key.
bool fixedKey(int key) {
    switch (key) {
    case Qt::Key_Escape: case Qt::Key_Backspace: case Qt::Key_Tab: case Qt::Key_Backtab:
    case Qt::Key_Return: case Qt::Key_Enter: case Qt::Key_Space: case Qt::Key_Pause:
    case Qt::Key_Home: case Qt::Key_End: case Qt::Key_PageUp: case Qt::Key_PageDown:
    case Qt::Key_Up: case Qt::Key_Down: case Qt::Key_Left: case Qt::Key_Right:
    case Qt::Key_Insert: case Qt::Key_Delete:
        return true;
    default:
        return key >= Qt::Key_F1 && key <= Qt::Key_F12;
    }
}

}

int of(const QKeyEvent &event) {
    const int key = event.key();
    // The keypad's keys (its digits, its Enter) have places of their own,
    // none bound by name.
    if (fixedKey(key) && !(event.modifiers() & Qt::KeypadModifier))
        return ofKey(key);
    static const QString platform = QGuiApplication::platformName();
    if (const int native = ofNative(event.nativeScanCode(), platform))
        return native;
    return ofKey(key);
}

int ofKey(int key) {
    for (const Place &p : Places)
        if (p.key == key || (p.shifted != 0 && p.shifted == key))
            return p.position;
    return 0;
}

int ofNative(quint32 scanCode, const QString &platform) {
    // The main block's keys up to F12, where Linux's evdev codes are set 1's
    // (XKB's keycode the evdev code + 8) and Windows gives set 1 itself
    // (0x100 marking extended keys).
    quint32 code = 0;
    if (platform == QLatin1String("xcb") || platform.startsWith(QLatin1String("wayland")))
        code = scanCode > 8 ? scanCode - 8 : 0;
    else if (platform == QLatin1String("windows"))
        code = scanCode;
    return code > 0 && code <= 0x58 ? int(code) : 0;
}

Qt::KeyboardModifiers modifiers(Qt::KeyboardModifiers modifiers) {
    return modifiers & (Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier);
}

Stroke parse(const QString &text) {
    Stroke stroke;
    const QStringList parts = text.trimmed().split('+');
    if (parts.isEmpty())
        return stroke;
    for (int i = 0; i + 1 < parts.size(); ++i) {
        const QString part = parts[i].trimmed().toLower();
        if (part == "ctrl" || part == "control")
            stroke.modifiers |= Qt::ControlModifier;
        else if (part == "alt")
            stroke.modifiers |= Qt::AltModifier;
        else if (part == "shift")
            stroke.modifiers |= Qt::ShiftModifier;
        else
            return Stroke();
    }
    const QString key = parts.last().trimmed();
    for (const Place &p : Places)
        if (key.compare(QLatin1String(p.name), Qt::CaseInsensitive) == 0) {
            stroke.position = p.position;
            return stroke;
        }
    return Stroke();
}

QString name(int position) {
    const Place *p = placeOf(position);
    return p != nullptr ? QString::fromLatin1(p->name) : QString();
}

QString name(const Stroke &stroke) {
    const QString key = name(stroke.position);
    if (key.isEmpty())
        return key;
    QString text;
    if (stroke.modifiers & Qt::ControlModifier)
        text += "Ctrl+";
    if (stroke.modifiers & Qt::AltModifier)
        text += "Alt+";
    if (stroke.modifiers & Qt::ShiftModifier)
        text += "Shift+";
    return text + key;
}

}
