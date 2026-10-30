#pragma once

class QKeyEvent;

// What takes the view's keys from it (task driving 07): an object the
// camera rides and controls, a driven train's cab (GameObj::keyTaker).
// While the ridden object has one, the view gives it every key before its
// camera, and the editor's own keys and the window's shortcuts wait, as map
// mode keeps the 3D keys out.
class KeyTaker {
public:
    virtual ~KeyTaker() = default;
    // Pressed or released (auto-repeats too): whether it used the key. One
    // it did not use is the camera's.
    virtual bool keyPress(const QKeyEvent &event) = 0;
    virtual bool keyRelease(const QKeyEvent &event) = 0;
    // Lets go of what it holds, as if released: the view lost the focus, the
    // UI took the keys, the camera rides something else.
    virtual void releaseKeys() = 0;
};
