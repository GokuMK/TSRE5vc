#pragma once
#include <string_view>
#include <vector>

// The steam loco's native module (task driving 04). It works inside in Open
// Rails' units (psi, lb, ft, °F, BTU), so its formulas and numbers compare
// with Open Rails' line by line, and gives and takes SI at its edge.
namespace Sim::Steam {

// A table as Open Rails' Interpolator has it: linear between its points and
// beyond its ends.
class Table {
public:
    Table() = default;
    Table(std::vector<double> x, std::vector<double> y);
    double operator()(double x) const;
    bool empty() const { return xs.empty(); }
    const std::vector<double> &x() const { return xs; }
    const std::vector<double> &y() const { return ys; }
    double maxY() const;
    double minY() const;

private:
    std::vector<double> xs, ys;
};

// Rows of tables along x (Open Rails' Interpolator2D): linear between the
// rows, and beyond the first and last, each row read at y.
class Table2D {
public:
    Table2D() = default;
    Table2D(std::vector<double> x, std::vector<Table> rows);
    double operator()(double x, double y) const;
    bool empty() const { return xs.empty(); }

private:
    std::vector<double> xs;
    std::vector<Table> rows;
};

// Open Rails' steam tables (SteamTable.cs, scripts/steam-tables-from-
// openrails.py), by the names Open Rails gives them, in its units; an empty
// table for a name it has none of.
const Table &table(std::string_view name);
const Table2D &table2D(std::string_view name);

}
