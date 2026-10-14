#pragma once
#include <QObject>

// Undo replaces activity data (activities, services, traffic) with what it
// kept: the windows let go of the events, objects and instances they show
// before (restoring), and show them again after (restored).
class ActivityNotifier : public QObject {
    Q_OBJECT
public:
    static ActivityNotifier *instance();
signals:
    void restoring();
    void restored();
    // A path edit moved, removed or kept the stops of services on it.
    void pathFollowed(const QString &path, const QStringList &lines);
    // Saving an activity renamed the overlay merged into it.
    void overlayRenamed(const QString &path);
};
