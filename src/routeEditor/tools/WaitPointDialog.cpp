#include "WaitPointDialog.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <tsre/trains/PathTrack.h>
#include <tsre/trains/WaitingPoint.h>

WaitPointDialog::WaitPointDialog(QWidget *parent, int value) : QDialog(parent) {
    setWindowTitle(
        //% "Wait Point"
        qtTrId("route.editor.path.tool.wait.title"));
    kind.addItem(
        //% "Wait"
        qtTrId("route.editor.path.tool.wait.kind.seconds"), WaitingPoint::Seconds);
    kind.addItem(
        //% "Wait until a time (Open Rails)"
        qtTrId("route.editor.path.tool.wait.kind.until"), WaitingPoint::UntilTime);
    kind.addItem(
        //% "Uncouple, keep the first cars (Open Rails)"
        qtTrId("route.editor.path.tool.wait.kind.keep.first"), WaitingPoint::KeepFirst);
    kind.addItem(
        //% "Uncouple, keep the last cars (Open Rails)"
        qtTrId("route.editor.path.tool.wait.kind.keep.last"), WaitingPoint::KeepLast);
    kind.addItem(
        //% "Sound the horn, no stop (Open Rails)"
        qtTrId("route.editor.path.tool.wait.kind.horn"), WaitingPoint::Horn);
    kind.addItem(
        //% "Level crossing horn, no stop (Open Rails)"
        qtTrId("route.editor.path.tool.wait.kind.crossing.horn"), WaitingPoint::CrossingHorn);
    seconds.setSuffix(" s");
    until.setDisplayFormat("HH:mm");
    cars.setRange(0, 99);
    untilLabel.setText(
        //% "Until:"
        qtTrId("route.editor.path.tool.wait.until"));
    carsLabel.setText(
        //% "Cars kept:"
        qtTrId("route.editor.path.tool.wait.cars"));
    QFormLayout *form = new QFormLayout(this);
    form->addRow(&kind);
    form->addRow(&secondsLabel, &seconds);
    form->addRow(&untilLabel, &until);
    form->addRow(&carsLabel, &cars);
    form->addRow(&shown);
    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form->addRow(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    const WaitingPoint::Point point = WaitingPoint::decode(value);
    kind.setCurrentIndex(kind.findData(point.kind));
    seconds.setRange(0, 29999);
    seconds.setValue(point.seconds);
    until.setTime(QTime(point.hour, point.minute));
    cars.setValue(point.cars);
    QObject::connect(&kind, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] { update(); });
    QObject::connect(&seconds, QOverload<int>::of(&QSpinBox::valueChanged), this, [this] { update(); });
    QObject::connect(&until, &QTimeEdit::timeChanged, this, [this] { update(); });
    QObject::connect(&cars, QOverload<int>::of(&QSpinBox::valueChanged), this, [this] { update(); });
    update();
}

int WaitPointDialog::value() const {
    WaitingPoint::Point point;
    point.kind = WaitingPoint::Kind(kind.currentData().toInt());
    point.seconds = seconds.value();
    point.hour = until.time().hour();
    point.minute = until.time().minute();
    point.cars = cars.value();
    return WaitingPoint::encode(point);
}

// Only the fields of the kind; the value written shown.
void WaitPointDialog::update() {
    const WaitingPoint::Kind chosen = WaitingPoint::Kind(kind.currentData().toInt());
    const bool timed = chosen == WaitingPoint::Seconds || chosen == WaitingPoint::KeepFirst
            || chosen == WaitingPoint::KeepLast || chosen == WaitingPoint::Horn;
    const int maximum = chosen == WaitingPoint::Seconds ? 29999 : chosen == WaitingPoint::Horn ? 10 : 99;
    if (seconds.maximum() != maximum)
        seconds.setRange(chosen == WaitingPoint::Horn ? 1 : 0, maximum);
    seconds.setVisible(timed);
    secondsLabel.setVisible(timed);
    secondsLabel.setText(chosen == WaitingPoint::Horn ?
        //% "Horn (seconds):"
        qtTrId("route.editor.path.tool.wait.horn.seconds") :
        //% "Wait (seconds):"
        qtTrId("route.editor.path.tool.wait.seconds"));
    until.setVisible(chosen == WaitingPoint::UntilTime);
    untilLabel.setVisible(chosen == WaitingPoint::UntilTime);
    cars.setVisible(chosen == WaitingPoint::KeepFirst || chosen == WaitingPoint::KeepLast);
    carsLabel.setVisible(cars.isVisible());
    shown.setText(QString("%1: %2").arg(value()).arg(WaitingPoint::describe(value())));
}

int WaitPointDialog::ask(QWidget *parent, int value, bool &ok) {
    WaitPointDialog dialog(parent, value);
    ok = dialog.exec() == QDialog::Accepted;
    return ok ? dialog.value() : value;
}
