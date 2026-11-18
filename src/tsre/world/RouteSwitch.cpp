/*  This file is part of TSRE5.
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/world/RouteSwitch.h>
#include <tsre/ErrorMessagesLib.h>
#include <tsre/Game.h>
#include <tsre/Undo.h>
#include <tsre/geo/TerrainOverlays.h>
#include <tsre/humans/StationPeople.h>
#include <tsre/humans/StationWalkAreas.h>
#include <tsre/math3d/Flex.h>
#include <tsre/procedural/OrtsTrackProfile.h>
#include <tsre/procedural/OrtsTrackProfileRenderer.h>
#include <tsre/procedural/ProceduralShape.h>
#include <tsre/renderer/ExhaustParticles.h>
#include <tsre/sim/EventResults.h>
#include <tsre/sim/SimulationController.h>
#include <tsre/sound/SoundSystem.h>
#include <tsre/texture/TexLib.h>
#include <tsre/traffic/LevelCrossings.h>
#include <tsre/trains/ActLib.h>
#include <tsre/trains/VehiclePreload.h>
#include <tsre/world/TelepoleData.h>

namespace RouteSwitch {

void leave() {
    // First: its thread reads the route.
    Sim::SimulationController::instance()->forgetRoute();
    Sim::EventResultStore::instance()->forgetAll();
    VehiclePreload::cancel();
    Humans::StationPeople::instance().clear();
    Humans::StationWalkAreas::instance().clear();
    ExhaustParticles::instance().clear();
    // The old objects stay, so they never remove themselves.
    SoundSystem::clear();
    LevelCrossings::clear();
    TerrainOverlays::clear();
    // The next load reads the files again (the same route's too).
    ActLib::Clear();
    Flex::Reset();
    Undo::Clear();
    ErrorMessagesLib::ForgetRoute();
    Game::currentSelectedGameObj = nullptr;
    // Start options, for the first route only.
    Game::routeMergeString.clear();
    Game::ActivityToPlay.clear();
    // A "view only" answer was for the last route's track database.
    Game::writeTDBSessionAllowed = true;
}

void reloadingSameRoute(const QString &routePath) {
    TelepoleData::routeData(routePath, true);
    OrtsTrackProfileRenderer::clearTemplateMeshCache();
    OrtsTrackProfileCatalog::load(routePath, true);
    ProceduralShape::Loaded = false;
    TexLib::reloadTexturesUnder(routePath);
}

}
