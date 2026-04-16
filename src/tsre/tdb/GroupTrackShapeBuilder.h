/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef GROUPTRACKSHAPEBUILDER_H
#define GROUPTRACKSHAPEBUILDER_H

#include <array>
#include <QVector>

class GroupObj;
class TrackObj;
class TSectionDAT;
class TrackShape;

class GroupTrackShapeBuilder {
public:
    struct Result {
        TrackShape* shape = NULL;
        QVector<std::array<float, 3>> endpoints;

        Result() = default;
        Result(const Result&) = delete;
        Result& operator=(const Result&) = delete;
        ~Result();
        void clear();
        bool isValid() const;
        TrackShape* releaseShape();
    };

    static bool build(Result& out, GroupObj* group, TrackObj* anchor, TSectionDAT* tsection);
};

#endif
