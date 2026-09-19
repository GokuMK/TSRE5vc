#include <tsre/geo/ElevationRaster.h>
#include <functional>
#include <cmath>

void runNoDataFillTests(const std::function<void(bool,const char*)> &check) {
    using namespace Elevation;
    std::atomic_bool cancel{false};
    const float nan = std::numeric_limits<float>::quiet_NaN();
    Raster r;
    r.width = 3; r.height = 3;
    r.values = {-2,0,2, -2,nan,2, -2,0,2};
    const auto before = r.values;
    check(fillNoData(r,false,cancel) == 1 && r.values[4] == 0,
        "fill averages adjacent original heights and preserves legitimate zero/negative heights");
    bool unchanged = true;
    for (int i=0; i<9; ++i) if (i != 4) unchanged &= r.values[i] == before[i];
    check(unchanged,"fill never changes original measurements");
    r.width = 7; r.height = 1;
    r.values = {2,nan,nan,nan,nan,nan,8};
    check(fillNoData(r,false,cancel) == 5 && r.values == QVector<float>({2,2,2,5,8,8,8}),
        "multi-pixel holes fill in simultaneous layers without scan-order bias");
    r.width = 3; r.values = {5,0,-9999}; r.hasNoData = true; r.noData = -9999;
    check(fillNoData(r,true,cancel) == 2 && r.values == QVector<float>({5,5,5}),
        "explicit sentinel and configured zero voids both fill");
    r.width = 4; r.values = {7,nan,nan,nan};
    QBitArray available(4,true); available.clearBit(2);
    check(fillNoData(r,false,cancel,available) == 1 && r.values[1] == 7
        && std::isnan(r.values[2]) && std::isnan(r.values[3]),
        "unavailable cells block propagation and disconnected empty areas remain NoData");
    r.width = 3; r.values = {nan,nan,nan};
    check(fillNoData(r,false,cancel) == 0 && std::isnan(r.values[1]),
        "an entirely empty raster invents no height");
    r.values = {5,nan,5}; cancel = true;
    check(fillNoData(r,false,cancel) == 0 && std::isnan(r.values[1]),"NoData fill respects cancellation");
}
