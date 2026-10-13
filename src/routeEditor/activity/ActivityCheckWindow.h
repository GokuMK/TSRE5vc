#pragma once
#include <QLabel>
#include <QTreeWidget>
#include <QVector>
#include <QWidget>
#include <tsre/trains/ActivityCheck.h>

class Activity;

// Check Activity's findings for an activity, errors first; choosing one
// asks for what it names to be shown.
class ActivityCheckWindow : public QWidget {
    Q_OBJECT
public:
    explicit ActivityCheckWindow(QWidget *parent);
    // Checks the activity again and lists what is found.
    void check(Activity *activity);
    Activity *checkedActivity() const { return activity; }

signals:
    void findingChosen(const ActivityFinding &finding);

private:
    Activity *activity = nullptr;
    QVector<ActivityFinding> findings;
    QTreeWidget list;
    QLabel summary;
};
