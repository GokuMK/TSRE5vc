#pragma once
#include <QObject>

// Undo replaces activity data (activities, services, traffic) with what it
// kept: the windows let go of the events, objects and instances they show
// before (restoring), and show them again after (restored).
class ActivityNotifier : public QObject {
    Q_OBJECT
public:
    static ActivityNotifier *instance();
    // While a simulation exists the activity data is not edited (task
    // simulation 01): the windows and tools show it but do not change it,
    // and undo leaves activity steps alone.
    static bool editingLocked() { return instance()->locked; }
    void setEditingLocked(bool on);
    // The kind of activity the activity panel shows (task simulation 08):
    // MSTS activities or Open Rails timetables; the activity windows follow
    // it.
    static bool timetables() { return instance()->timetableKind; }
    void setTimetables(bool on);
signals:
    void restoring();
    void restored();
    // A path edit moved, removed or kept the stops of services on it.
    void pathFollowed(const QString &path, const QStringList &lines);
    // Saving an activity renamed the overlay merged into it.
    void overlayRenamed(const QString &path);
    void editingLockChanged(bool locked);
    void kindChanged(bool timetables);
private:
    bool locked = false;
    bool timetableKind = false;
};
