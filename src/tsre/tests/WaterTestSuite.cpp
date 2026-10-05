#include <tsre/tests/WaterTestSuite.h>

#include <QDebug>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>
#include <QScopedValueRollback>
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
#include <tsre/renderer/WaterNormalMap.h>

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

    // Wave map: level on average, full range, squares of the slopes, and
    // no seam where it repeats.
    const int size = WaterNormalMap::Size;
    const std::vector<unsigned char> map = WaterNormalMap::generate(size);
    double mean[2] = {0, 0};
    int lowest = 255, highest = 0, squaresOff = 0;
    double inside = 0, across = 0;
    for (int z = 0; z < size; ++z)
        for (int x = 0; x < size; ++x) {
            const unsigned char *t = &map[(size_t(z) * size + x) * 4];
            for (int c = 0; c < 2; ++c) {
                mean[c] += t[c];
                lowest = std::min<int>(lowest, t[c]);
                highest = std::max<int>(highest, t[c]);
                const double slope = t[c] / 255.0 * 2.0 - 1.0;
                squaresOff += std::abs(slope * slope * 255.0 - t[2 + c]) > 3.0;
            }
            const unsigned char *next = &map[(size_t(z) * size + (x + 1) % size) * 4];
            (x == size - 1 ? across : inside) += std::abs(int(t[0]) - int(next[0]));
        }
    mean[0] /= double(size) * size;
    mean[1] /= double(size) * size;
    check(map.size() == size_t(size) * size * 4, "the wave map has RGBA texels");
    check(std::abs(mean[0] - 127.5) < 4.0 && std::abs(mean[1] - 127.5) < 4.0,
          "the waves are level on average");
    check(lowest <= 1 || highest >= 254, "the largest slope uses the full range");
    check(squaresOff == 0, "blue and alpha hold the squared slopes");
    check(across / size < 1.5 * inside / (double(size) * (size - 1)),
          "the wave map repeats without a seam");

    float mirror[16];
    PlanarReflection::mirrorMatrix(3.0f, mirror);
    float point[3] = {1.0f, 5.0f, 2.0f}, mirrored[3];
    Vec3::transformMat4(mirrored, point, mirror);
    check(mirrored[0] == 1.0f && mirrored[1] == 1.0f && mirrored[2] == 2.0f,
          "the mirror reflects heights about the plane");

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
    WaterNormalMap waves;
    GLint bound = 0;
    check(waves.bind(f, 15), "the wave map is created");
    f->glActiveTexture(GL_TEXTURE15);
    f->glGetIntegerv(GL_TEXTURE_BINDING_2D, &bound);
    f->glActiveTexture(GL_TEXTURE0);
    check(bound != 0, "the wave map is bound on unit 15");

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
    const float eye[3] = {0.0f, 0.0f, 0.0f};
    renderer.setViewPosition(eye);
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
    PlanarReflection::mirrorMatrix(-1.0f, mirror);
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
        reflection.end(0);
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
