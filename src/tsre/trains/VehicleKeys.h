#pragma once
#include <QAnyStringView>
#include <QString>
#include <QStringList>
#include <QVector>
#include <tsre/units/Quantity.h>

// The key catalogue of ENG/WAG files (task driving 01): each path Open Rails
// reads ("engine(maxforce"), how it reads it and each value's quantity and
// default unit, so every value reaches Lua and C++ in SI. Made from Open
// Rails' parse code by scripts/vehicle-keys-from-openrails.py
// (VehicleKeysTable.inc); a key it does not list is read as written.
namespace VehicleKeys {

enum class Read {
    // Values in the block, in order (values).
    Values,
    // A block of its own: curves, controllers, subsystems.
    Block,
    // Only being there counts (PassengerCapacity), or read some other way.
    Present
};

struct Value {
    enum class Kind { Number, Integer, Bool, Text };
    Kind kind = Kind::Text;
    // A number's quantity, and the unit of a number written without one
    // (empty: SI); Any for Open Rails' unitless numbers.
    Units::Quantity quantity = Units::Quantity::Any;
    QString defaultUnit;
};

struct Key {
    QString path;
    Read read = Read::Values;
    QVector<Value> values;
    // The Open Rails class reading it.
    QString source;
};

// The key at a path (any case), null for one Open Rails does not read.
const Key *find(QAnyStringView path);
// Every key, by path.
const QVector<Key> &all();
// Open Rails' unit names in the table that have no quantity here (for the
// tests: none).
QStringList unmappedUnits();
// The value at `index` of a key at `path`; for the values past the last
// listed, the last (a curve's values all alike); a Text value for none.
Value value(QAnyStringView path, int index);

}
