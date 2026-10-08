/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef SHAPELOADER_H
#define SHAPELOADER_H

class ComplexShape;

// Loads shapes on worker threads (core.rendering.threadedShapeLoading). A job
// loads a detached copy of a shape, from its file to its meshes; the shape
// adopts the copy on the main thread the next time it is drawn, and its
// textures are registered then. Until then the shape is unloaded to the
// rest of the editor, as a shape waiting for its turn always was.
// Without threading, a shape loads on the main thread when it is drawn, as
// many a frame as Game::objectLoadingTokens allow.
namespace ShapeLoader {

enum class Request {
    LoadHere, // load now, on this thread
    Wait,     // loading on a worker, or no worker or token free this frame
    Adopted   // a finished job was adopted: the shape is loaded or failed
};

// Called by an unloaded shape when it is drawn (main thread).
Request request(ComplexShape *shape);
// Drops the shape's job, if any: its result is never adopted. Reloading and
// destroying a shape call it.
void cancel(ComplexShape *shape);
// True while a job is running.
bool busy();
// Jobs finished so far: a view has settled only once this stops changing.
unsigned progress();
// Blocks until no job is running.
void waitForAll();

// While one exists, requests have no limits: no tokens, no cap on jobs.
// The route editor loads its first view, and the view after a camera jump,
// this way before showing it.
class WholeView {
public:
    WholeView();
    ~WholeView();
    WholeView(const WholeView &) = delete;
    WholeView &operator=(const WholeView &) = delete;
};

}

#endif /* SHAPELOADER_H */
