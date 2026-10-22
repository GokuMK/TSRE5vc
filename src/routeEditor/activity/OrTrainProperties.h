#pragma once
#include <QWidget>
#include <QtWidgets>
#include <tsre/trains/OrTimetable.h>

class Activity;

// An Open Rails timetable's train in the Timetable window (task simulation
// 08): its start, path, consist and commands, its stops and their times,
// its problems. Its start time and its stops' times are edited (phase 6),
// and its commands, its stops' and their stations' (phase 8: unsupported
// ones kept and shown in orange), each an undo step; the timetable is then
// unsaved (Save Activities writes it in TSRE's layout). The train's
// information, commands and problems are in info, which the window puts
// beside the stops.
class OrTrainProperties : public QWidget {
    Q_OBJECT
public:
    explicit OrTrainProperties(QWidget *parent = nullptr);
    // The set's train: its timetable and its place there (-1: none).
    void showTrain(Activity *set, int timetable, int train, const QStringList &problems);
    // Label and box for each: its name, path, consist, comment and file
    // (shown only), its start and commands; its problems under them.
    QWidget *info = nullptr;

signals:
    void edited();

private:
    Activity *set = nullptr;
    int timetable = -1;
    int train = -1;
    // The train's name, path, consist, comment and timetable file.
    QLineEdit shownFields[5];
    QTimeEdit startTime;
    // The train's commands: #start's, #note, #dispose, the speed row.
    QLineEdit commandFields[4];
    QTreeWidget stops;
    QLabel problems;
    bool filling = false;
    void startEdited();
    void commandsEdited(OrTimetable::TrainCommands which);
    void stopEdited(QTreeWidgetItem *item, int column);
    // After the field's or item's own signal: the window fills the panel
    // again.
    void editedLater();
};
