#include <tsre/tests/WaterTestSuite.h>

#include <QDebug>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>
#include <QScopedValueRollback>
#include <QThread>
#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <vector>

#include <tsre/Game.h>
#include <tsre/math3d/GLMatrix.h>
#include <tsre/ogl/GLUU.h>
#include <tsre/ogl/OglObj.h>
#include <tsre/renderer/OpenGL3Renderer.h>
#include <tsre/renderer/PlanarReflection.h>
#include <tsre/renderer/WaterWaves.h>

namespace {

// An unlit square of half-size h across the -Z axis at distance d, centred
// at height y.
std::unique_ptr<OglObj> square(float d, float h, float y, const float *colour) {
    const float corners[4][3] = {{-h, y - h, -d}, {h, y - h, -d}, {h, y + h, -d}, {-h, y + h, -d}};
    std::vector<float> vertices;
    for (int index : {0, 1, 2, 0, 2, 3})
        vertices.insert(vertices.end(), corners[index], corners[index] + 3);
    auto object = std::make_unique<OglObj>();
    object->setMaterial(colour[0], colour[1], colour[2]);
    object->init(vertices.data(), int(vertices.size()), RenderItem::V, GL_TRIANGLES);
    return object;
}

std::array<unsigned char, 4> readPixel(QOpenGLExtraFunctions *f, int x, int y) {
    std::array<unsigned char, 4> pixel{};
    f->glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel.data());
    return pixel;
}

bool matches(const std::array<unsigned char, 4> &pixel, const float *colour) {
    for (int c = 0; c < 3; ++c)
        if (std::abs(int(pixel[c]) - int(colour[c] * 255.0f)) > 40)
            return false;
    return true;
}

}

int TsreTests::runWaterGlSuite(bool verbose) {
    int passed = 0, failed = 0;
    auto check = [&](bool ok, const char *name) {
        if (ok) {
            ++passed;
            if (verbose) qInfo() << "[tests:water-gl] PASS" << name;
        } else {
            ++failed;
            qWarning() << "[tests:water-gl] FAIL" << name;
        }
    };

    // Waves: the slopes of each cascade level on average, scaled to the
    // wind's mean square slope, with squareSum beside them, seamless where
    // they repeat, running with the wind, and turning with time, short
    // waves faster than long ones.
    const int size = WaterWaves::Size;
    const size_t layer = size_t(size) * size * 4;
    WaterWaves::Wind breeze;
    breeze.speed = 3.0;
    breeze.direction = 90.0;
    WaterWaves::Field field(breeze);
    std::vector<float> waves, later;
    field.compute(5.0, waves);
    check(waves.size() == layer * WaterWaves::Cascades, "the waves have RGBA texels per cascade");
    double meanSlope = 0.0, squareSum = 0.0, alongX = 0.0, alongZ = 0.0;
    int squaresOff = 0;
    double inside = 0.0, across = 0.0;
    for (int c = 0; c < WaterWaves::Cascades; ++c)
        for (int z = 0; z < size; ++z)
            for (int x = 0; x < size; ++x) {
                const float *t = &waves[layer * c + (size_t(z) * size + x) * 4];
                meanSlope += t[0] + t[1];
                squareSum += t[2] + t[3];
                alongX += t[2];
                alongZ += t[3];
                squaresOff += std::abs(t[0] * t[0] - t[2]) > 1e-6f || std::abs(t[1] * t[1] - t[3]) > 1e-6f;
                if (c == 2) {
                    const float *next = &waves[layer * c + (size_t(z) * size + (x + 1) % size) * 4];
                    (x == size - 1 ? across : inside) += std::abs(t[0] - next[0]);
                }
            }
    const double texels = double(size) * size;
    const double target = WaterWaves::meanSquareSlope(breeze.speed);
    check(std::abs(meanSlope / texels) < 1e-3, "the waves are level on average");
    check(std::abs(squareSum / texels - target) < 0.25 * target,
          "the slopes match the wind's mean square slope");
    check(std::abs(field.cascadeSlopeVariance[0] + field.cascadeSlopeVariance[1]
                   + field.cascadeSlopeVariance[2] - target) < 1e-9
          && field.cascadeSlopeVariance[0] > 0.0 && field.cascadeSlopeVariance[2] > 0.0,
          "every cascade has waves, together as much as the wind makes");
    check(squaresOff == 0, "blue and alpha hold the squared slopes");
    check(across / size < 1.5 * inside / (double(size) * (size - 1)),
          "the waves repeat without a seam");
    check(alongX > 1.3 * alongZ, "the waves run mostly with the wind");
    WaterWaves::Field again(breeze);
    std::vector<float> same;
    again.compute(5.0, same);
    check(same == waves, "the same wind and time give the same waves");
    field.compute(5.2, later);
    auto correlation = [&](int c) {
        double ab = 0.0, aa = 0.0, bb = 0.0;
        for (size_t i = layer * c; i < layer * (c + 1); i += 4) {
            ab += double(waves[i]) * later[i];
            aa += double(waves[i]) * waves[i];
            bb += double(later[i]) * later[i];
        }
        return ab / std::sqrt(aa * bb);
    };
    check(correlation(0) > correlation(1) && correlation(1) > correlation(2)
          && correlation(2) < 0.5,
          "short waves change faster than long ones");
    WaterWaves::Wind calm;
    calm.speed = 0.0;
    WaterWaves::Field still(calm);
    WaterWaves::Wind gale;
    gale.speed = 15.0;
    WaterWaves::Field rough(gale);
    check(still.cascadeSlopeVariance[2] < rough.cascadeSlopeVariance[2],
          "stronger wind, steeper waves");
    std::vector<qfloat16> uploaded;
    quint64 serial = 0;
    check(WaterWaves::shared().update(0.0, breeze, true, serial, uploaded)
          && uploaded.size() == WaterWaves::levelOffset(WaterWaves::Levels)
          && !WaterWaves::shared().update(0.0, breeze, true, serial, uploaded),
          "frozen waves are made once, with every mipmap level");
    const qfloat16 *top = uploaded.data() + WaterWaves::levelOffset(WaterWaves::Levels - 1);
    double topMean = 0.0;
    for (int c = 0; c < WaterWaves::Cascades; ++c)
        topMean += float(top[c * 4 + 2]) + float(top[c * 4 + 3]);
    check(std::abs(topMean - target) < 0.25 * target,
          "the last mipmap level keeps the mean square slope");
    // Running: the worker delivers the waves of later frames.
    const quint64 first = serial;
    bool delivered = false;
    for (int frame = 1; frame < 200 && !delivered; ++frame) {
        WaterWaves::shared().update(frame / 60.0, breeze, false, serial, uploaded);
        delivered = serial >= first + 2;
        QThread::msleep(5);
    }
    check(delivered, "the worker computes the waves of the coming frames");

    float mirror[16];
    const float level[4] = {0.0f, 1.0f, 0.0f, -3.0f};
    PlanarReflection::mirrorMatrix(level, mirror);
    float point[3] = {1.0f, 5.0f, 2.0f}, mirrored[3];
    Vec3::transformMat4(mirrored, point, mirror);
    check(mirrored[0] == 1.0f && mirrored[1] == 1.0f && mirrored[2] == 2.0f,
          "the mirror reflects heights about the plane");
    // A river falling 2 m per 100 m along x: patches on a grid around the eye.
    std::vector<float> patches;
    for (int i = -4; i <= 4; ++i)
        for (int k = -1; k <= 1; ++k)
            patches.insert(patches.end(), {i * 64.0f, 10.0f - 0.02f * i * 64.0f, k * 64.0f, 45.0f});
    const float eye[3] = {0.0f, 30.0f, 0.0f};
    float plane[4];
    bool fitted = PlanarReflection::fitPlane(patches, eye, plane);
    float residual = 0.0f;
    for (size_t i = 0; i < patches.size(); i += 4)
        residual = std::max(residual, std::abs(plane[0] * patches[i] + plane[1] * patches[i + 1]
                                               + plane[2] * patches[i + 2] + plane[3]));
    check(fitted && plane[1] > 0.99f && residual < 0.05f,
          "a sloping river gets a tilted plane through its patches");
    float tilted[16];
    PlanarReflection::mirrorMatrix(plane, tilted);
    const float above[3] = {100.0f, 20.0f, 30.0f};
    float image[3];
    Vec3::transformMat4(image, const_cast<float *>(above), tilted);
    const float before = plane[0] * above[0] + plane[1] * above[1] + plane[2] * above[2] + plane[3];
    const float after = plane[0] * image[0] + plane[1] * image[1] + plane[2] * image[2] + plane[3];
    check(std::abs(before + after) < 1e-3f && before > 0.0f,
          "the tilted mirror puts a point at the same distance under the plane");
    std::vector<float> row;
    for (int i = -4; i <= 4; ++i)
        row.insert(row.end(), {i * 64.0f, 5.0f, 0.0f, 45.0f});
    fitted = PlanarReflection::fitPlane(row, eye, plane);
    check(fitted && std::abs(plane[1] - 1.0f) < 1e-4f && std::abs(plane[3] + 5.0f) < 1e-3f,
          "patches in one row give a level plane");
    check(!PlanarReflection::fitPlane({}, eye, plane), "no patches, no plane");
    // A river falling 2.7 % near the eye and 1.3 % in a reach 4 km away: the
    // plane follows the water near the eye.
    std::vector<float> river;
    for (int i = -3; i <= 3; ++i)
        river.insert(river.end(), {i * 128.0f, 10.0f - 0.027f * i * 128.0f, 0.0f, 90.0f});
    for (int i = 0; i < 20; ++i)
        river.insert(river.end(), {4000.0f + i * 128.0f, -90.0f - 0.013f * i * 128.0f, -800.0f, 90.0f});
    const float bank[3] = {-41.0f, 12.0f, 0.0f};
    fitted = PlanarReflection::fitPlane(river, bank, plane);
    const float waterHeight = -(plane[0] * bank[0] + plane[2] * bank[2] + plane[3]) / plane[1];
    check(fitted && std::abs(waterHeight - (10.0f + 0.027f * 41.0f)) < 0.05f,
          "far reaches of a river do not move the plane at the camera");

    QOpenGLContext context;
    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CompatibilityProfile);
    context.setFormat(format);
    if (!context.create()) return 2;
    QOffscreenSurface surface;
    surface.setFormat(context.format());
    surface.create();
    if (!context.makeCurrent(&surface)) return 2;
    QOpenGLExtraFunctions *f = context.extraFunctions();
    QScopedValueRollback<int> shadows(Game::shadowsEnabled, 0);
    QScopedValueRollback<bool> frozen(Game::animationFrozen, true);

    GLUU *gluu = GLUU::get();
    gluu->initShader();
    Shader *water = gluu->shaders.value("StandardFogWater");
    check(water != nullptr && water->isLinked() && water->waterLayers >= 0
          && water->waterReflectionView >= 0 && water->waterTime >= 0,
          "the water program links with its uniforms");
    check(gluu->waterVariant(gluu->shaders["StandardFog"]) == water
          && gluu->waterVariant(gluu->shaders["StandardFast"]) == water
          && gluu->waterVariant(gluu->shaders["Selection"]) == gluu->shaders["Selection"],
          "the main and fast programs draw water with it; selection does not");
    check(GLUU::animationSeconds() == 0.0f, "frozen animation stands at zero");
    check(water->waterWind >= 0, "the water program has the wind");
    check(water->waterScene >= 0 && water->waterDepthRange >= 0
          && water->uniformLocation("waterSceneColor") >= 0
          && water->uniformLocation("waterSceneDepth") >= 0,
          "the water program reads the frame and depth under the water");
    WaterWaveTexture waveTexture;
    GLint bound = 0;
    check(waveTexture.bind(f, 15, 0.0, breeze, true), "the wave cascades are created");
    f->glActiveTexture(GL_TEXTURE15);
    f->glGetIntegerv(GL_TEXTURE_BINDING_2D_ARRAY, &bound);
    f->glActiveTexture(GL_TEXTURE0);
    check(bound != 0 && f->glGetError() == GL_NO_ERROR, "the wave cascades are bound on unit 15");

    // A red square above the plane y = -1 and a green one below it, nearer.
    // Mirrored, red shows under the horizon; green would show above it but
    // lies below the plane, so the clip plane removes it.
    gluu->currentShader = gluu->shaders["StandardFog"];
    gluu->fogDensity = 0;
    gluu->currentShader->bind();
    const float red[3] = {1, 0, 0}, green[3] = {0, 1, 0}, black[3] = {0, 0, 0};
    std::vector<std::unique_ptr<OglObj>> squares;
    squares.push_back(square(10.0f, 2.0f, 1.0f, red));
    squares.push_back(square(6.0f, 2.0f, -3.0f, green));
    OpenGL3Renderer renderer;
    renderer.resetFrame();
    const float origin[3] = {0.0f, 0.0f, 0.0f};
    renderer.setViewPosition(origin);
    Mat4::identity(renderer.transform());
    for (auto &object : squares)
        object->pushRenderItem(renderer);

    PlanarReflection reflection;
    const int side = 32;
    check(reflection.ensure(side, side) && reflection.levels() == 6,
          "the reflection target is created with mipmaps");
    float view[16], mirroredView[16], projection[16];
    // The camera at the origin looking along -Z, +Y up.
    Mat4::identity(view);
    const float waterPlane[4] = {0.0f, 1.0f, 0.0f, 1.0f};
    PlanarReflection::mirrorMatrix(waterPlane, mirror);
    Mat4::multiply(mirroredView, view, mirror);
    Mat4::perspective(projection, float(M_PI) / 2.0f, 1.0f, 0.1f, 100.0f);
    auto draw = [&](bool clip) {
        reflection.begin(black);
        const float plane[4] = {0.0f, 1.0f, 0.0f, 1.0f};
        const float keep[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        std::copy(clip ? plane : keep, (clip ? plane : keep) + 4, gluu->clipPlane);
        if (clip)
            f->glEnable(GL_CLIP_DISTANCE0);
        Mat4::multiply(gluu->pMatrix, projection, mirroredView);
        Mat4::multiply(gluu->fMatrix, projection, mirroredView);
        Mat4::identity(gluu->mvMatrix);
        Mat4::identity(gluu->objStrMatrix);
        gluu->setMatrixUniforms();
        renderer.renderPassesRetained(Renderer::PASS_TERRAIN, Renderer::PASS_WATER);
        const std::array<unsigned char, 4> below = readPixel(f, side / 2, 10);
        const std::array<unsigned char, 4> above = readPixel(f, side / 2, 21);
        f->glDisable(GL_CLIP_DISTANCE0);
        std::copy(keep, keep + 4, gluu->clipPlane);
        reflection.end();
        return std::make_pair(below, above);
    };
    const auto unclipped = draw(false);
    const auto clipped = draw(true);
    check(matches(clipped.first, red), "the mirror image of an object above the water is below the horizon");
    check(matches(unclipped.second, green), "without clipping, geometry under the water shows");
    check(matches(clipped.second, black), "the clip plane removes geometry under the water");

    squares.clear();
    check(f->glGetError() == GL_NO_ERROR, "no GL errors");
    context.doneCurrent();
    qInfo() << "[tests:water-gl] cases=" << (passed + failed)
            << "passed=" << passed << "failed=" << failed;
    return failed == 0 ? 0 : 1;
}
