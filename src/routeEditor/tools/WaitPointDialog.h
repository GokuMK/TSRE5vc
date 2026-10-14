#pragma once
#include <QComboBox>
#include <QDialog>
#include <QLabel>
#include <QSpinBox>
#include <QTimeEdit>

// A path's waiting point: MSTS's wait in seconds, or one of Open Rails'
// kinds (until a time, uncouple, horn), as its value (WaitingPoint).
class WaitPointDialog : public QDialog {
    Q_OBJECT
public:
    WaitPointDialog(QWidget *parent, int value);
    int value() const;
    // The value asked for; ok false when cancelled.
    static int ask(QWidget *parent, int value, bool &ok);

private:
    QComboBox kind;
    QSpinBox seconds;
    QTimeEdit until;
    QSpinBox cars;
    QLabel shown;
    QLabel secondsLabel, untilLabel, carsLabel;
    void update();
};
