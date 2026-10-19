#include <tsre/gui/UnitSpinBox.h>

#include <QLineEdit>
#include <QRegularExpression>
#include <tsre/units/Units.h>

namespace {

// A unit written after the number ("4.985 m", "45deg"), not only digits.
bool hasUnit(const QString &text) {
    for (const QChar c : text)
        if (c.isLetter() || c == QChar(0x00b0))
            return true;
    return false;
}

}

UnitSpinBox::UnitSpinBox(Units::Quantity quantity, QWidget *parent) : QDoubleSpinBox(parent), measured(quantity) {
    minimumSi_ = Units::fromShown(minimum(), measured);
    maximumSi_ = Units::fromShown(maximum(), measured);
    connect(this, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double shown) {
        if (showing)
            return;
        // Typed with its unit: held as typed, not as the rounded value shown.
        const QString text = typed(lineEdit()->text());
        double exact = 0.0;
        if (hasUnit(text) && Units::parse(text, measured, exact))
            held = exact;
        else
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

QString UnitSpinBox::typed(const QString &text) const {
    QString bare = text;
    if (!suffix().isEmpty() && bare.endsWith(suffix()))
        bare.chop(suffix().size());
    return bare.trimmed();
}

QValidator::State UnitSpinBox::validate(QString &input, int &pos) const {
    (void)pos;
    const QString text = typed(input);
    if (text.isEmpty())
        return QValidator::Intermediate;
    double si = 0.0;
    if (Units::parse(text, measured, si))
        return QValidator::Acceptable;
    // A number, maybe with a unit still being typed ("4.98 k").
    static const QRegularExpression partial(R"(^[-+]?(\d+[.,]?\d*|[.,]\d*)?([eE][-+]?\d*)?\s*[^\d\s]*$)");
    return partial.match(text).hasMatch() ? QValidator::Intermediate : QValidator::Invalid;
}

double UnitSpinBox::valueFromText(const QString &text) const {
    double si = 0.0;
    return Units::parse(typed(text), measured, si) ? Units::toShown(si, measured) : value();
}
