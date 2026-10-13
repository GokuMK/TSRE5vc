#pragma once

class Activity;
class Service;
class Traffic;

// An edit of activity data from the activity windows, made while one is
// alive: an undo step that keeps the activity, service or traffic as it was
// before. Typing in a field continues the step the keys before it opened
// (undo closes it after a pause), as world edits do.
class ActivityEdit {
public:
    explicit ActivityEdit(Activity *activity, bool typing = false);
    explicit ActivityEdit(Service *service, bool typing = false);
    explicit ActivityEdit(Traffic *traffic, bool typing = false);
    ~ActivityEdit();
    // More files the same edit changes.
    void also(Activity *activity);
    void also(Service *service);
    void also(Traffic *traffic);
    // The activities using the service (its path and stops reach their
    // timetables).
    void alsoUsersOf(Service *service);
    // The traffic file the activity names.
    void alsoTrafficOf(Activity *activity);
    // Nothing changed after all: no step (not for typing).
    void cancel();
    ActivityEdit(const ActivityEdit &) = delete;
    ActivityEdit &operator=(const ActivityEdit &) = delete;
private:
    bool typing;
    bool cancelled = false;
    void begin();
};
