#include "Cab3DTestSuite.h"
#include "TokenTestSupport.h"
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <cmath>
#include <tsre/GameObj.h>
#include <tsre/camera/CameraFree.h>
#include <tsre/trains/Consist.h>
#include <tsre/trains/Eng.h>

namespace {

// As the ZSSKC183 writes its cabs: the rear one naming a shape that is not
// there, two alternate viewpoints, and an alternate block before any cab,
// which Open Rails leaves out.
const char *const Fixture = R"(SIMISA@@@@@@@@@@JINX0D0t______

Wagon ( Cab
	Type ( Engine )
	WagonShape ( cab.s )
	Size ( 3 4 18 )
	ORTSAlternate3DCabViewPoints (
		ORTSAlternate3DCabViewPoint ( ORTS3DCabFile ( "Early.s" ) ORTS3DCabHeadPos ( 0 0 0 ) )
	)
	ORTS3DCab (
		ORTS3DCabFile ( "Cab.s" )
		ORTS3DCabHeadPos ( 0.6 290cm 7.3 )
		RotationLimit ( 40 60 0 )
		StartDirection ( 12 0 0 )
	)
	ORTS3DCab (
		ORTS3DCabFile ( "Missing.s" )
		ORTS3DCabHeadPos ( -0.6 2.9 -7.3 )
		RotationLimit ( 40 60 0 )
		StartDirection ( 12 180 0 )
	)
	ORTSAlternate3DCabViewPoints (
		ORTSAlternate3DCabViewPoint (
			ORTS3DCabFile ( "Cab.s" )
			ORTS3DCabHeadPos ( -0.6 2.9 7.3 )
			StartDirection ( 12 40 0 )
		)
		ORTSAlternate3DCabViewPoint (
			ORTS3DCabFile ( "Cab.s" )
			ORTS3DCabHeadPos ( 0.6 2.9 -7.3 )
			StartDirection ( 12 220 0 )
		)
	)
)

Engine ( Cab
	Wagon ( Cab )
	Type ( Electric )
	Name ( "3D cab test" )
)
)";

// A cab at its front only.
const char *const FrontFixture = R"(SIMISA@@@@@@@@@@JINX0D0t______

Wagon ( Front
	Type ( Engine )
	WagonShape ( front.s )
	ORTS3DCab ( ORTS3DCabFile ( "Cab.s" ) ORTS3DCabHeadPos ( 0.5 3 8 ) StartDirection ( 0 -30 0 ) )
)
)";

const char *const NoCabFixture = R"(SIMISA@@@@@@@@@@JINX0D0t______

Wagon ( Plain
	Type ( Engine )
	WagonShape ( plain.s )
)
)";

bool near(float a, float b, float tolerance = 1e-4f) { return std::fabs(a - b) < tolerance; }

const float Degree = float(M_PI / 180.0);

// Rides at (100, 50, 200) on tile (0, 0), its frame turned `yaw`, with the
// mount it is given (none when `hasMount` is false).
struct Ridden : GameObj {
    CameraMount mount;
    bool hasMount = true;
    float yaw = 0.0f;
    int attached = 0, detached = 0;
    void getCameraPosition(float *out) override {
        const float place[8] = {0.0f, 0.0f, 100.0f, 50.0f, 200.0f, yaw, 0.0f, 0.0f};
        std::copy(place, place + 8, out);
    }
    bool getCameraMount(CameraMount &out) override {
        if (hasMount)
            out = mount;
        return hasMount;
    }
    void cameraAttached(bool on) override { ++(on ? attached : detached); }
};

}

int TsreTests::runCab3DSuite(bool verbose) {
    TokenTest::Suite test{"[tests:cab3d]", verbose};
    QTemporaryDir temp;
    QDir(temp.path()).mkpath("CABVIEW3D");
    auto write = [&](const QString &name, const char *text) {
        QFile file(temp.filePath(name));
        return file.open(QIODevice::WriteOnly) && file.write(text) >= 0;
    };
    test.check(write("cab.eng", Fixture) && write("front.eng", FrontFixture) && write("plain.eng", NoCabFixture)
                   && write("CABVIEW3D/Cab.s", ""),
               "write fixtures");

    Eng eng(temp.path(), "cab.eng");
    eng.flip = 0;
    test.check(eng.loaded == 1 && eng.definition->cab3D != nullptr, "the fixture loads with a 3D cab");
    if (eng.definition->cab3D == nullptr)
        return test.finish();
    const Eng::Cab3D &cab = *eng.definition->cab3D;
    test.check(cab.shapes == QStringList({"Cab.s", "Missing.s", "Cab.s", "Cab.s"}) && cab.viewpoints.size() == 4,
               "both cabs and the alternate viewpoints after them; the block before any cab left out");
    if (cab.viewpoints.size() != 4)
        return test.finish();
    const Eng::Cab3D::Viewpoint &front = cab.viewpoints[0];
    test.check(near(front.position[0], 0.6f) && near(front.position[1], 2.9f) && near(front.position[2], 7.3f)
                   && near(front.limit[0], 40.0f) && near(front.limit[1], 60.0f) && near(front.start[0], 12.0f)
                   && near(front.start[1], 0.0f),
               "the head's place (units read), rotation limit and start direction");
    test.check(near(cab.viewpoints[2].position[0], -0.6f) && near(cab.viewpoints[2].start[1], 40.0f)
                   && near(cab.viewpoints[2].limit[1], 0.0f),
               "an alternate viewpoint, free to turn");
    test.check(eng.cab3DShapePath() == temp.filePath("CABVIEW3D/Cab.s"),
               "the cab's shape: the last one named that is in CABVIEW3D");
    test.check(eng.cab3DViewpoint(false) == 0 && eng.cab3DViewpoint(true) == 1,
               "the first viewpoint looking ahead and the first looking back");

    CameraMount mount;
    test.check(eng.cab3DMount(0, mount) && near(mount.look[0], float(M_PI)) && near(mount.look[1], -12.0f * Degree)
                   && near(mount.turnLimit[0], 60.0f * Degree) && near(mount.turnLimit[1], 40.0f * Degree)
                   && near(mount.position[0], 0.0f) && near(mount.position[1], 0.0f) && near(mount.position[2], 0.0f)
                   && near(mount.moveBox[0], Eng::CabMove[0]) && near(mount.moveStep, Eng::CabMoveStep) && mount.id == 1,
               "the front cab's mount: at the head, looking ahead (-z) and 12 degrees down, turning 60 and 40");
    test.check(eng.cab3DMount(1, mount) && near(std::remainder(mount.look[0], float(2.0 * M_PI)), 0.0f) && mount.id == 2,
               "the rear cab looks back along the car (+z)");
    test.check(eng.cab3DMount(2, mount) && near(std::sin(mount.look[0]), std::sin(40.0f * Degree))
                   && near(std::cos(mount.look[0]), -std::cos(40.0f * Degree)) && near(mount.turnLimit[0], 0.0f),
               "40 degrees round towards the shape's +x: to the right; no limit, free");
    eng.flip = 1;
    test.check(eng.cab3DMount(1, mount) && near(std::cos(mount.look[0]), -1.0f)
                   && eng.cab3DMount(0, mount) && near(std::cos(mount.look[0]), 1.0f),
               "flipped: the rear cab looks ahead, the front one back");
    test.check(eng.cab3DMount(3, mount) && near(std::sin(mount.look[0]), -std::sin(220.0f * Degree))
                   && near(std::cos(mount.look[0]), std::cos(220.0f * Degree)),
               "flipped: the shape's +x is on the left");
    eng.flip = 0;
    test.check(!eng.cab3DMount(4, mount) && !eng.cab3DMount(-1, mount), "no mount at a viewpoint it has not");

    Eng frontOnly(temp.path(), "front.eng");
    frontOnly.flip = 0;
    test.check(frontOnly.cab3DViewpoint(false) == 0 && frontOnly.cab3DViewpoint(true) == -1
                   && frontOnly.cab3DMount(0, mount) && near(mount.turnLimit[0], 0.0f)
                   && near(mount.look[0], float(M_PI) + 30.0f * Degree),
               "a cab at the front only: none looking back");
    Eng plain(temp.path(), "plain.eng");
    plain.flip = 0;
    test.check(plain.definition->cab3D == nullptr && plain.cab3DViewpoint(false) == -1 && plain.cab3DShapePath().isEmpty()
                   && !plain.cab3DMount(0, mount),
               "no 3D cab");

    // A consist riding in a vehicle's cab.
    {
        Consist consist;
        Consist::EngItem first, second;
        first.engPointer = &plain;
        second.engPointer = &eng;
        consist.engItems << first << second;
        test.check(!consist.rideCab3D(0, false) && !consist.getCameraMount(mount), "a vehicle without a cab");
        test.check(consist.rideCab3D(1, true) && consist.cameraVehicle == 1 && consist.getCameraMount(mount)
                       && mount.id == 2 + 1024,
                   "the second vehicle's rear cab: the camera on it, its mount told apart from another vehicle's");
        consist.cameraVehicle = 0;
        test.check(!consist.getCameraMount(mount), "the camera on another vehicle: no mount");
        consist.cameraVehicle = 1;
        consist.cameraAttached(false);
        test.check(!consist.getCameraMount(mount), "the camera gone: out of the cab");
        consist.engItems.clear();
    }

    // The riding camera keeps to the mount.
    float tile[2] = {0.0f, 0.0f};
    CameraFree camera(tile);
    Ridden ridden;
    ridden.mount.position[1] = 0.2f;
    ridden.mount.look[1] = -0.2f;
    ridden.mount.moveBox[0] = ridden.mount.moveBox[2] = 0.5f;
    ridden.mount.moveBox[1] = 0.3f;
    ridden.mount.moveStep = 0.05f;
    ridden.mount.turnLimit[0] = 1.0f;
    ridden.mount.turnLimit[1] = 0.7f;
    ridden.mount.id = 1;
    camera.setCameraObject(&ridden);
    camera.update(30.0f);
    const float *at = camera.getPos();
    test.check(ridden.attached == 1 && near(at[0], 100.0f) && near(at[1], 50.2f) && near(at[2], 200.0f)
                   && near(camera.getRotX(), float(M_PI)) && near(camera.getRotY(), -0.2f),
               "it starts at the mount, looking its way");
    for (int i = 0; i < 4; ++i)
        camera.moveForward(30.0f);
    camera.update(30.0f);
    at = camera.getPos();
    test.check(near(at[2], 200.0f - 4 * 0.05f * std::cos(0.2f), 1e-3f), "it moves by the mount's step");
    for (int i = 0; i < 100; ++i)
        camera.moveForward(30.0f);
    camera.update(30.0f);
    at = camera.getPos();
    test.check(near(at[2], 199.5f) && near(at[1], 49.9f) && near(at[0], 100.0f), "no farther than the mount allows");
    camera.setPlayerRot(float(M_PI) + 3.0f, 2.0f);
    camera.update(30.0f);
    test.check(near(camera.getRotX(), float(M_PI) + 1.0f) && near(camera.getRotY(), -0.2f + 0.7f),
               "it turns no farther than the mount allows");
    camera.setPlayerRot(float(M_PI) - 2.0f * float(M_PI) + 0.5f, -2.0f);
    camera.update(30.0f);
    test.check(near(std::remainder(camera.getRotX() - float(M_PI) - 0.5f, float(2.0 * M_PI)), 0.0f)
                   && near(camera.getRotY(), -0.9f),
               "a turn a full round away counts as the same");
    ridden.mount.id = 2;
    ridden.mount.position[2] = 3.0f;
    camera.update(30.0f);
    at = camera.getPos();
    test.check(near(at[2], 203.0f) && near(camera.getRotX(), float(M_PI)), "another place: it starts there");
    ridden.yaw = float(M_PI) / 2.0f;
    camera.update(30.0f);
    at = camera.getPos();
    test.check(near(at[0], 103.0f, 1e-3f) && near(at[2], 200.0f, 1e-3f), "the place turns with the object");
    ridden.hasMount = false;
    camera.update(30.0f);
    for (int i = 0; i < 10; ++i)
        camera.moveForward(30.0f);
    camera.update(30.0f);
    at = camera.getPos();
    test.check(std::fabs(at[0] - 103.0f) > 1.0f, "without a mount it moves freely");
    camera.releaseObject();
    test.check(ridden.detached == 1, "released: the object is told");
    return test.finish();
}
