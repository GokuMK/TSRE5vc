#pragma once
#include <QDoubleSpinBox>
#include <tsre/units/Quantity.h>

// A spin box for a physical value (task core si-units): held in SI, shown
// in the units the editors show with the unit as its suffix, and shown again
// when those units change. The value given is kept as it is until the user
// changes it, so showing it in other units never changes it. Steps and
// decimals are in the unit shown (a step of 5: 5 km/h, or 5 mph). A value
// may be typed with any unit of its quantity ("4.985 m" where feet are
// shown): it is held as typed, exactly.
class UnitSpinBox : public QDoubleSpinBox {
    Q_OBJECT
public:
    // Quantity::Any: no unit, the value as it is (set one with setQuantity).
    explicit UnitSpinBox(Units::Quantity quantity = Units::Quantity::Any, QWidget *parent = nullptr);
    double si() const { return held; }
    void setSi(double value);
    void setRangeSi(double minimum, double maximum);
    double maximumSi() const { return maximumSi_; }
    void setMaximumSi(double maximum) { setRangeSi(minimumSi_, maximum); }
    Units::Quantity quantity() const { return measured; }
    void setQuantity(Units::Quantity quantity);
    QValidator::State validate(QString &input, int &pos) const override;
    double valueFromText(const QString &text) const override;

signals:
    // The user changed the value (not a change of the units shown).
    void siChanged(double value);

private:
    Units::Quantity measured;
    double held = 0.0;
    double minimumSi_ = 0.0;
    double maximumSi_ = 0.0;
    bool showing = false;
    void show();
    // The text without the suffix shown.
    QString typed(const QString &text) const;
};
