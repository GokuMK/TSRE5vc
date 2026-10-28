#include "EngLightsTestSuite.h"
#include "TokenTestSupport.h"
#include <QFile>
#include <QTemporaryDir>
#include <algorithm>
#include <cmath>
#include <tsre/Game.h>
#include <tsre/trains/Eng.h>
#include <tsre/trains/CarLights.h>
#include <tsre/trains/Consist.h>
#include <tsre/trains/EngLights.h>

namespace {

// Lights blocks as content writes them: a first block replaced by the
// second, comments, a count that is wrong, two Conditions blocks, a cone.
const char *const Fixture = R"(SIMISA@@@@@@@@@@JINX0D0t______

Wagon ( Test
	Type ( Engine )
	WagonShape ( test.s )
	Size ( 3 4 20 )
	Lights ( 1
		Light ( Type ( 0 ) States ( 1 State ( LightColour ( ff0000ff ) Radius ( 1 ) ) ) )
	)
	Lights ( 9
		ORTSGraphic ( "MyGlow.png" )
		comment ( "lights" )
		Light ( comment ( "front, bright" )
			Type ( 0 )
			Conditions (
				Headlight ( 3 )
				Unit ( 2 )
			)
			FadeIn ( 0.5 )
			FadeOut ( 0.25 )
			Cycle ( 0 )
			States ( 1
				State (
					Duration ( 0 )
					LightColour ( 85fffdda )
					Position ( 1.1 1.7 8.7 )
					Elevation ( 0 0 0 )
					Azimuth ( 0 0 0 )
					Transition ( 0 )
					Radius ( 0.05 )
				)
			)
		)
		Light ( comment ( "tail" )
			Type ( 0 )
			Conditions ( Headlight ( 4 ) Unit ( 3 ) )
			States ( 1 State ( LightColour ( FFFF0000 ) Position ( -1.1 1.7 -8.7 ) Azimuth ( 180 180 180 ) Radius ( 0.062 ) ) )
		)
		Light ( comment ( "beam" )
			Type ( 1 )
			Conditions ( Headlight ( 3 ) Unit ( 2 ) )
			States ( 1 State ( LightColour ( 85fffdda ) Position ( 0.5 3.5 11 ) Radius ( 70 ) Angle ( 30 ) Elevation ( 0 5 0 ) ) )
		)
		Light (
			Type ( 0 )
			Conditions ( Unit ( 2 ) Control ( 1 ) )
			Conditions ( Unit ( 2 ) Control ( 2 ) Service ( 2 ) )
			ShapeHierarchy ( "Bogie1" )
			States ( 1 State ( LightColour ( ffffffff ) Position ( 0 1 0 ) Radius ( 0.1 ) ) )
		)
		Light ( comment ( "flashing, three states announced, two written" )
			Type ( 0 )
			Cycle ( 1 )
			States ( 3
				State ( Duration ( 0.5 ) LightColour ( ff00ff00 ) Radius ( 0.2 ) Transition ( 1 ) )
				State ( Duration ( 0.75 ) LightColour ( 00000000 ) Radius ( 0.2 ) )
			)
		)
		Light (
			Type ( 0 )
			Conditions ( Unit ( 02 ) TimeOfDay ( 2 ) Weather ( 2 ) )
			States ( 1 State ( LightColour ( ffffff00 ) Radius ( 0.1 ) ) )
		)
	)
)

Engine ( Test
	Wagon ( Test )
	Type ( Electric )
	Name ( "Lights test" )
)
)";

const char *const WagonFixture = R"(SIMISA@@@@@@@@@@JINX0D0t______

Wagon ( W
	Type ( Freight )
	WagonShape ( w.s )
)
)";

bool near(float a, float b, float tolerance = 1e-4f) { return std::fabs(a - b) < tolerance; }

using Light = EngLights::Light;
using Train = EngLights::Train;

bool holds(const EngLights::Condition &c, const Train &t) { return EngLights::holds(c, t); }

}

int TsreTests::runEngLightsSuite(bool verbose) {
    TokenTest::Suite test{"[tests:eng-lights]", verbose};
    QTemporaryDir temp;
    {
        QFile file(temp.filePath("test.eng"));
        test.check(file.open(QIODevice::WriteOnly) && file.write(Fixture) > 0, "write fixture");
    }
    Eng eng(temp.path(), "test.eng");
    test.check(eng.loaded == 1 && eng.getLights() != nullptr, "the fixture loads with lights");
    if (eng.getLights() == nullptr)
        return test.finish();
    const EngLights &lights = *eng.getLights();
    test.check(lights.lights.size() == 7 && lights.graphic == "MYGLOW.PNG",
               "the later block replaces the first; six lights and the cone's reversed copy; the block's glow");
    if (lights.lights.size() != 7)
        return test.finish();
    const Light &front = lights.lights[0];
    test.check(front.index == 0 && front.type == EngLights::Glow && near(front.fadeIn, 0.5f) && near(front.fadeOut, 0.25f)
                   && !front.cycle && front.conditions.size() == 1 && front.conditions[0].headlight == 3
                   && front.conditions[0].unit == 2 && front.states.size() == 1,
               "a glow's type, fades and conditions");
    const EngLights::State &s = front.states[0];
    test.check(s.colour == 0x85fffddau && near(s.position[0], 1.1f) && near(s.position[1], 1.7f)
                   && near(s.position[2], 8.7f) && near(s.radius, 0.05f) && !s.transition,
               "a state's colour (ARGB), position and radius");
    float d[3];
    s.direction(d);
    test.check(near(d[0], 0) && near(d[1], 0) && near(d[2], 1), "azimuth 0 shines forward");
    lights.lights[1].states[0].direction(d);
    test.check(lights.lights[1].states[0].colour == 0xffff0000u && near(d[0], 0) && near(d[2], -1),
               "azimuth 180 shines backwards");
    const Light &beam = lights.lights[2];
    beam.states[0].direction(d);
    test.check(beam.type == EngLights::Cone && near(beam.states[0].halfAngle(), float(M_PI) / 12.0f)
                   && near(beam.states[0].reach(), 35.0f / std::sin(float(M_PI) / 12.0f), 1e-3f)
                   && near(d[1], -std::sin(5.0f * float(M_PI) / 180.0f)) && d[2] > 0.99f,
               "a cone's half-angle, reach (135 m) and elevation (down)");
    const Light &reversed = lights.lights[6];
    test.check(reversed.reversed && reversed.index == beam.index && reversed.type == EngLights::Cone
                   && near(reversed.states[0].position[0], -0.5f) && near(reversed.states[0].position[2], -11.0f)
                   && near(reversed.states[0].azimuth[1], 180.0f) && reversed.conditions[0].unit == 5,
               "the cone's reversed copy, as MSTS made it: mirrored, turned, for the first unit reversed");
    test.check(lights.lights[3].conditions.size() == 2 && lights.lights[3].conditions[1].service == 2
                   && lights.lights[3].shapeHierarchy == "BOGIE1",
               "two Conditions blocks and a shape matrix name");
    const Light &flashing = lights.lights[4];
    test.check(flashing.cycle && flashing.conditions.isEmpty() && flashing.states.size() == 2
                   && flashing.states[0].transition && near(flashing.states[1].duration, 0.75f)
                   && flashing.states[1].colour == 0,
               "the states written count, not the count announced");
    test.check(lights.lights[5].conditions[0].unit == 2 && lights.lights[5].conditions[0].timeOfDay == 2
                   && lights.lights[5].conditions[0].weather == 2,
               "a unit written 02, time of day and weather");

    // Which lights a train turns on, by Open Rails' rules.
    const Train standard;
    test.check(standard.headlight == Train::HeadlightBright && standard.inService && standard.player
                   && !standard.penalty && standard.first && standard.last && !standard.reversed,
               "the test state: a train in service, headlights at Bright, the player's");
    auto on = [&](int i, const Train &t) { return EngLights::isOn(lights.lights[i], t); };
    test.check(on(0, standard) && on(1, standard) && on(2, standard) && !on(6, standard) && on(4, standard),
               "a single car: front, tail and beam on, the reversed beam off, a light without conditions on");
    Train flipped;
    flipped.reversed = true;
    test.check(!on(0, flipped) && !on(1, flipped) && !on(2, flipped) && on(6, flipped),
               "flipped: the reversed beam instead");
    Train middle;
    middle.first = middle.last = false;
    test.check(!on(0, middle) && !on(1, middle) && !on(2, middle), "a car in the middle shows none of them");
    Train ai;
    ai.player = false;
    Train parked;
    parked.inService = false;
    test.check(on(3, ai) && on(3, standard) && !on(3, parked), "any one Conditions block turns a light on");
    Train night;
    night.day = false;
    night.weather = Train::Rain;
    test.check(!on(5, standard) && on(5, night), "night and rain");

    // The headlight switch (rows: the condition, columns: off, dim, bright).
    const bool headlight[7][3] = {{true, true, true},   {true, false, false}, {false, true, false},
                                  {false, false, true}, {false, true, true},  {true, false, true},
                                  {true, true, false}};
    bool headlightRight = true;
    for (int value = 0; value <= 6; ++value)
        for (int position = 0; position < 3; ++position) {
            EngLights::Condition c;
            c.headlight = value;
            Train t;
            t.headlight = position;
            headlightRight = headlightRight && holds(c, t) == headlight[value][position];
        }
    EngLights::Condition unknown;
    unknown.headlight = 7;
    test.check(headlightRight && !holds(unknown, standard),
               "headlight off, dim, bright, dim or bright, off or bright, off or dim; unknown values never");
    // Units: first, last, reversed.
    auto unit = [&](int value, bool first, bool last, bool reversed) {
        EngLights::Condition c;
        c.unit = value;
        Train t;
        t.first = first;
        t.last = last;
        t.reversed = reversed;
        return holds(c, t);
    };
    test.check(unit(1, false, false, false) && !unit(1, true, false, false) && unit(2, true, false, false)
                   && !unit(2, true, false, true) && unit(3, false, true, false) && !unit(3, false, true, true)
                   && unit(4, false, true, true) && unit(5, true, false, true) && !unit(5, false, true, true),
               "units: middle, first, last, last reversed, first reversed");
    EngLights::Condition c;
    Train t;
    c.coupling = 1;
    t.coupledFront = true;
    const bool front1 = holds(c, t);
    t.coupledRear = true;
    const bool front2 = holds(c, t);
    c.coupling = 3;
    test.check(front1 && !front2 && holds(c, t), "coupling at the front only, or at both ends");
    c = EngLights::Condition();
    c.reverser = 6;
    t = Train();
    const bool forward = holds(c, t);
    t.reverser = 0;
    test.check(!forward && holds(c, t), "reverser reverse or neutral");
    c = EngLights::Condition();
    c.doors = 5;
    t = Train();
    const bool closed = holds(c, t);
    t.rightDoorOpen = true;
    test.check(!closed && holds(c, t), "doors left or right open");
    c = EngLights::Condition();
    c.mu = 2;
    t = Train();
    const bool lead = holds(c, t);
    t.mu = 0;
    test.check(lead && !holds(c, t), "a local unit: the lead or one with it, not a remote one");
    c = EngLights::Condition();
    c.battery = 2;
    c.brake = 2;
    c.penalty = 2;
    t = Train();
    t.battery = false;
    t.brakeApplied = true;
    const bool noPenalty = holds(c, t);
    t.penalty = true;
    test.check(!noPenalty && holds(c, t), "battery off, brakes applied, penalty");

    Eng copy(&eng);
    Eng again(temp.path(), "test.eng");
    test.check(copy.getLights() == eng.getLights() && again.getLights() == eng.getLights()
                   && again.getLights() != nullptr && again.getLights()->lights.size() == 7,
               "copies of a car share its kept block and its lights; another load of the file unchanged shares them too (task driving 01)");

    // A consist gives each car its place: a locomotive, a wagon, a
    // locomotive turned round.
    {
        QFile file(temp.filePath("wagon.wag"));
        test.check(file.open(QIODevice::WriteOnly) && file.write(WagonFixture) > 0, "write wagon fixture");
    }
    Eng wagon(temp.path(), "wagon.wag");
    Consist consist;
    consist.engItems.resize(3);
    consist.engItems[0].engPointer = &eng;
    consist.engItems[1].engPointer = &wagon;
    consist.engItems[2].engPointer = &copy;
    consist.engItems[2].flip = true;
    const Train head = consist.lightsTrain(0), between = consist.lightsTrain(1), end = consist.lightsTrain(2);
    test.check(wagon.getLights() == nullptr && head.first && !head.last && !head.reversed && !head.coupledFront
                   && head.coupledRear && head.mu == -1,
               "the first car: first, coupled at its rear, the leading locomotive");
    test.check(!between.first && !between.last && between.coupledFront && between.coupledRear && between.mu == 0,
               "a wagon between: coupled at both ends, no unit");
    test.check(end.last && end.reversed && !end.coupledFront && end.coupledRear && end.mu == 1,
               "the last car turned round: reversed, coupled at its own rear, running with the leading one");
    EngLights::setEnvironment(false, Train::Snow);
    const Train snowNight = consist.lightsTrain(0);
    EngLights::setEnvironment(true, Train::Clear);
    test.check(!snowNight.day && snowNight.weather == Train::Snow && consist.lightsTrain(0).day,
               "the scene's day and weather");
    test.check(standard.headlight == head.headlight && head.inService && head.player,
               "the rest is the test state");

    // Lenses on the car: the shape's root mirrors x; a glow is a square
    // Radius wide, its lens a disc of half that, a centimetre out.
    const std::vector<CarLights::Lens> lenses = CarLights::lenses(lights, standard);
    test.check(lenses.size() == 4, "lenses of the lit glows (the beams and the night light are not)");
    if (lenses.size() == 4) {
        const CarLights::Lens &f = lenses[0], &tail = lenses[1];
        test.check(near(f.position[0], -1.1f) && near(f.position[1], 1.7f) && near(f.position[2], 8.71f)
                       && near(f.direction[2], 1.0f) && near(f.radius, 0.025f) && f.colour == 0x85fffddau,
                   "a front lamp: x mirrored, a centimetre ahead, facing forward");
        test.check(near(tail.position[0], 1.1f) && near(tail.position[2], -8.71f) && near(tail.direction[2], -1.0f)
                       && near(tail.radius, 0.031f),
                   "a tail lamp: a centimetre behind, facing backwards");
    }

    // Beams: spot lights of the first lit cone, reaching as far as it, out
    // of the car's lit head lamps (white, facing the cone's way, at its
    // end: the fixture's front lamp; not the white light at the middle);
    // without lamps from the cone, at the car's end at the latest (the
    // fixture's cone is 1 m beyond it).
    const float halfLength = eng.definition->size[2] * 0.5f;
    const float half = float(M_PI) / 12.0f, reach = 35.0f / std::sin(half);
    const float down = 5.0f * float(M_PI) / 180.0f;
    const float strength = 0.01f * float(M_PI) * reach * reach;
    const std::vector<RenderItem::Light> beams = CarLights::beams(lights, standard, halfLength);
    test.check(beams.size() == 1, "one beam for the lit cone, out of its one head lamp");
    if (beams.size() == 1) {
        const RenderItem::Light &b = beams[0];
        test.check(near(halfLength, 10.0f) && b.type == RenderItem::Light::SPOT && near(b.position[0], -1.1f)
                       && near(b.position[1], 1.7f) && near(b.position[2], 8.71f)
                       && near(b.direction[1], -std::sin(down)) && b.direction[2] > 0.99f,
                   "the beam out of the head lamp (x mirrored), the cone's way, a little down");
        test.check(near(b.range, reach, 1e-2f) && near(b.intensity, strength, 1.0f)
                       && near(b.innerCone, 0.7f * half) && near(b.outerCone, 1.5f * half) && near(b.color[0], 1.0f),
                   "its reach (135 m), its strength (faint at its reach) and cone");
    }
    // Two head lamps share the cone's strength; one lit twice (player and
    // AI lamps of one place) counts once.
    EngLights twoLamps = lights, sameLamp = lights, coneOnly = lights;
    for (const EngLights::Light &light : lights.lights)
        if (light.type == EngLights::Glow && !light.states.isEmpty() && light.states[0].colour == 0x85fffddau) {
            EngLights::Light other = light;
            other.states[0].position[0] = -other.states[0].position[0];
            twoLamps.lights.push_back(other);
            sameLamp.lights.push_back(light);
        }
    const std::vector<RenderItem::Light> shared = CarLights::beams(twoLamps, standard, halfLength);
    test.check(shared.size() == 2 && near(shared[0].intensity, strength * 0.5f, 1.0f)
                   && near(shared[1].intensity, strength * 0.5f, 1.0f)
                   && near(shared[0].position[0], -shared[1].position[0]),
               "two head lamps: a beam out of each, half the strength each");
    test.check(CarLights::beams(sameLamp, standard, halfLength).size() == 1, "a lamp lit twice: one beam");
    coneOnly.lights.erase(std::remove_if(coneOnly.lights.begin(), coneOnly.lights.end(),
                                         [](const EngLights::Light &light) { return light.type == EngLights::Glow; }),
                          coneOnly.lights.end());
    const std::vector<RenderItem::Light> fromCone = CarLights::beams(coneOnly, standard, halfLength);
    test.check(fromCone.size() == 1 && near(fromCone[0].position[0], -0.5f) && near(fromCone[0].position[2], 10.0f)
                   && near(fromCone[0].position[1], 3.5f + std::tan(down)) && near(fromCone[0].intensity, strength, 1.0f),
               "without head lamps, the cone's beam from the car's end, moved back along it (x mirrored)");
    const std::vector<RenderItem::Light> unmoved = CarLights::beams(coneOnly, standard, 0.0f);
    test.check(unmoved.size() == 1 && near(unmoved[0].position[2], 11.0f) && near(unmoved[0].position[1], 3.5f),
               "without head lamps and the car's size, the beam at the cone's place");
    const std::vector<RenderItem::Light> back = CarLights::beams(lights, flipped, halfLength);
    test.check(back.size() == 1 && near(back[0].position[2], -10.0f) && back[0].direction[2] < -0.99f,
               "flipped: the reversed cone's beam, backwards from the other end");

    // The CMK test train's locomotive, when the content is there.
    const QString vectron = Game::root + "/TRAINS/TRAINSET/PKOR_Vectron";
    if (QFile::exists(vectron + "/ID_6193-490_PL.eng")) {
        Eng loco(vectron, "ID_6193-490_PL.eng");
        const EngLights *vectronLights = loco.getLights();
        test.check(vectronLights != nullptr && vectronLights->lights.size() == 1104,
                   "PKOR_Vectron: 1101 lights and 3 reversed cones");
        if (vectronLights != nullptr) {
            auto count = [&](const Train &train, quint32 colour, int type, bool frontEnd) {
                int n = 0;
                for (const Light &light : vectronLights->lights)
                    if (light.type == type && EngLights::isOn(light, train)
                            && (light.type == EngLights::Cone || light.states[0].colour == colour)
                            && (light.states[0].position[2] > 0) == frontEnd)
                        ++n;
                return n;
            };
            const quint32 white = 0xfffffdd9u, red = 0xffff0000u;
            test.check(count(standard, white, EngLights::Glow, true) == 65 && count(standard, red, EngLights::Glow, false) == 92
                           && count(standard, white, EngLights::Glow, false) == 0
                           && count(standard, red, EngLights::Glow, true) == 0
                           && count(standard, 0, EngLights::Cone, true) == 1
                           && count(standard, 0, EngLights::Cone, false) == 0,
                       "PKOR_Vectron at Bright: 65 white lamps and a beam at the front, 92 red at the back");
            test.check(count(flipped, white, EngLights::Glow, false) == 65 && count(flipped, red, EngLights::Glow, true) == 92
                           && count(flipped, 0, EngLights::Cone, false) == 1
                           && count(flipped, 0, EngLights::Cone, true) == 0,
                       "PKOR_Vectron flipped: the same the other way round");
            // Its 65 white dots are three lamps (7, 7 and 51 dots): a beam
            // out of each, a third of the long cone's strength.
            const std::vector<RenderItem::Light> vectronBeams = CarLights::beams(*vectronLights, standard, loco.definition->size[2] * 0.5f);
            int low = 0, top = 0;
            for (const RenderItem::Light &b : vectronBeams) {
                low += near(b.position[1], 1.88f, 0.02f) && near(std::fabs(b.position[0]), 1.15f, 0.02f);
                top += near(b.position[1], 3.61f, 0.02f) && near(b.position[0], 0.0f, 0.02f);
            }
            test.check(vectronBeams.size() == 3 && low == 2 && top == 1 && near(vectronBeams[0].range, 135.2f, 0.1f)
                           && vectronBeams[0].position[2] > 8.0f,
                       "PKOR_Vectron at Bright: the long beam (135 m) out of its three lamps at the front (8.3 to 8.7 m), not its dots");
        }
    } else if (verbose) {
        qInfo().noquote() << "[tests:eng-lights] no PKOR_Vectron under" << Game::root;
    }
    return test.finish();
}
