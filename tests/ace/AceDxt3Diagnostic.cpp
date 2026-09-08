#include "AceDxt3Diagnostic.h"
#include <tsre/texture/AceDocument.h>
#include <tsre/texture/AceLib.h>
#include <tsre/texture/DxtCodec.h>
#include <tsre/texture/Texture.h>
#include <QDebug>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLShaderProgram>
#include <QTemporaryDir>
#include <array>

// Deliberately separate from the production codec and the broad --gl matrix.
// A single BC2 block holds alpha nibbles 0..15, white RGB, selector zero.
int runDxt3Diagnostic() {
    int checks = 0, failures = 0;
    auto check = [&](bool ok, const QString &label) {
        ++checks;
        if (!ok) ++failures;
        qInfo().noquote() << (ok ? "PASS" : "FAIL") << label;
        return ok;
    };
    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    QOpenGLContext context;
    context.setFormat(format);
    if (!check(context.create(), "create GL context")) return 2;
    QOffscreenSurface surface;
    surface.setFormat(context.format());
    surface.create();
    if (!check(context.makeCurrent(&surface), "make GL context current")) return 2;
    QOpenGLFunctions_3_3_Core gl;
    if (!check(gl.initializeOpenGLFunctions(), "initialize GL 3.3 functions")) return 2;
    qInfo() << "GL vendor" << reinterpret_cast<const char *>(gl.glGetString(GL_VENDOR));
    qInfo() << "GL renderer" << reinterpret_cast<const char *>(gl.glGetString(GL_RENDERER));
    qInfo() << "GL version" << reinterpret_cast<const char *>(gl.glGetString(GL_VERSION));

    QByteArray block(16, '\0'), expected(64, char(255));
    for (int i = 0; i < 8; ++i) block[i] = char((2 * i) | ((2 * i + 1) << 4));
    block[8] = block[9] = char(255); // RGB565 endpoint 0 = white; endpoint 1 = black.
    for (int i = 0; i < 16; ++i) expected[i * 4 + 3] = char(i * 17);
    qInfo().noquote() << "Handcrafted block hex:" << block.toHex();
    qInfo() << "Expected alpha: 0 17 34 51 68 85 102 119 136 153 170 187 204 221 238 255";
    QString error;
    QByteArray cpu;
    if (!check(DxtCodec::decode(block, 4, 4, DxtCodec::Format::Dxt3, true, cpu, error)
               && cpu == expected, "CPU decode of handcrafted block matches independent reference")) return 2;

    QOpenGLShaderProgram program;
    const char *vertex = R"(#version 330 core
void main() {
    vec2 p[3] = vec2[3](vec2(-1,-1), vec2(3,-1), vec2(-1,3));
    gl_Position = vec4(p[gl_VertexID], 0, 1);
})";
    const char *fragment = R"(#version 330 core
uniform sampler2D sourceTexture;
uniform bool alphaAsRgb;
out vec4 color;
void main() {
    vec4 value = texelFetch(sourceTexture, ivec2(gl_FragCoord.xy), 0);
    color = alphaAsRgb ? vec4(value.aaa, 1) : value;
})";
    if (!check(program.addShaderFromSourceCode(QOpenGLShader::Vertex, vertex)
               && program.addShaderFromSourceCode(QOpenGLShader::Fragment, fragment)
               && program.link(), "compile/link diagnostic shader: " + program.log())) return 2;
    QOpenGLFramebufferObjectFormat targetFormat;
    targetFormat.setInternalTextureFormat(GL_RGBA8);
    targetFormat.setSamples(0);
    QOpenGLFramebufferObject target(4, 4, targetFormat);
    if (!check(target.isValid(), "create RGBA8 framebuffer")) return 2;
    GLuint vao = 0;
    gl.glGenVertexArrays(1, &vao);
    gl.glBindVertexArray(vao);
    gl.glPixelStorei(GL_PACK_ALIGNMENT, 1);
    gl.glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    gl.glDisable(GL_BLEND);
    gl.glDisable(GL_DITHER);
    gl.glDisable(GL_DEPTH_TEST);
    gl.glDisable(GL_SCISSOR_TEST);
    gl.glDisable(GL_CULL_FACE);
    gl.glDisable(GL_FRAMEBUFFER_SRGB);
    gl.glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

    auto report = [&](const QString &label, const QByteArray &pixels, bool alphaAsRgb) {
        QStringList values;
        bool exact = true;
        for (int i = 0; i < 16; ++i) {
            values << QString::number(quint8(pixels[i * 4 + (alphaAsRgb ? 0 : 3)]));
            for (int c = 0; c < 4; ++c) {
                const int wanted = alphaAsRgb ? (c == 3 ? 255 : i * 17)
                                             : quint8(expected[i * 4 + c]);
                exact &= quint8(pixels[i * 4 + c]) == wanted;
            }
        }
        qInfo().noquote() << label << "alpha:" << values.join(' ');
        check(exact, label + " all RGBA components match");
    };
    auto probe = [&](const QString &name, GLuint texture, const QByteArray &encoded) {
        gl.glActiveTexture(GL_TEXTURE0);
        gl.glBindTexture(GL_TEXTURE_2D, texture);
        gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
        gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
        if (!encoded.isEmpty()) {
            GLint bytes = 0, internal = 0;
            gl.glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_COMPRESSED_IMAGE_SIZE, &bytes);
            gl.glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT, &internal);
            qInfo() << name << "GPU internal format" << Qt::hex << internal;
            if (check(bytes == encoded.size(), name + " compressed byte count")) {
                QByteArray copied(bytes, '\0');
                gl.glGetCompressedTexImage(GL_TEXTURE_2D, 0, copied.data());
                check(copied == encoded, name + " compressed readback is byte-identical");
            }
        }
        QByteArray direct(64, '\0');
        gl.glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, direct.data());
        report(name + " direct U8 readback", direct, false);
        std::array<GLfloat, 64> floats{};
        gl.glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_FLOAT, floats.data());
        QStringList alphaFloats;
        for (int i = 0; i < 16; ++i)
            alphaFloats << QString::number(floats[i * 4 + 3] * 255.0f, 'f', 3);
        qInfo().noquote() << name << "direct float alpha multiplied by 255:" << alphaFloats.join(' ');
        for (bool alphaAsRgb : {false, true}) {
            target.bind();
            gl.glViewport(0, 0, 4, 4);
            program.bind();
            program.setUniformValue("sourceTexture", 0);
            program.setUniformValue("alphaAsRgb", alphaAsRgb);
            gl.glBindTexture(GL_TEXTURE_2D, texture);
            gl.glDrawArrays(GL_TRIANGLES, 0, 3);
            QByteArray rendered(64, '\0');
            gl.glReadPixels(0, 0, 4, 4, GL_RGBA, GL_UNSIGNED_BYTE, rendered.data());
            report(name + (alphaAsRgb ? " shader alpha-as-RGB" : " shader RGBA"), rendered, alphaAsRgb);
            program.release();
            target.release();
        }
        check(gl.glGetError() == GL_NO_ERROR, name + " no GL errors");
    };

    constexpr GLenum dxt3 = 0x83f2; // GL_COMPRESSED_RGBA_S3TC_DXT3_EXT
    GLuint raw = 0, control = 0;
    gl.glGenTextures(1, &raw);
    gl.glBindTexture(GL_TEXTURE_2D, raw);
    gl.glCompressedTexImage2D(GL_TEXTURE_2D, 0, dxt3, 4, 4, 0, block.size(), block.constData());
    probe("handcrafted/direct GL", raw, block);
    gl.glGenTextures(1, &control);
    gl.glBindTexture(GL_TEXTURE_2D, control);
    gl.glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 4, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, expected.constData());
    probe("uncompressed RGBA8 control", control, {});

    QTemporaryDir directory;
    AceWriteOptions writeOptions;
    writeOptions.encoding = AceEncoding::Dxt3;
    AceDocument generated;
    if (check(directory.isValid() && AceDocument::fromPixels(
                  reinterpret_cast<const unsigned char *>(expected.constData()), expected.size(),
                  4, 4, 4, writeOptions, generated, error), "generate ACE fixture: " + error)) {
        for (bool handcrafted : {true, false}) {
            const QString name = handcrafted ? "handcrafted/ACE/Texture" : "generated/ACE/Texture";
            AceDocument doc = generated;
            if (handcrafted) doc.levels[0].data = block;
            const QByteArray encoded = doc.levels[0].data;
            const QString path = directory.path() + "/probe.ace";
            Texture texture(path);
            AceLoadOptions loadOptions;
            if (check(doc.write(path, false, error) && AceLib::load(path, texture, loadOptions, error),
                      name + " file roundtrip: " + error)) {
                check(texture.compressedData == encoded, name + " reader preserves block bytes");
                int components = 0;
                check(doc.decode(0, cpu, components, error) && components == 4 && cpu == expected,
                      name + " CPU decode matches independent reference");
                if (check(texture.GLTextures(false), name + " production Texture upload")) {
                    probe(name, texture.tex[0], encoded);
                    Texture clone(&texture);
                    check(clone.loaded && clone.imageData && QByteArray(
                              reinterpret_cast<const char *>(clone.imageData), clone.imageSize) == expected,
                          name + " GPU-only clone preserves every alpha level");
                    check(!texture.imageData && texture.compressedData.isEmpty(),
                          name + " cloning leaves source GPU-only");
                    if (clone.tex) gl.glDeleteTextures(1, clone.tex);
                    delete[] clone.tex;
                    delete[] clone.imageData;
                    clone.tex = nullptr;
                    clone.imageData = nullptr;

                    GLuint packBuffer = 0;
                    gl.glGenBuffers(1, &packBuffer);
                    gl.glBindBuffer(GL_PIXEL_PACK_BUFFER, packBuffer);
                    gl.glBufferData(GL_PIXEL_PACK_BUFFER, 16, nullptr, GL_STREAM_READ);
                    gl.glPixelStorei(GL_PACK_ALIGNMENT, 8);
                    gl.glPixelStorei(GL_PACK_ROW_LENGTH, 32);
                    texture.setEditable();
                    check(texture.editable && texture.imageData && QByteArray(
                              reinterpret_cast<const char *>(texture.imageData), texture.imageSize) == expected,
                          name + " setEditable preserves every alpha level");
                    GLint bound = 0, alignment = 0, rowLength = 0;
                    gl.glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &bound);
                    gl.glGetIntegerv(GL_PACK_ALIGNMENT, &alignment);
                    gl.glGetIntegerv(GL_PACK_ROW_LENGTH, &rowLength);
                    check(bound == GLint(packBuffer) && alignment == 8 && rowLength == 32,
                          name + " compressed readback restores hostile pack state");
                    gl.glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
                    gl.glDeleteBuffers(1, &packBuffer);
                    gl.glPixelStorei(GL_PACK_ALIGNMENT, 1);
                    gl.glPixelStorei(GL_PACK_ROW_LENGTH, 0);
                    if (texture.imageData) {
                        QByteArray edited = expected;
                        edited[3] = char(255);
                        texture.imageData[3] = 255;
                        texture.update();
                        check(texture.gpuInternalFormat == GL_RGBA8,
                              name + " editing changes GPU storage to uncompressed RGBA8");
                        delete[] texture.imageData;
                        texture.imageData = nullptr;
                        texture.editable = false;
                        texture.setEditable();
                        check(texture.imageData && QByteArray(reinterpret_cast<const char *>(texture.imageData),
                                                              texture.imageSize) == edited,
                              name + " post-edit readback uses current GPU format");
                        const QString saved = directory.path() + "/edited.ace";
                        check(AceLib::save(saved, &texture, writeOptions, error), name + " save edited DXT3");
                        Texture reload(saved);
                        AceLoadOptions cpuOptions;
                        cpuOptions.cpuPixels = true;
                        check(AceLib::load(saved, reload, cpuOptions, error) && reload.imageData && QByteArray(
                                  reinterpret_cast<const char *>(reload.imageData), reload.imageSize) == edited,
                              name + " edited save/reload preserves alpha");
                        delete[] reload.imageData;
                        reload.imageData = nullptr;
                    }
                    check(gl.glGetError() == GL_NO_ERROR, name + " production regression checks GL errors");
                }
            }
            if (texture.tex) gl.glDeleteTextures(1, texture.tex);
            delete[] texture.tex;
            delete[] texture.imageData;
            texture.tex = nullptr;
            texture.imageData = nullptr;
        }
    }
    gl.glDeleteTextures(1, &raw);
    gl.glDeleteTextures(1, &control);
    gl.glDeleteVertexArrays(1, &vao);
    qInfo() << "DXT3 diagnostic checks" << checks << "failures" << failures;
    return failures ? 1 : 0;
}
