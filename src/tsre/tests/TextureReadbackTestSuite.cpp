#include <tsre/tests/TextureReadbackTestSuite.h>

#include <QByteArray>
#include <QDebug>
#include <cmath>
#include <cstring>
#include <tsre/Game.h>
#include <tsre/ogl/GLUU.h>
#include <tsre/renderer/rhi/RhiContext.h>
#include <tsre/renderer/rhi/RhiTextures.h>
#include <tsre/texture/DxtCodec.h>
#include <tsre/texture/Texture.h>
#include <memory>

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

    // A display-only texture (TexLib::Display): its loader prepares its GPU
    // levels and drops the source; the upload makes the texture from them.
    // It is never edited, its pixels read into a copy.
    RhiTextures::supportsBlocks(); // As the renderer, once made, for the loaders.
    Texture shown(32, 32, 32, nullptr);
    fill(shown);
    const QByteArray shownPixels = pixels(shown);
    shown.displayOnly = true;
    check(shown.prepareForRhi() && shown.prepared && shown.imageData == nullptr && !shown.editable,
          "a display-only texture is prepared, its source dropped");
    QByteArray read;
    check(shown.readPixels(read) && read == shownPixels, "its prepared pixels are read into a copy");
    check(shown.uploadForRhi(true) && shown.glLoaded && !shown.prepared && shown.preparedLevels.isEmpty(),
          "the upload makes the GPU texture from the prepared levels");
    shown.setEditable();
    check(!shown.editable && shown.imageData == nullptr, "a display-only texture is never edited");
    read.clear();
    check(shown.readPixels(read) && read == shownPixels, "its pixels are read back from the GPU into a copy");
    if (RhiTextures::supportsBlocks()) {
        Texture blocksShown(32, 16, 24, nullptr);
        fill(blocksShown);
        QByteArray blocks, expected;
        QString message;
        DxtCodec::encode(blocksShown.imageData, blocksShown.imageSize, blocksShown.width, blocksShown.height, 3,
                         DxtCodec::Format::Dxt1, false, blocks, message);
        DxtCodec::decode(blocks, blocksShown.width, blocksShown.height, DxtCodec::Format::Dxt1, false,
                         expected, message);
        blocksShown.compressedData = blocks;
        blocksShown.compressedGLFormat = GL_COMPRESSED_RGB_S3TC_DXT1_EXT;
        blocksShown.displayOnly = true;
        check(blocksShown.prepareForRhi() && blocksShown.preparedFormat == GL_COMPRESSED_RGB_S3TC_DXT1_EXT
                      && blocksShown.compressedData.isEmpty() && blocksShown.imageData == nullptr,
              "a display-only DXT1 texture is prepared as its blocks");
        read.clear();
        check(blocksShown.readPixels(read) && read == expected, "its prepared blocks are read decoded");
        check(blocksShown.uploadForRhi(false) && blocksShown.gpuInternalFormat == GL_COMPRESSED_RGB_S3TC_DXT1_EXT,
              "and go up compressed");
    }

    // An edit (terrain painting) goes to the GPU and keeps the CPU pixels for
    // the next one; the next replaces the texture in place.
    Texture painted(64, 32, 24, nullptr);
    fill(painted);
    painted.uploadForRhi(false);
    painted.setEditable();
    auto gpuPixel = [&](int pixel) {
        QByteArray data;
        if (RhiTextures::readBack(painted.tex[0], data) != RhiTextures::Contents::Rgba8
                || data.size() <= pixel * 4 + 2)
            return -1;
        return int(static_cast<unsigned char>(data[pixel * 4]));
    };
    painted.imageData[0] = 7;
    painted.update();
    const unsigned int first = painted.tex[0];
    check(painted.imageData != nullptr && painted.editable && painted.glLoaded && gpuPixel(0) == 7,
          "an edited texture's pixels go to the GPU, the CPU pixels kept");
    painted.imageData[3] = 9;
    painted.update();
    check(painted.tex[0] == first && gpuPixel(1) == 9 && gpuPixel(0) == 7, "the next edit replaces it in place");

    // Mipmaps on shape textures (Texture::shapeMipmaps): levels by the
    // source, made as the loader prepares the texture.
    {
        const bool mipmapsWere = Texture::shapeMipmaps.load();
        const int sourceWas = Texture::mipmapSource.load();
        auto toLinear = [](int c) {
            const double v = c / 255.0;
            return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
        };
        auto toSrgb = [](double l) {
            const double c = l <= 0.0031308 ? l * 12.92 : 1.055 * std::pow(l, 1.0 / 2.4) - 0.055;
            return int(c * 255.0 + 0.5);
        };
        // Level 1's first texel from level 0's top left 2x2, as light.
        auto meanOfFirst = [&](const QByteArray &level0, int width, int channel) {
            const auto *p = reinterpret_cast<const unsigned char *>(level0.constData());
            double sum = 0.0;
            for (int i : {0, 1, width, width + 1})
                sum += toLinear(p[i * 4 + channel]);
            return toSrgb(sum / 4.0);
        };
        auto shapeTexture = [&](int side) {
            auto *t = new Texture(side, side, 32, nullptr);
            fill(*t);
            t->displayOnly = true;
            return t;
        };

        Texture::shapeMipmaps = false;
        std::unique_ptr<Texture> off(shapeTexture(16));
        check(off->prepareForRhi() && off->preparedLevels.size() == 1, "mipmaps off: level 0 alone, as before");

        Texture::shapeMipmaps = true;
        Texture::mipmapSource = int(Texture::MipmapSource::Regenerate);
        std::unique_ptr<Texture> made(shapeTexture(16));
        const QByteArray made0 = pixels(*made);
        const bool madeOk = made->prepareForRhi() && made->preparedFormat == 0 && made->preparedLevels.size() == 5;
        const auto *level1 = reinterpret_cast<const unsigned char *>(madeOk ? made->preparedLevels[1].constData() : nullptr);
        check(madeOk && std::abs(level1[0] - meanOfFirst(made0, 16, 0)) <= 1
                      && std::abs(level1[1] - meanOfFirst(made0, 16, 1)) <= 1,
              "made again: a full chain, colours mixed as light");

        // A cutout keeps its coverage (texels passing half alpha) level by level.
        std::unique_ptr<Texture> cutout(shapeTexture(64));
        for (int i = 0; i < 64 * 64; ++i)
            cutout->imageData[i * 4 + 3] = ((i * 2654435761u) >> 24) % 10 < 3 ? 255 : 0;
        cutout->classifyAlpha();
        auto coverage = [](const QByteArray &rgba) {
            int passed = 0;
            for (qsizetype i = 0; i < rgba.size() / 4; ++i)
                passed += static_cast<unsigned char>(rgba[i * 4 + 3]) >= 131 ? 1 : 0;  // the shaders' 0.51
            return double(passed) / double(rgba.size() / 4);
        };
        const QByteArray cutout0 = pixels(*cutout);
        bool kept = cutout->prepareForRhi() && cutout->preparedLevels.size() == 7;
        // A 2x2 mean of 0 and 255 has five values: the coverage moves in
        // steps (30% here: 31, 38, 34% at levels 1 to 3; plain means keep a
        // few percent).
        for (int k = 1; kept && k <= 3; ++k)
            kept = std::abs(coverage(cutout->preparedLevels[k]) - coverage(cutout0)) < 0.1;
        check(kept, "made again: an alpha-tested texture keeps its coverage");

        // Improved on a cutout whose own levels thinned out (0/255 alpha, a
        // texel passing only where most of the 2x2 above did): ours instead,
        // its coverage kept; and none scaled under the shaders' 0.51.
        {
            std::unique_ptr<Texture> thinned(shapeTexture(32));
            for (int i = 0; i < 32 * 32; ++i)
                thinned->imageData[i * 4 + 3] = ((i * 2654435761u) >> 24) % 10 < 3 ? 255 : 0;
            thinned->classifyAlpha();
            const QByteArray thinned0 = pixels(*thinned);
            QByteArray above = thinned0;
            for (int side = 16; side >= 1; side /= 2) {
                QByteArray level(side * side * 4, Qt::Uninitialized);
                const int big = side * 2;
                for (int y = 0; y < side; ++y)
                    for (int x = 0; x < side; ++x)
                        for (int c = 0; c < 4; ++c) {
                            int sum = 0;
                            for (int dy = 0; dy < 2; ++dy)
                                for (int dx = 0; dx < 2; ++dx)
                                    sum += static_cast<unsigned char>(above[((2 * y + dy) * big + 2 * x + dx) * 4 + c]);
                            level[(y * side + x) * 4 + c] = char(c == 3 ? (sum >= 3 * 255 ? 255 : 0) : (sum + 2) / 4);
                        }
                thinned->sourceMipmaps.push_back({side, side, 0, level});
                above = level;
            }
            Texture::mipmapSource = int(Texture::MipmapSource::Improve);
            bool dense = thinned->prepareForRhi() && thinned->preparedLevels.size() == 6;
            for (int k = 1; dense && k <= 2; ++k)
                dense = std::abs(coverage(thinned->preparedLevels[k]) - coverage(thinned0)) < 0.1;
            check(dense, "improved: thinned-out levels of a cutout made again, its coverage kept");
        }

        // Improved: the file's level 1 (a fair downsample) kept, its level 2
        // (red) replaced, and ours from there.
        Texture::mipmapSource = int(Texture::MipmapSource::Improve);
        std::unique_ptr<Texture> improved(shapeTexture(16));
        const QByteArray improved0 = pixels(*improved);
        QByteArray fair(8 * 8 * 4, Qt::Uninitialized);
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 8; ++x)
                for (int c = 0; c < 4; ++c) {
                    const auto *p = reinterpret_cast<const unsigned char *>(improved0.constData());
                    double sum = 0.0;
                    for (int dy = 0; dy < 2; ++dy)
                        for (int dx = 0; dx < 2; ++dx)
                            sum += c == 3 ? p[((2 * y + dy) * 16 + 2 * x + dx) * 4 + 3] / 255.0
                                          : toLinear(p[((2 * y + dy) * 16 + 2 * x + dx) * 4 + c]);
                    fair[(y * 8 + x) * 4 + c] = char(c == 3 ? int(sum / 4.0 * 255.0 + 0.5) : toSrgb(sum / 4.0));
                }
        QByteArray red(4 * 4 * 4, Qt::Uninitialized);
        for (int i = 0; i < 16; ++i) {
            red[i * 4] = char(255);
            red[i * 4 + 1] = 0;
            red[i * 4 + 2] = 0;
            red[i * 4 + 3] = char(255);
        }
        improved->sourceMipmaps.push_back({8, 8, 0, fair});
        improved->sourceMipmaps.push_back({4, 4, 0, red});
        check(improved->prepareForRhi() && improved->preparedLevels.size() == 5
                      && improved->preparedLevels[1] == fair && improved->preparedLevels[2] != red,
              "improved: a fair file level kept, a wrong one made again");

        // Blocks: level 0 as it is, the levels below encoded alike.
        Texture::mipmapSource = int(Texture::MipmapSource::Regenerate);
        if (RhiTextures::supportsBlocks()) {
            Texture blocks(32, 32, 24, nullptr);
            fill(blocks);
            QByteArray dxt;
            QString message;
            DxtCodec::encode(blocks.imageData, blocks.imageSize, 32, 32, 3, DxtCodec::Format::Dxt1, false, dxt,
                             message);
            blocks.compressedData = dxt;
            blocks.compressedGLFormat = GL_COMPRESSED_RGB_S3TC_DXT1_EXT;
            blocks.displayOnly = true;
            bool sized = blocks.prepareForRhi() && blocks.preparedFormat == GL_COMPRESSED_RGB_S3TC_DXT1_EXT
                    && blocks.preparedLevels.size() == 6 && blocks.preparedLevels[0] == dxt;
            for (int k = 0, side = 32; sized && k < 6; ++k, side = std::max(1, side / 2))
                sized = blocks.preparedLevels[k].size() == DxtCodec::byteSize(side, side, DxtCodec::Format::Dxt1);
            check(sized && blocks.uploadForRhi(true) && blocks.gpuMipLevels == 6,
                  "made again on blocks: level 0 kept, the levels below encoded, uploaded whole");
        }
        Texture::shapeMipmaps = mipmapsWere;
        Texture::mipmapSource = sourceWas;
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
