#pragma once
#include <QString>

// What undo keeps of an activity, service or traffic file: its text as its
// writer gives it, and whether and how often it had been saved, so that a
// state from before a save restores as unsaved.
struct TextSnapshot {
    QString text;
    bool unsaved = false;
    int saves = 0;
    // What else the object keeps until it is saved (an activity: the
    // overlay merged into it, renamed when it is saved).
    QString pending;
};
