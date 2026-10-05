#include <tsre/tests/EnvironmentMapTestSuite.h>

#include <QDebug>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>
#include <QScopedValueRollback>
#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <vector>

#include <tsre/Game.h>
#include <tsre/math3d/GLMatrix.h>
#include <tsre/ogl/GLUU.h>
#include <tsre/ogl/OglObj.h>
#include <tsre/renderer/EnvironmentMap.h>
#include <tsre/renderer/OpenGL3Renderer.h>

namespace {

// An unlit square of half-size h facing the origin, centred on axis at
// distance d, shifted by (du, dv) along the square's own axes.
std::unique_ptr<OglObj> marker(int axis, float sign, float d, float h,
                               float du, float dv, const float *colour) {
    float corners[4][3];
    const int a = (axis + 1) % 3, b = (axis + 2) % 3;
    const float offsets[4][2] = {{-h, -h}, {h, -h}, {h, h}, {-h, h}};
    for (int i = 0; i < 4; ++i) {
        corners[i][axis] = sign * d;
        corners[i][a] = offsets[i][0] + (a == 1 ? dv : du);
        corners[i][b] = offsets[i][1] + (b == 1 ? dv : du);
    }
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

int TsreTests::runEnvironmentMapGlSuite(bool verbose) {
    int passed = 0, failed = 0;
    auto check = [&](bool ok, const char *name) {
        if (ok) {
            ++passed;
            if (verbose) qInfo() << "[tests:environment-map-gl] PASS" << name;
        } else {
            ++failed;
            qWarning() << "[tests:environment-map-gl] FAIL" << name;
        }
    };
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

    EnvironmentMap rotation;
    check(rotation.nextFaces(4) == QVector<int>({0, 1, 2, 3})
          && rotation.nextFaces(4) == QVector<int>({4, 5, 0, 1})
          && rotation.nextFaces(9).size() == EnvironmentMap::FaceCount,
          "faces are scheduled round robin");

    GLUU *gluu = GLUU::get();
    gluu->initShader();
    gluu->currentShader = gluu->shaders["StandardFog"];
    gluu->fogDensity = 0;
    gluu->currentShader->bind();

    // One colour per direction, and a white marker above the +Z square.
    const float colours[6][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1},
                                 {1, 1, 0}, {0, 1, 1}, {1, 0, 1}};
    const float white[3] = {1, 1, 1};
    std::vector<std::unique_ptr<OglObj>> markers;
    for (int face = 0; face < EnvironmentMap::FaceCount; ++face)
        markers.push_back(marker(face / 2, face % 2 ? -1.0f : 1.0f, 10.0f, 10.0f,
                                 0.0f, 0.0f, colours[face]));
    markers.push_back(marker(2, 1.0f, 9.0f, 1.5f, 0.0f, 5.0f, white));

    OpenGL3Renderer renderer;
    renderer.resetFrame();
    const float eye[3] = {0.0f, 0.0f, 0.0f};
    renderer.setViewPosition(eye);
    Mat4::identity(renderer.transform());
    for (auto &object : markers)
        object->pushRenderItem(renderer);

    EnvironmentMap environment;
    const int size = 32;
    check(environment.ensure(size) && environment.texture() != 0 && !environment.complete(),
          "the cube and its framebuffer are created");
    const float black[3] = {0, 0, 0};
    float view[16], projection[16];
    bool centres = true, up = false;
    for (int face = 0; face < EnvironmentMap::FaceCount; ++face) {
        environment.beginFace(face, black);
        check(f->glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE,
              "a face framebuffer is complete");
        EnvironmentMap::faceView(face, eye, view);
        EnvironmentMap::faceProjection(0.1f, 100.0f, projection);
        Mat4::multiply(gluu->pMatrix, projection, view);
        Mat4::multiply(gluu->fMatrix, projection, view);
        Mat4::identity(gluu->mvMatrix);
        Mat4::identity(gluu->objStrMatrix);
        gluu->setMatrixUniforms();
        renderer.renderPassesRetained(Renderer::PASS_TERRAIN, Renderer::PASS_WATER);
        centres &= matches(readPixel(f, size / 2, size / 2), colours[face]);
        if (face == 4) {
            // A face's first texture row is the +Y side for the side faces,
            // and the framebuffer's first row is the face's first row.
            up = matches(readPixel(f, size / 2, size / 4), white)
                    && matches(readPixel(f, size / 2, size * 3 / 4), colours[face]);
        }
    }
    environment.endFaces(0);
    check(centres, "each face looks along its own axis");
    check(up, "face rows follow the cube map convention (first row is +Y)");
    check(environment.complete(), "rendering every face completes the cube");

    EnvironmentMap warehouse;
    check(warehouse.fillWarehouse(64) && warehouse.complete(), "the warehouse fills all faces");
    GLuint readBuffer = 0;
    f->glGenFramebuffers(1, &readBuffer);
    f->glBindFramebuffer(GL_FRAMEBUFFER, readBuffer);
    auto levelStats = [&](unsigned int texture, int level, int face, int &bright, double &mean,
                          double &spread) {
        const int side = 64 >> level;
        f->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                  GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, texture, level);
        std::vector<unsigned char> pixels(size_t(side) * side * 4);
        f->glReadPixels(0, 0, side, side, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        double sum = 0, squares = 0;
        bright = 0;
        for (size_t i = 0; i < pixels.size(); i += 4) {
            sum += pixels[i + 1];
            squares += double(pixels[i + 1]) * pixels[i + 1];
            bright += pixels[i + 1] > 240;
        }
        mean = sum / (side * side);
        spread = std::sqrt(std::max(0.0, squares / (side * side) - mean * mean));
    };
    auto faceStats = [&](int face, int &bright, double &mean, double &spread) {
        levelStats(warehouse.texture(), 0, face, bright, mean, spread);
    };
    int bright = 0;
    double mean = 0, spread = 0;
    faceStats(2, bright, mean, spread);
    check(bright > 20 && mean < 110, "the ceiling is dark with bright lamps");
    faceStats(3, bright, mean, spread);
    check(bright == 0 && mean > 70 && mean < 120, "the floor is grey concrete");
    faceStats(4, bright, mean, spread);
    check(mean > 100 && mean < 180 && spread > 10, "walls are patterned brick");

    // The prefiltered cube: level 0 is the sharp cube, the last level the
    // roughest lobe: smooth, and brighter than the dark ceiling alone because
    // it also gathers the walls and windows.
    const int last = warehouse.levels() - 1;
    int rawBright = 0, sharpBright = 0, roughBright = 0;
    double rawMean = 0, rawSpread = 0, sharpMean = 0, sharpSpread = 0, roughMean = 0, roughSpread = 0;
    faceStats(2, rawBright, rawMean, rawSpread);
    levelStats(warehouse.prefilteredTexture(), 0, 2, sharpBright, sharpMean, sharpSpread);
    levelStats(warehouse.prefilteredTexture(), last, 2, roughBright, roughMean, roughSpread);
    check(warehouse.levels() == 5 && warehouse.prefilteredTexture() != 0,
          "the prefiltered cube stops at 4 x 4 texels");
    check(std::abs(sharpMean - rawMean) < 2.0 && sharpBright > 20,
          "the first prefiltered level is the sharp cube");
    check(roughBright == 0 && roughSpread < rawSpread * 0.5
          && roughMean > rawMean && roughMean < 200.0,
          "the roughest level is blurred over the surroundings");
    f->glBindFramebuffer(GL_FRAMEBUFFER, 0);
    f->glDeleteFramebuffers(1, &readBuffer);

    markers.clear();
    environment.release();
    warehouse.release();
    check(f->glGetError() == GL_NO_ERROR, "no GL errors");
    context.doneCurrent();
    qInfo() << "[tests:environment-map-gl] cases=" << (passed + failed)
            << "passed=" << passed << "failed=" << failed;
    return failed == 0 ? 0 : 1;
}
