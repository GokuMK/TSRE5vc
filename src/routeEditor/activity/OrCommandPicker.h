#pragma once
#include <QDialog>
#include <QtWidgets>
#include <tsre/trains/OrTimetableCommands.h>

// The command picker of the Timetable window (task simulation 08): the
// commands Open Rails takes at one place (a train's #start, #note,
// #dispose, speed row, a stop, a station's row) listed by kind with what
// each does; ticked, with their values and qualifiers, they make the
// commands' text. Those already written start ticked and filled; ones it
// does not know, or a second of a kind, are kept as typed. What TSRE does
// not run yet is in orange. Opened by the "…" of the train's command
// fields and of its stops' command cells; OK gives text() to the field.
class OrCommandPicker : public QDialog {
    Q_OBJECT
public:
    // `place` an OrCommandSpec::Place; `trains` and `paths` offered for
    // the values that name them; `speedUnit` the speed row's ("km/h").
    OrCommandPicker(int place, const QString &title, const QString &text, const QStringList &trains,
                    const QStringList &paths, const QString &speedUnit, QWidget *parent = nullptr);
    // The commands as chosen, as a field takes them ("$hold $follow=T2").
    QString text() const;
    // The "…" of the fields and cells that open it.
    static QIcon Icon(const QWidget *widget);

private:
    struct Qualifier {
        QString name;
        QString value;
    };
    struct Choice {
        // None: a command kept as typed.
        const OrCommandSpec *spec = nullptr;
        bool on = false;
        // Its place among the commands written (new ones after them, as
        // ticked).
        int order = -1;
        QString value;
        // The qualifiers written, in order.
        QVector<Qualifier> qualifiers;
        // Kept as typed (without its '$').
        QString typed;
        QString text() const;
    };
    QVector<Choice> choices;
    int nextOrder = 0;
    QStringList trainNames;
    QStringList pathNames;
    QString speedUnit;
    QTreeWidget list;
    QScrollArea detailsArea;
    QLineEdit result;
    // Fills the details of a choice (index into choices, -1 none).
    void showDetails(int choice);
    QWidget *valueField(OrCommandSpec::Value kind, const QString &hint, const QString &value,
                        const std::function<void(const QString &)> &changed);
    void setOn(int choice, bool on);
    void showResult();
    QTreeWidgetItem *itemOf(int choice);
};
