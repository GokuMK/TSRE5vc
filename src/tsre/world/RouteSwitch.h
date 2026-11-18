/*  This file is part of TSRE5.
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef ROUTESWITCH_H
#define ROUTESWITCH_H

#include <QString>

// Another route, or the same one again, loaded in the running editor (task
// editor 10). The old route is kept, not freed: nothing still pointing at it
// can dangle, and no cache keyed by an address can take a new object for an
// old one. What would keep it running, or show it on the next route, is cut
// off here; the view does its own part (RouteEditorGLWidget::leaveRoute).
namespace RouteSwitch {
// Before the next route loads: the simulation stopped, the route's
// registries, singletons, libraries, undo and messages emptied.
void leave();
// The same route about to be read again: its caches by folder read again
// with force, its textures read from disk (unsaved paint dropped).
void reloadingSameRoute(const QString &routePath);
}

#endif /* ROUTESWITCH_H */
