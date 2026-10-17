#include <tsre/gui/UnitSpinBox.h>

#include <tsre/units/Units.h>

UnitSpinBox::UnitSpinBox(Units::Quantity quantity, QWidget *parent) : QDoubleSpinBox(parent), measured(quantity) {
    minimumSi_ = Units::fromShown(minimum(), measured);
    maximumSi_ = Units::fromShown(maximum(), measured);
    connect(this, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double shown) {
        if (showing)
            return;
        held = Units::fromShown(shown, measured);
        emit siChanged(held);
    });
    connect(Units::Notifier::instance(), &Units::Notifier::changed, this, &UnitSpinBox::show);
    show();
}

void UnitSpinBox::setSi(double value) {
    held = value;
    show();
}

void UnitSpinBox::setRangeSi(double minimum, double maximum) {
    minimumSi_ = minimum;
    maximumSi_ = maximum;
    show();
}

void UnitSpinBox::setQuantity(Units::Quantity quantity) {
    measured = quantity;
    show();
}

// The suffix, the range and the value in the units shown now; the value
// held stays as it is.
void UnitSpinBox::show() {
    showing = true;
    const QSignalBlocker blocker(this);
    const QString unit = Units::unit(measured);
    // An angle's degrees stick to the number, as Units::format writes them.
    setSuffix(unit.isEmpty() ? QString() : unit == QString(QChar(0x00b0)) ? unit : " " + unit);
    const double low = Units::toShown(minimumSi_, measured), high = Units::toShown(maximumSi_, measured);
    setRange(std::min(low, high), std::max(low, high));
    setValue(Units::toShown(held, measured));
    showing = false;
}
