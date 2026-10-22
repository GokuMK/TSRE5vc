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
// beside the stops. The "…" of a command field or cell opens the command
// picker (OrCommandPicker). Its path and consist are chosen (the route's
// paths, the consists) or typed; with All stations its stops list has
// every station, a time typed at one without a stop adds it; a stop's only
// time cleared removes it.
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
    // The train's name, comment and timetable file.
    QLineEdit shownFields[3];
    QComboBox pathBox;
    QComboBox consistBox;
    QCheckBox allStations;
    // The problems shown (All stations shows the train again).
    QStringList shownProblems;
    QTimeEdit startTime;
    // The train's commands: #start's, #note, #dispose, the speed row.
    QLineEdit commandFields[4];
    QTreeWidget stops;
    QLabel problems;
    bool filling = false;
    void startEdited();
    void commandsEdited(OrTimetable::TrainCommands which);
    void stopEdited(QTreeWidgetItem *item, int column);
    void pathEdited();
    void consistEdited();
    // The picker for a command field (its index) or a stop's command cell
    // (the stop's place in the train, its column); OK sets the field or
    // cell as typing does.
    void pickTrainCommands(int field);
    void pickStopCommands(int stop, int station, int column);
    void openPicker(int place, const QString &title, const QString &text,
                    const std::function<void(const QString &)> &apply);
    // After the field's or item's own signal: the window fills the panel
    // again.
    void editedLater();
};
