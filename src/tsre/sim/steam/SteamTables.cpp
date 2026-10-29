#include <tsre/sim/steam/SteamTables.h>
#include <algorithm>
#include <map>
#include <string>

namespace Sim::Steam {

namespace {

struct Table1DData {
    const char *name;
    const double *x;
    const double *y;
    int size;
};

struct Table2DData {
    const char *name;
    const double *x;
    const char *const *rows;
    int size;
};

#include <tsre/sim/steam/SteamTablesData.inc>

// The segment x falls in, the first or last beyond the ends.
size_t segment(const std::vector<double> &xs, double x) {
    const size_t n = xs.size();
    if (x < xs[1])
        return 0;
    if (x > xs[n - 2])
        return n - 2;
    size_t lo = 0, hi = n - 1;
    while (hi - lo > 1) {
        const size_t k = (lo + hi) / 2;
        if (xs[k] > x)
            hi = k;
        else
            lo = k;
    }
    return lo;
}

struct Tables {
    std::map<std::string, Table, std::less<>> tables;
    std::map<std::string, Table2D, std::less<>> tables2D;

    Tables() {
        for (const Table1DData &t : Tables1D)
            tables.emplace(t.name, Table(std::vector<double>(t.x, t.x + t.size), std::vector<double>(t.y, t.y + t.size)));
        for (const Table2DData &t : Tables2D) {
            std::vector<Table> rows;
            for (int i = 0; i < t.size; ++i)
                rows.push_back(tables.at(t.rows[i]));
            tables2D.emplace(t.name, Table2D(std::vector<double>(t.x, t.x + t.size), std::move(rows)));
        }
    }
};

const Tables &all() {
    static const Tables tables;
    return tables;
}

}

Table::Table(std::vector<double> x, std::vector<double> y) : xs(std::move(x)), ys(std::move(y)) {}

double Table::operator()(double x) const {
    if (xs.empty())
        return 0.0;
    if (xs.size() == 1)
        return ys[0];
    const size_t i = segment(xs, x);
    const double d = xs[i + 1] - xs[i];
    if (d == 0.0)
        return ys[i];
    return ((xs[i + 1] - x) * ys[i] + (x - xs[i]) * ys[i + 1]) / d;
}

double Table::maxY() const {
    return ys.empty() ? 0.0 : *std::max_element(ys.begin(), ys.end());
}

double Table::minY() const {
    return ys.empty() ? 0.0 : *std::min_element(ys.begin(), ys.end());
}

Table2D::Table2D(std::vector<double> x, std::vector<Table> rows) : xs(std::move(x)), rows(std::move(rows)) {}

double Table2D::operator()(double x, double y) const {
    if (xs.empty())
        return 0.0;
    if (xs.size() == 1)
        return rows[0](y);
    const size_t i = segment(xs, x);
    const double d = xs[i + 1] - xs[i];
    const double a = (xs[i + 1] - x) / d, b = (x - xs[i]) / d;
    double z = 0.0;
    if (a != 0.0)
        z += a * rows[i](y);
    if (b != 0.0)
        z += b * rows[i + 1](y);
    return z;
}

const Table &table(std::string_view name) {
    static const Table none;
    const auto found = all().tables.find(name);
    return found == all().tables.end() ? none : found->second;
}

const Table2D &table2D(std::string_view name) {
    static const Table2D none;
    const auto found = all().tables2D.find(name);
    return found == all().tables2D.end() ? none : found->second;
}

}
