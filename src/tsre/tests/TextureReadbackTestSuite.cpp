#include <tsre/tests/TextureReadbackTestSuite.h>

#include <QByteArray>
#include <QDebug>
#include <cstring>
#include <tsre/Game.h>
#include <tsre/ogl/GLUU.h>
#include <tsre/renderer/rhi/RhiContext.h>
#include <tsre/renderer/rhi/RhiTextures.h>
#include <tsre/texture/DxtCodec.h>
#include <tsre/texture/Texture.h>

namespace {

// A pattern no two neighbouring pixels share, alpha varying too.
void fill(Texture &texture) {
    for (int y = 0; y < texture.height; ++y)
        for (int x = 0; x < texture.width; ++x)
            for (int c = 0; c < texture.bytesPerPixel; ++c)
                texture.imageData[(qsizetype(y) * texture.width + x) * texture.bytesPerPixel + c] =
                        static_cast<unsigned char>((x * 7 + y * 13 + c * 61) & 255);
}

QByteArray pixels(const Texture &texture) {
    if (texture.imageData == nullptr)
        return QByteArray();
    return QByteArray(reinterpret_cast<const char *>(texture.imageData), texture.imageSize);
}

// Drops the CPU pixels as an upload does, keeping the GPU texture.
void forgetPixels(Texture &texture) {
    delete[] texture.imageData;
    texture.imageData = nullptr;
    texture.editable = false;
}

}

int TsreTests::runTextureReadbackSuite(bool verbose) {
    int passed = 0;
    int failed = 0;
    auto check = [&](bool condition, const char *name) {
        if (condition) {
            ++passed;
            if (verbose)
                qInfo() << "[tests:texture-readback] PASS" << name;
        } else {
            ++failed;
            qWarning() << "[tests:texture-readback] FAIL" << name;
        }
    };
    const QString backend = Game::renderBackend;
    Game::renderBackend = "qrhi";
    RhiContext *context = RhiContext::instance();
    if (context == nullptr || context->rhi() == nullptr) {
        qWarning() << "[tests:texture-readback] no QRhi";
        Game::renderBackend = backend;
        return 1;
    }

    // An RGB texture, as overlays and most terrain textures are.
    Texture rgb(64, 32, 24, nullptr);
    fill(rgb);
    const QByteArray rgbPixels = pixels(rgb);
    check(rgb.uploadForRhi(false) && rgb.imageData == nullptr && !rgb.editable,
          "the upload frees the CPU pixels and the texture is not editable");
    rgb.setEditable();
    check(rgb.editable && pixels(rgb) == rgbPixels, "setEditable reads the RGB pixels back");
    forgetPixels(rgb);
    Texture rgbCopy(&rgb);
    check(rgbCopy.loaded && !rgbCopy.error && pixels(rgbCopy) == rgbPixels,
          "a copy reads the RGB pixels back");

    // An RGBA texture keeps its alpha.
    Texture rgba(32, 32, 32, nullptr);
    fill(rgba);
    const QByteArray rgbaPixels = pixels(rgba);
    rgba.uploadForRhi(true);
    rgba.setEditable();
    check(rgba.editable && pixels(rgba) == rgbaPixels, "setEditable reads the RGBA pixels back");

    // A block-compressed texture, read back as blocks and decoded.
    if (RhiTextures::supportsBlocks()) {
        Texture dxt(32, 16, 24, nullptr);
        fill(dxt);
        QByteArray blocks;
        QString message;
        const bool encoded = DxtCodec::encode(dxt.imageData, dxt.imageSize, dxt.width, dxt.height, 3,
                                              DxtCodec::Format::Dxt1, false, blocks, message);
        QByteArray expected;
        DxtCodec::decode(blocks, dxt.width, dxt.height, DxtCodec::Format::Dxt1, false, expected,
                         message);
        dxt.compressedData = blocks;
        dxt.compressedGLFormat = GL_COMPRESSED_RGB_S3TC_DXT1_EXT;
        check(encoded && dxt.uploadForRhi(false) && dxt.imageData == nullptr,
              "a DXT1 texture goes up compressed");
        dxt.setEditable();
        check(dxt.editable && pixels(dxt) == expected, "setEditable decodes the blocks read back");
    } else if (verbose) {
        qInfo() << "[tests:texture-readback] block-compressed textures not supported here";
    }

    // Inside a frame being recorded there is no readback: nothing breaks.
    Texture busy(16, 16, 24, nullptr);
    fill(busy);
    busy.uploadForRhi(false);
    QRhiCommandBuffer *commandBuffer = nullptr;
    bool refused = false;
    if (context->rhi()->beginOffscreenFrame(&commandBuffer) == QRhi::FrameOpSuccess) {
        busy.setEditable();
        refused = !busy.editable && busy.imageData == nullptr;
        context->rhi()->endOffscreenFrame();
    }
    check(refused, "no readback inside a frame");
    busy.setEditable();
    check(busy.editable, "the readback works after the frame");

    Game::renderBackend = backend;
    qInfo() << "[tests:texture-readback] cases=" << (passed + failed) << "passed=" << passed
            << "failed=" << failed;
    return failed == 0 ? 0 : 1;
}
