/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include "PathTools.h"
#include "ToolContext.h"
#include <tsre/Game.h>
#include <tsre/Undo.h>
#include <tsre/tdb/TDB.h>
#include <tsre/trains/Path.h>
#include <tsre/trains/PathTrack.h>
#include <QAction>
#include <QMenu>

namespace {

using PathTrack::Node;
using PathTrack::Note;
using PathTrack::Result;

// The path being edited: the path selected (the activity tools' Edit
// selects it).
Path *editedPath(ToolContext &ctx) {
    GameObj *selected = ctx.selected();
    if (selected == nullptr || selected->typeObj != GameObj::activitypath)
        return nullptr;
    return static_cast<Path *>(selected);
}

// The track point nearest the pointer, from the track database, as the
// activity tools find it: across the ground on the map, where the pointer
// has no height.
bool trackAtPointer(ToolContext &ctx, PathTrack::TrackPoint &point) {
    TDB *tdb = Game::trackDB;
    if (tdb == nullptr)
        return false;
    int tileX = ctx.tileX();
    int tileZ = ctx.tileZ();
    float position[3] = {ctx.pointer()[0], ctx.pointer()[1], ctx.pointer()[2]};
    Game::check_coords(tileX, tileZ, position);
    float tile[2] = {float(tileX), float(tileZ)};
    float found[3];
    if (tdb->findNearestPositionOnTDB(tile, position, nullptr, found,
                                      ctx.viewMode() == ViewMode::Scene3D) < 0)
        return false;
    point.track = int(found[0]);
    point.distance = found[1];
    return true;
}

QString noteText(Note note) {
    switch (note) {
    case Note::NoTrack:
        //% "no track here"
        return qtTrId("route.editor.path.tool.note.no.track");
    case Note::NoPath:
        //% "the path has no start"
        return qtTrId("route.editor.path.tool.note.no.path");
    case Note::NotOnPath:
        //% "only on the path"
        return qtTrId("route.editor.path.tool.note.not.on.path");
    case Note::NoRoute:
        //% "no route without a reversal"
        return qtTrId("route.editor.path.tool.note.no.route");
    case Note::NotFacingJunction:
        //% "no start or facing junction here"
        return qtTrId("route.editor.path.tool.note.not.facing");
    case Note::Rejoined:
        //% "rejoins the path"
        return qtTrId("route.editor.path.tool.note.rejoined");
    case Note::RestReplaced:
        //% "replaces the rest of the path"
        return qtTrId("route.editor.path.tool.note.rest.replaced");
    case Note::StoppedAtLoop:
        //% "ends before a loop"
        return qtTrId("route.editor.path.tool.note.loop");
    case Note::StoppedAtLimit:
        //% "ends after 200 km"
        return qtTrId("route.editor.path.tool.note.limit");
    case Note::StoppedAtBrokenTrack:
        //% "ends where the track database is broken"
        return qtTrId("route.editor.path.tool.note.broken");
    default:
        return QString();
    }
}

Result failed(Note note) {
    Result result;
    result.note = note;
    return result;
}

// An edit as one undo step; the services on the path place their consists
// again (Path::apply).
void applyEdit(Path *path, const QVector<Node> &nodes) {
    const bool own = !Undo::IsStateOpen();
    if (own)
        Undo::StateBegin();
    Undo::PushPathData(path);
    path->apply(nodes);
    if (own)
        Undo::StateEnd();
}

// Edits the path selected in the activity tools (task editor 06). A left
// click starts a new path; the context menu acts at the track nearest the
// pointer (the track database's lookup), and Take Other Direction at the
// start or facing junction on that track (PathTrack::otherDirectionNode).
class TrackPathTool : public EditorTool {
public:
    TrackPathTool() : EditorTool(PathTools::TrackPathToolId, ViewMode::Scene3D | ViewMode::Map) {}

    QString title() const override {
        //% "Path"
        return qtTrId("route.editor.path.tool.title");
    }

    // A new path, without nodes yet, starts where it is first clicked.
    bool press(ToolContext &ctx, const ToolMouse &) override {
        Path *path = editedPath(ctx);
        PathTrack::TrackPoint point;
        if (path != nullptr && Game::trackDB != nullptr && path->nodes().isEmpty()
                && trackAtPointer(ctx, point)) {
            const Result start = PathTrack::startAt(Game::trackDB, point);
            if (start.ok)
                applyEdit(path, start.nodes);
        }
        return true;
    }

    void contextMenu(ToolContext &ctx, QMenu &menu) override {
        Path *path = editedPath(ctx);
        TDB *tdb = Game::trackDB;
        if (path == nullptr || tdb == nullptr) {
            QAction *none = menu.addAction(
                //% "Choose a path in the activity tools and press Edit"
                qtTrId("route.editor.path.tool.no.path"));
            none->setEnabled(false);
            return;
        }
        menu.addSection(
            //% "Path: %1"
            qtTrId("route.editor.path.tool.section")
                .arg(path->displayName.isEmpty() ? path->nameId : path->displayName));
        const QVector<Node> nodes = path->nodes();
        const QVector<PathTrack::Leg> legs = path->legs();
        PathTrack::TrackPoint point;
        const bool onTrack = trackAtPointer(ctx, point);
        const int node = onTrack ? PathTrack::otherDirectionNode(tdb, nodes, legs, point) : -1;

        auto add = [&menu, path](const QString &text, const Result &result) {
            const QString note = noteText(result.note);
            QAction *action = menu.addAction(note.isEmpty() ? text : text + " (" + note + ")");
            action->setEnabled(result.ok);
            if (!result.ok)
                return;
            const QVector<Node> changed = result.nodes;
            QObject::connect(action, &QAction::triggered, [path, changed] { applyEdit(path, changed); });
        };
        add(
            //% "Start Here"
            qtTrId("route.editor.path.tool.start"),
            onTrack ? PathTrack::startAt(tdb, point) : failed(Note::NoTrack));
        add(
            //% "End Here"
            qtTrId("route.editor.path.tool.end"),
            onTrack ? PathTrack::endAt(tdb, nodes, point) : failed(Note::NoTrack));
        Result other = failed(Note::NotFacingJunction);
        if (node == 0)
            other = PathTrack::turnStart(tdb, nodes);
        else if (node > 0)
            other = PathTrack::otherExit(tdb, nodes, node);
        add(
            //% "Take Other Direction"
            qtTrId("route.editor.path.tool.other.direction"),
            other);
        add(
            //% "Add Reverse Point"
            qtTrId("route.editor.path.tool.reverse"),
            onTrack ? PathTrack::reverseAt(tdb, nodes, point) : failed(Note::NoTrack));
    }
};

}

namespace PathTools {

std::vector<std::unique_ptr<EditorTool>> create() {
    std::vector<std::unique_ptr<EditorTool>> tools;
    tools.push_back(std::make_unique<TrackPathTool>());
    return tools;
}

}
