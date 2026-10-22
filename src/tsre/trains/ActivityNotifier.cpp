#include <tsre/trains/ActivityNotifier.h>

ActivityNotifier *ActivityNotifier::instance() {
    static ActivityNotifier notifier;
    return &notifier;
}

void ActivityNotifier::setEditingLocked(bool on) {
    if (locked == on)
        return;
    locked = on;
    emit editingLockChanged(on);
}

void ActivityNotifier::setTimetables(bool on) {
    if (timetableKind == on)
        return;
    timetableKind = on;
    emit kindChanged(on);
}
