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
// paths, the consists) or typed. Its stops list has the stations along its
// path (those without a stop greyed: a time typed adds the stop, and the
// station where the timetable lacks it), then its stops not on its path
// (yellow) or at a station the route has no platform of (red); a stop's
// only time cleared removes it. Its train data (scripting task 02's fields,
// #tsre.<field>) and its own data (#tsre.extra.<key>, the keys every
// train's), edited each an undo step, between the information and the
// commands, with the train as the displays' examples show it.
class OrTrainProperties : public QWidget {
    Q_OBJECT
public:
    explicit OrTrainProperties(QWidget *parent = nullptr);
    // The set's train: its timetable and its place there (-1: none).
    void showTrain(Activity *set, int timetable, int train, const QStringList &problems);
    // Label and box for each: its name, path, consist, comment and file
    // (shown only), its start and commands; its problems under them.
    QWidget *info = nullptr;
    // The train's timetable's own data keys (#tsre.extra.<key>, every
    // train's): added, renamed, removed, each an undo step; what is wrong
    // with the key, else empty.
    QString addExtraKey(const QString &key);
    QString renameExtraKey(const QString &key, const QString &to);
    void removeExtraKey(const QString &key);

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
    QTimeEdit startTime;
    // The train's commands: #start's, #note, #dispose, the speed row.
    QLineEdit commandFields[4];
    // Its fields (origin's and destination's placeholders: its first and
    // last stops' stations, as the displays show them without), how the
    // examples show it, its own data (a key and its value a row) and the
    // keys' buttons (add, rename, remove).
    QLineEdit fieldBoxes[TrainFields::Count];
    QLabel shownAs;
    QTreeWidget extras;
    QToolButton keyButtons[3];
    // Its cars (task scripting 03): a line a vehicle as its consists list
    // them, its number (the consist's greyed) and tags (#tsre.cars).
    QTreeWidget carsTable;
    QTreeWidget stops;
    QLabel problems;
    bool filling = false;
    // A train is shown.
    bool shown() const;
    void startEdited();
    void commandsEdited(OrTimetable::TrainCommands which);
    void stopEdited(QTreeWidgetItem *item, int column);
    void pathEdited();
    void consistEdited();
    void fieldEdited(int field);
    void extraEdited(QTreeWidgetItem *item, int column);
    // A car's number or tags typed: the row written for all its cars (none
    // when all are the consists'), its count always the train's.
    void carEdited(QTreeWidgetItem *item, int column);
    void showShownAs();
    // The keys' buttons: the key asked for (letters made lower case as
    // typed), a removal with values confirmed; then the key chosen.
    void addKeyAsked();
    void renameKeyAsked();
    void removeKeyAsked();
    void chooseKey(const QString &key, bool edit);
    QString chosenKey() const;
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
