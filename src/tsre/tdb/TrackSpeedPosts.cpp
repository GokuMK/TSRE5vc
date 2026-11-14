#include <tsre/tdb/TrackSpeedPosts.h>

#include <cmath>
#include <tsre/tdb/TRitem.h>

namespace TrackSpeedPosts {

namespace {
double wrap(double angle) {
    while (angle > M_PI)
        angle -= 2.0 * M_PI;
    while (angle < -M_PI)
        angle += 2.0 * M_PI;
    return angle;
}
}

bool read(TRitem &item, float trackYaw, Post &out) {
    if (item.type != "speedpostitem" || item.speedpostTrItemData == nullptr)
        return false;
    const TRitem::SType type = item.getSpeedpostType();
    if (type == TRitem::MILEPOST)
        return false;
    out.kind = type == TRitem::RESUME ? Post::Resume : type == TRitem::WARNING ? Post::Warning : Post::Limit;
    const int flags = int(item.speedpostTrItemData[0]);
    const bool both = (flags >> 7) & 1;
    out.passenger = both || ((flags >> 5) & 1);
    out.freight = both || ((flags >> 6) & 1);
    const float value = item.getSpeedpostSpeed();
    out.speed = item.getSpeedPostSpeedUnitId() == 1 ? value * 0.44704f : value / 3.6f;
    out.forward = std::fabs(wrap(item.getSpeedpostRot() - (trackYaw - M_PI / 2.0))) > M_PI / 2.0;
    return true;
}

}
