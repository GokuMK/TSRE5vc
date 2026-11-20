/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */
// The station people's steering cost (task humans 13): a made-up station's
// walk area (a platform 300 m long and 8 m wide over terrain), Detour's
// crowd with StationPeople's agent settings, people standing (an idle agent,
// with the steering flags or none) and walking (to points 20 to 60 m along
// the platform, a new one each time one is reached). Each case: its crowd
// stepped 0.1 s, 20 steps to settle, then the mean of 200 steps timed.

#include "PeopleSteeringBenchmark.h"
#include "TokenTestSupport.h"
#include <tsre/humans/WalkArea.h>
#include <DetourCrowd.h>
#include <DetourNavMesh.h>
#include <DetourNavMeshQuery.h>
#include <QElapsedTimer>
#include <QString>
#include <cmath>
#include <memory>
#include <random>
#include <vector>

namespace {

using Humans::WalkArea;

void addBox(WalkArea::Input &in, float x0, float y0, float z0, float x1, float y1, float z1, WalkArea::Role role) {
    const int base = int(in.vertices.size() / 3);
    const float corners[8][3] = {{x0, y0, z0}, {x1, y0, z0}, {x1, y0, z1}, {x0, y0, z1},
                                 {x0, y1, z0}, {x1, y1, z0}, {x1, y1, z1}, {x0, y1, z1}};
    for (const auto &c : corners)
        in.vertices.insert(in.vertices.end(), c, c + 3);
    const int faces[6][4] = {{0, 1, 2, 3}, {4, 7, 6, 5}, {0, 4, 5, 1}, {1, 5, 6, 2}, {2, 6, 7, 3}, {3, 7, 4, 0}};
    for (const auto &f : faces) {
        for (int t : {0, 1, 2, 0, 2, 3})
            in.triangles.push_back(base + f[t]);
        in.roles.push_back(role);
        in.roles.push_back(role);
    }
}

struct Case {
    const char *name;
    int standing;
    int walking;
    // The standing ones' flags: StationPeople's (avoidance, separation) or
    // none (pushed only); how far they look for neighbours.
    bool standingSteer;
    float standingRange;
    // Where they are: half the stretch's length (m along the platform),
    // and the stretch across it (a busy station's density: Raszowa's 135 m
    // of platform, 900 people on 4.5 m).
    float halfLength = 140.0f;
    float across0 = 2.3f, across1 = 9.0f;
};

struct Result {
    double ms = 0.0;
    int placed = 0;
};

Result runCase(const dtNavMesh *mesh, const Case &c) {
    Result result;
    std::unique_ptr<dtCrowd, void (*)(dtCrowd *)> crowd(dtAllocCrowd(), dtFreeCrowd);
    if (!crowd->init(c.standing + c.walking, 0.6f, const_cast<dtNavMesh *>(mesh)))
        return result;
    crowd->getEditableFilter(0)->setIncludeFlags(WalkArea::FlagWalk);
    const dtNavMeshQuery *query = crowd->getNavMeshQuery();
    const dtQueryFilter *filter = crowd->getFilter(0);
    std::mt19937 random(12345u);
    std::uniform_real_distribution<float> along(-c.halfLength, c.halfLength), across(c.across0, c.across1),
            step(10.0f, std::min(60.0f, c.halfLength));
    const auto paramsOf = [](bool steer, float range, float speed) {
        dtCrowdAgentParams params{};
        params.radius = 0.25f;
        params.height = 1.8f;
        params.maxAcceleration = 2.0f;
        params.maxSpeed = speed;
        params.collisionQueryRange = range;
        params.pathOptimizationRange = 15.0f;
        params.separationWeight = 1.5f;
        params.updateFlags = steer ? (DT_CROWD_ANTICIPATE_TURNS | DT_CROWD_OPTIMIZE_VIS | DT_CROWD_OPTIMIZE_TOPO
                                      | DT_CROWD_OBSTACLE_AVOIDANCE | DT_CROWD_SEPARATION)
                                   : 0;
        params.obstacleAvoidanceType = 0;
        params.queryFilterType = 0;
        return params;
    };
    const float extents[3] = {1.0f, 2.0f, 1.0f};
    auto nearest = [&](float x, float z, float *out, dtPolyRef *ref) {
        const float p[3] = {x, 0.55f, z};
        return dtStatusSucceed(query->findNearestPoly(p, extents, filter, ref, out)) && *ref != 0;
    };
    std::vector<int> walkers;
    for (int i = 0; i < c.standing + c.walking; ++i) {
        const bool walks = i >= c.standing;
        float at[3];
        dtPolyRef ref = 0;
        if (!nearest(along(random), across(random), at, &ref))
            continue;
        const dtCrowdAgentParams params = walks ? paramsOf(true, 2.0f, 1.4f) : paramsOf(c.standingSteer, c.standingRange, 1.4f);
        const int agent = crowd->addAgent(at, &params);
        if (agent < 0)
            continue;
        ++result.placed;
        if (walks)
            walkers.push_back(agent);
    }
    auto retarget = [&](int agent) {
        const dtCrowdAgent *a = crowd->getAgent(agent);
        const float x = a->npos[0] + (random() % 2 ? 1.0f : -1.0f) * step(random);
        float to[3];
        dtPolyRef ref = 0;
        if (nearest(std::clamp(x, -c.halfLength, c.halfLength), across(random), to, &ref))
            crowd->requestMoveTarget(agent, ref, to);
    };
    for (int agent : walkers)
        retarget(agent);
    auto stepOnce = [&]() {
        crowd->update(0.1f, nullptr);
        for (int agent : walkers) {
            const dtCrowdAgent *a = crowd->getAgent(agent);
            const float dx = a->targetPos[0] - a->npos[0], dz = a->targetPos[2] - a->npos[2];
            if (dx * dx + dz * dz < 0.25f || a->targetState == DT_CROWDAGENT_TARGET_FAILED)
                retarget(agent);
        }
    };
    for (int i = 0; i < 20; ++i)
        stepOnce();
    QElapsedTimer timer;
    timer.start();
    constexpr int Steps = 200;
    for (int i = 0; i < Steps; ++i)
        stepOnce();
    result.ms = double(timer.nsecsElapsed()) / 1e6 / Steps;
    return result;
}

} // namespace

namespace TsreTests {

int runPeopleSteeringBenchmark(bool verbose) {
    TokenTest::Suite test{"[tests:people-steering-benchmark]", verbose};
    WalkArea::Input in;
    addBox(in, -150.0f, -0.3f, 1.6f, 150.0f, 0.55f, 9.6f, WalkArea::Role::Floor);
    WalkArea::Platform platform;
    platform.line = {-150.0f, 0.0f, 0.0f, 150.0f, 0.0f, 0.0f};
    platform.side = 1.0f;
    in.platforms.push_back(platform);
    in.tracks.push_back({-160.0f, 0.0f, 0.0f, 160.0f, 0.0f, 0.0f});
    const WalkArea::Built built = WalkArea::build(in);
    test.check(built.ok && built.mesh, QString("a 300 m platform's walk area: %1 polygons").arg(built.polygons));
    if (!built.ok || !built.mesh)
        return test.finish();
    const Case cases[] = {
        {"250 standing, steering flags (today's cap)", 250, 0, true, 2.0f},
        {"250 standing, 0 walking, no flags", 250, 0, false, 2.0f},
        {"1000 standing, steering flags", 1000, 0, true, 2.0f},
        {"1000 standing, no flags", 1000, 0, false, 2.0f},
        {"1000 standing, no flags, 0.6 m range", 1000, 0, false, 0.6f},
        {"300 walking alone", 0, 300, true, 2.0f},
        {"300 walking, 700 standing with flags", 700, 300, true, 2.0f},
        {"300 walking, 700 standing, no flags", 700, 300, false, 2.0f},
        {"300 walking, 700 standing, no flags, 0.6 m range", 700, 300, false, 0.6f},
        {"1000 walking", 0, 1000, true, 2.0f},
        {"Raszowa's density: 900 standing, steering flags (250's way for 900)", 900, 0, true, 2.0f, 67.5f, 4.5f, 9.0f},
        {"Raszowa's density: 300 walking, 600 standing, no flags, 0.6 m", 600, 300, false, 0.6f, 67.5f, 4.5f, 9.0f},
        {"Raszowa's density: 300 walking, 600 standing with flags", 600, 300, true, 2.0f, 67.5f, 4.5f, 9.0f},
        {"Raszowa's density: 900 walking", 0, 900, true, 2.0f, 67.5f, 4.5f, 9.0f},
    };
    for (const Case &c : cases) {
        const Result r = runCase(built.mesh.get(), c);
        test.check(r.placed == c.standing + c.walking,
                   QString("%1: %2 ms a step (%3 placed)").arg(c.name).arg(r.ms, 0, 'f', 3).arg(r.placed));
    }
    return test.finish();
}

}
