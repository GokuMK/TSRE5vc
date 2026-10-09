#include "SigCfgTestSuite.h"
#include "TokenTestSupport.h"
#include <QFile>
#include <QTemporaryDir>
#include <cmath>
#include <tsre/tdb/SigCfg.h>
#include <tsre/tdb/SignalShape.h>
#include <tsre/tdb/SignalType.h>

namespace {

const char *const Fixture = R"(SIMISA@@@@@@@@@@JINX0G0t______

comment ( "a comment block" )
LightTextures ( 1
	LightTex ( "LTex" "SigLight.ace" 0 0 1 1 )
)
LightsTab ( 3
	LightsTabEntry ( "Red Light" colour ( 255 255 40 40 ) )
	LightsTabEntry ( "Green Light" colour ( 255 0 255 0 ) )
	_LightsTabEntry ( "White Light" colour ( 255 255 255 255 ) )
	LightsTabEntry ( "Amber Light" colour ( 255 255 200 0 ) )
)
ORTSNormalSubtypes ( 1 ORTSNormalSubtype ( "Home" ) )
SignalTypes ( 3
	SignalType ( "Home3"
		SignalFnType ( NORMAL )
		SignalLightTex ( "ltex" )
		SignalFlags ( Abs )
		SigFlashDuration ( 0.6 0.25 )
		ORTSNormalSubtype ( "HOME" )
		ORTSDayGlow ( 2 )
		ORTSDayLight ( false )
		SignalLights ( 3
			SignalLight ( 2 "Green Light" Position ( 0 0.7 0.01 ) Radius ( 0.1 ) )
			SignalLight ( 0 "Red Light" Position ( 0 0.2 0.01 ) Radius ( 0.1 ) )
			SignalLight ( 1 "Amber Light" Position ( a b c ) Radius ( 0.1 ) SignalFlags ( SEMAPHORE_CHANGE ) )
		)
		SignalDrawStates ( 4
			SignalDrawState ( 0 "Red" DrawLights ( 1 DrawLight ( 0 ) ) )
			SignalDrawState ( 1 "Yellow" DrawLights ( 1 DrawLight ( 1 SignalFlags ( flashing ) ) ) )
			SignalDrawState ( 2 "Green" DrawLights ( 1 DrawLight ( 2 ) ) SemaphorePos ( 1 ) )
			SignalDrawState ( 3 "Red" DrawLights ( 1 DrawLight ( 0 ) ) )
		)
		SignalAspects ( 3
			SignalAspect ( CLEAR_2 "Green" )
			SignalAspect ( APPROACH_1 "Yellow" SpeedKPH ( 72 ) )
			SignalAspect ( STOP "Red" SignalFlags ( ASAP ) )
		)
		SignalNumClearAhead ( 2 )
		SignalNumClearAhead ( 3 )
	)
	SignalType ( "NoAspects"
		SignalFnType ( SHUNTING )
		SignalLights ( 1 SignalLight ( 0 "red light" Radius ( 0.05 ) ) )
		SignalDrawStates ( 2
			SignalDrawState ( 1 "Off" )
			SignalDrawState ( 0 "On" DrawLights ( 1 DrawLight ( 0 ) ) )
		)
	)
	skip ( SignalType ( "Skipped" ) )
	SignalType ( "Custom"
		SignalFnType ( MYSTERY )
	)
)
SignalShapes ( 1
	SignalShape ( "Home.S" "Home signal"
		SignalSubObjs ( 3
			SignalSubObj ( 1 "HEAD1" "Head" SigSubType ( signal_head ) SigSubSType ( "Home3" ) SignalFlags ( optional DEFAULT ) )
			SignalSubObj ( 0 "POLE" "Pole" SigSubType ( DECOR ) )
			SignalSubObj ( 7 "BAD" "Out of range" )
			SignalSubObj ( 2 "HEAD2" "Back" SigSubType ( SIGNAL_HEAD ) SigSubSType ( "NoAspects" ) SignalFlags ( BACK_FACING ) )
		)
	)
)
ScriptFiles ( ScriptFile ( "sigscr.dat" ) )
)";

bool near(float a, float b) { return std::fabs(a - b) < 1e-4f; }

}

int TsreTests::runSigCfgSuite(const QString &configurationPath, bool verbose) {
    TokenTest::Suite test{"[tests:sigcfg]", verbose};
    QTemporaryDir temp;
    const QString path = temp.filePath("sigcfg.dat");
    {
        QFile file(path);
        test.check(file.open(QIODevice::WriteOnly) && file.write(Fixture) > 0, "write fixture");
    }
    SigCfg cfg(path);
    test.check(cfg.loaded && cfg.sourceFileExists, "fixture loads");
    test.check(cfg.lightTextures.size() == 1 && cfg.lightTextures.contains("ltex")
                   && cfg.lightTextures["ltex"].file == "SigLight.ace" && near(cfg.lightTextures["ltex"].u1, 1),
               "light texture by lower-case name");
    test.check(cfg.lightsTable.size() == 3 && !cfg.lightsTable.contains("white light")
                   && cfg.lightsTable["amber light"].r == 255 && cfg.lightsTable["amber light"].g == 200
                   && cfg.lightsTable["amber light"].b == 0,
               "light colours, the _ entry ignored and the next one kept");
    test.check(cfg.signalType.size() == 3 && cfg.findSignalType("HOME3") && cfg.findSignalType("noaspects")
                   && cfg.findSignalType("custom") && !cfg.findSignalType("skipped") && !cfg.findSignalType("ltex"),
               "signal types keyed by their name, the skipped one left out");
    const SignalType *home = cfg.findSignalType("home3");
    if (home == nullptr) return test.finish();
    test.check(home->function == "NORMAL" && home->lightTexture == "ltex" && home->abs && !home->semaphore,
               "function, light texture and flags kept apart");
    test.check(near(home->flashTimeOn, 0.6f) && near(home->flashTimeOff, 0.25f) && home->normalSubtype == "HOME"
                   && near(home->dayGlow, 2) && home->nightGlow < 0 && !home->dayLight
                   && home->numClearAhead == QVector<int>({2, 3}),
               "flashing, Open Rails keys and clear-ahead values");
    test.check(home->lights.size() == 3 && home->lights[0].index == 0 && home->lights[2].index == 2
                   && home->lights[0].name == "red light" && near(home->lights[2].position[1], 0.7f)
                   && near(home->lights[0].radius, 0.1f),
               "lights sorted by index with positions and radii");
    test.check(home->light(1) && home->light(1)->semaphoreChange && near(home->light(1)->radius, 0.1f)
                   && near(home->light(1)->position[0], 0),
               "a malformed position loses only itself");
    test.check(home->drawStates.size() == 4 && home->drawState("red") && home->drawState("dst3")
                   && home->drawState("yellow")->lights.size() == 1 && home->drawState("yellow")->lights[0].flashing
                   && near(home->drawState("green")->semaphorePos, 1),
               "draw states with flashing lights and semaphore positions; a duplicate renamed");
    test.check(home->aspects.size() == 3 && home->aspects[1].aspect == SignalType::APPROACH_1
                   && near(home->aspects[1].speedMpS, 20) && home->aspects[2].asap,
               "aspects with speeds and flags");
    test.check(home->defaultDrawState() && home->defaultDrawState()->name == "red"
                   && home->defaultDrawState()->lights[0].light == 0,
               "default draw state of the most restrictive aspect");
    const SignalType *shunt = cfg.findSignalType("noaspects");
    test.check(shunt && shunt->function == "SHUNTING" && shunt->defaultDrawState()
                   && shunt->defaultDrawState()->name == "on",
               "without aspects the draw state with the lowest index");
    test.check(cfg.findSignalType("custom")->function == "INFO", "unknown function becomes INFO");
    SignalShape *shape = cfg.findSignalShape("home.s");
    test.check(shape && shape->desc == "Home signal" && shape->iSubObj == 3 && cfg.signalShapeById.value(0) == shape,
               "signal shape by name and list position");
    if (shape != nullptr && shape->iSubObj == 3) {
        const SignalShape::SubObj &pole = shape->subObj[0], &head = shape->subObj[1], &back = shape->subObj[2];
        test.check(pole.type == "POLE" && pole.sigSubTypeId == SignalShape::DECOR, "sub-object placed by its index");
        test.check(head.type == "HEAD1" && head.sigSubTypeId == SignalShape::SIGNAL_HEAD && head.sigSubSType == "Home3"
                       && head.optional && head.defaultt && !head.backFacing && head.faceidx == 0,
                   "sub-object type and flags, any case");
        test.check(back.backFacing && back.faceidx == 0 && back.sigSubSType == "NoAspects", "back-facing sub-object");
    }
    test.check(cfg.scriptFiles == QStringList({"sigscr.dat"}), "script files");
    bool outOfRange = false, malformed = false;
    for (const QString &w : cfg.warnings) {
        outOfRange = outOfRange || w.contains("index 7");
        malformed = malformed || w.contains("Incomplete Position");
    }
    test.check(outOfRange && malformed, "problems reported as warnings");

    SigCfg missing(temp.filePath("none.dat"));
    test.check(missing.loaded && !missing.sourceFileExists && missing.signalType.isEmpty(), "missing file is empty");

    if (!configurationPath.isEmpty()) {
        SigCfg route(configurationPath);
        test.check(route.loaded && route.sourceFileExists, "route file loads: " + configurationPath);
        int heads = 0, unresolvedTypes = 0, lights = 0, unresolvedLights = 0, withoutDefault = 0;
        for (SignalShape *s : route.signalShape)
            for (int i = 0; i < s->iSubObj; ++i)
                if (s->subObj[i].sigSubTypeId == SignalShape::SIGNAL_HEAD) {
                    ++heads;
                    if (!route.findSignalType(s->subObj[i].sigSubSType)) ++unresolvedTypes;
                }
        for (SignalType *t : route.signalType) {
            if (!t->drawStates.isEmpty() && t->defaultDrawState() == nullptr) ++withoutDefault;
            for (const SignalType::Light &l : t->lights) {
                ++lights;
                if (!route.lightsTable.contains(l.name)) ++unresolvedLights;
            }
        }
        qInfo().noquote() << "[tests:sigcfg]" << configurationPath << route.signalType.size() << "types,"
                          << route.signalShape.size() << "shapes," << heads << "heads (" << unresolvedTypes
                          << "without a type)," << lights << "lights (" << unresolvedLights << "without a colour),"
                          << withoutDefault << "types without a default draw state," << route.warnings.size() << "warnings";
        for (const QString &w : route.warnings)
            qInfo().noquote() << "[tests:sigcfg]   " << w;
        test.check(!route.signalType.isEmpty() && !route.signalShape.isEmpty(), "route file has types and shapes");
    }
    return test.finish();
}
