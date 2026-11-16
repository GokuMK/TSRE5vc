#pragma once

class TRitem;

// A speed post item as Open Rails reads it (task tracks 16: shared by the
// simulation's track network and the superelevation's speeds): its kind,
// the trains it applies to, its speed and the way it faces.
namespace TrackSpeedPosts {

struct Post {
    enum Kind { Limit, Warning, Resume };
    Kind kind = Limit;
    bool passenger = true;
    bool freight = true;
    // m/s, as written (km/h or mph).
    float speed = 0.0f;
    // It faces trains moving the node's way.
    bool forward = true;
};

// `item` a speed post item (not a milepost) at a place on its node where
// the track's yaw is `trackYaw` (TDB::getDrawPositionOnTrNode's, radians):
// true and `out` filled; false for anything else. Facing is the post's
// angle against the track's direction there, as Open Rails takes it: a post
// at the track's angle less a quarter turn (as TSRE places a new one) faces
// trains going against the node's way.
bool read(TRitem &item, float trackYaw, Post &out);

}
