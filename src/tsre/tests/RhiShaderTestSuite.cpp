#include <tsre/tests/RhiShaderTestSuite.h>

#include <QDebug>
#include <rhi/qshaderbaker.h>
#include <rhi/qshaderdescription.h>

#include <tsre/ogl/GLUU.h>
#include <tsre/renderer/rhi/RhiContext.h>
#include <tsre/renderer/rhi/RhiRenderer.h>
#include <tsre/renderer/rhi/RhiShaderSource.h>
#include <tsre/renderer/RenderItem.h>

namespace {

const QShaderDescription::BlockVariable *member(const QShader &shader, const QString &name) {
    for (const QShaderDescription::UniformBlock &block : shader.description().uniformBlocks()) {
        if (block.binding != RhiShaderSource::UniformBlockBinding)
            continue;
        for (const QShaderDescription::BlockVariable &variable : block.members)
            if (variable.name == name.toUtf8())
                return &variable;
    }
    return nullptr;
}

int samplerBinding(const QShader &shader, const QString &name) {
    for (const QShaderDescription::InOutVariable &sampler : shader.description().combinedImageSamplers())
        if (sampler.name == name.toUtf8())
            return sampler.binding;
    return -1;
}

}

int TsreTests::runRhiShaderSuite(bool verbose) {
    int passed = 0, failed = 0;
    auto check = [&](bool ok, const QString &name) {
        if (ok) {
            ++passed;
            if (verbose) qInfo().noquote() << "[tests:rhi-shaders] PASS" << name;
        } else {
            ++failed;
            qWarning().noquote() << "[tests:rhi-shaders] FAIL" << name;
        }
    };

    const QByteArray branches =
            "a\n#ifdef X\nb\n#else\nc\n#endif\n#if defined(Y)\nd\n#elif defined(X)\ne\n#else\nf\n#endif\n"
            "#ifndef X\ng\n#endif\n";
    check(RhiShaderSource::preprocess(branches, {"X"}) == "a\nb\ne\n\n",
          "#ifdef, #elif defined and #ifndef follow the defines");
    check(RhiShaderSource::preprocess(branches, {}) == "a\nc\nf\ng\n\n",
          "branches fall to #else without defines");
    const QByteArray combined = "#if defined(A) && defined(B)\nab\n#endif\n#if defined(A) || defined(B)\naorb\n#endif\n";
    check(RhiShaderSource::preprocess(combined, {"A"}) == "aorb\n\n"
          && RhiShaderSource::preprocess(combined, {"A", "B"}) == "ab\naorb\n\n",
          "&& and || combine defined() terms");

    const RhiShaderSource::Program unwritten = RhiShaderSource::convert(
            "#version 330 core\nin vec4 vertex;\nout float written;\nvoid main() { written = 1.0; gl_Position = vertex; }\n",
            "#version 330 core\nin float written;\nin vec3 unwritten;\nout vec4 color;\n"
            "void main() { color = vec4(unwritten, written); }\n", {});
    check(unwritten.error.isEmpty() && unwritten.fragment.contains("vec3 unwritten = vec3(0);")
          && unwritten.fragment.contains("layout(location = 0) in float written;"),
          "fragment inputs the vertex stage does not write become private zeros");

    struct Variant { const char *vertex; const char *fragment; QStringList defines; };
    const QList<Variant> variants = {
        {"StandardFog", "StandardFog", {}}, {"StandardFog", "StandardFast", {}},
        {"StandardFog", "StandardFog", {"TSRE_TERRAIN"}}, {"StandardFog", "StandardFast", {"TSRE_TERRAIN"}},
        {"StandardFog", "StandardFog", {"TSRE_UNLIT"}}, {"StandardFog", "StandardFast", {"TSRE_UNLIT"}},
        {"StandardFog", "Selection", {"TSRE_TERRAIN"}}, {"StandardFog", "StandardFog", {"TSRE_PBR"}},
        {"StandardFog", "StandardFog", {"TSRE_WATER"}}, {"Shadows", "Shadows", {}},
        // With motion blur on (task 38): the velocity.
        {"StandardFog", "StandardFog", {"TSRE_VELOCITY"}},
        {"StandardFog", "StandardFog", {"TSRE_VELOCITY", "TSRE_TERRAIN"}},
        {"StandardFog", "StandardFog", {"TSRE_VELOCITY", "TSRE_UNLIT"}},
        {"StandardFog", "StandardFog", {"TSRE_VELOCITY", "TSRE_PBR"}},
        {"StandardFog", "StandardFog", {"TSRE_VELOCITY", "TSRE_WATER"}}};
    const QString directory = GLUU::shaderDirectory();
    for (QRhi::Implementation implementation : {QRhi::Vulkan, QRhi::OpenGLES2}) {
        const QString api = implementation == QRhi::Vulkan ? "SPIR-V" : "GLSL 330";
        for (const Variant &variant : variants) {
            const QString name = QString("%1/%2 %3 (%4)").arg(variant.vertex, variant.fragment,
                                                             variant.defines.join(','), api);
            const RhiContext::Program program = RhiContext::bake(
                        directory, variant.vertex, variant.fragment, variant.defines, implementation);
            if (!program.valid())
                qWarning().noquote() << program.error;
            check(program.valid(), name + " bakes");
            if (!program.valid())
                continue;
            check(member(program.vertex, "uPMatrix") != nullptr || QString(variant.vertex) == "Shadows",
                  name + " has the projection in its uniform block");
            if (variant.defines.contains("TSRE_PBR")) {
                // std140 arrays: 16 bytes per element. Reflection gives the
                // whole array's size (its stride stays 0).
                const QShaderDescription::BlockVariable *transforms = member(program.fragment, "pbrUvTransform");
                const int rows = RenderItem::Pbr::MAP_COUNT * 2;
                check(transforms != nullptr && transforms->size == rows * 16
                      && transforms->arrayDims == QList<int>({rows}),
                      name + " lays out uniform arrays with a 16-byte stride");
            }
            // The velocity's output and the instance's motion index with motion
            // blur on, neither without (the programs as before it).
            if (QString(variant.vertex) == "StandardFog" && QString(variant.fragment) == "StandardFog") {
                const bool velocity = variant.defines.contains("TSRE_VELOCITY");
                bool output = false, input = false;
                for (const QShaderDescription::InOutVariable &out : program.fragment.description().outputVariables())
                    output = output || out.location == 3;
                for (const QShaderDescription::InOutVariable &in : program.vertex.description().inputVariables())
                    input = input || in.location == 12;
                check(output == velocity && input == velocity,
                      name + (velocity ? " writes the velocity" : " has no velocity"));
            }
            if (QString(variant.fragment) == "StandardFog")
                check(samplerBinding(program.fragment, "uSampler") == 0
                      && samplerBinding(program.fragment, "shadow0") == 9,
                      name + " keeps the OpenGL texture units as sampler bindings");
            // QRhi's OpenGL backend sets uniform blocks member by member on
            // every draw: the patch records come from a texture instead.
            if (variant.defines.contains("TSRE_TERRAIN") || QString(variant.vertex) == "Shadows")
                check(samplerBinding(program.vertex, "terrainPatchData")
                              == RhiShaderSource::TerrainPatchDataBinding
                      && program.vertex.description().uniformBlocks().size() == 1,
                      name + " reads the terrain patch records from a texture");
        }
    }
    // Ambient occlusion passes: code-held GLSL 440.
    // The fog's compute passes (task 33): Vulkan and Direct3D 11 only.
    const QList<QByteArray> fog = RhiRenderer::fogShaders();
    for (int i = 0; i < 2; ++i) {
        QShaderBaker baker;
        baker.setGeneratedShaders({{QShader::SpirvShader, QShaderVersion(100)},
                                   {QShader::HlslShader, QShaderVersion(50)}});
        baker.setGeneratedShaderVariants({QShader::StandardShader});
        baker.setSourceString(fog[i], QShader::ComputeStage);
        const QShader shader = baker.bake();
        if (!shader.isValid())
            qWarning().noquote() << baker.errorMessage();
        check(shader.isValid(), QString("fog compute pass %1 bakes").arg(i));
    }
    // The falling drops' vertex shader (no vertex input: the drops from the
    // instance index).
    {
        QShaderBaker baker;
        baker.setGeneratedShaders({{QShader::SpirvShader, QShaderVersion(100)},
                                   {QShader::GlslShader, QShaderVersion(330)}});
        baker.setGeneratedShaderVariants({QShader::StandardShader});
        baker.setSourceString(RhiRenderer::rainShaders()[0], QShader::VertexStage);
        const QShader shader = baker.bake();
        if (!shader.isValid())
            qWarning().noquote() << baker.errorMessage();
        check(shader.isValid(), "falling drops' vertex shader bakes");
    }
    const QList<QByteArray> occlusion = RhiRenderer::ambientOcclusionShaders() + RhiRenderer::imageShaders()
            + RhiRenderer::skyShaders() + RhiRenderer::cloudShaders() + RhiRenderer::lightShaftShaders()
            + QList<QByteArray>{fog[2], RhiRenderer::rainShaders()[1]} + RhiRenderer::motionBlurShaders()
            + RhiRenderer::temporalShaders();
    for (int i = 0; i < occlusion.size(); ++i) {
        QShaderBaker baker;
        baker.setGeneratedShaders({{QShader::SpirvShader, QShaderVersion(100)},
                                   {QShader::GlslShader, QShaderVersion(330)}});
        baker.setGeneratedShaderVariants({QShader::StandardShader});
        baker.setSourceString(occlusion[i], QShader::FragmentStage);
        const QShader shader = baker.bake();
        if (!shader.isValid())
            qWarning().noquote() << baker.errorMessage();
        check(shader.isValid(), QString("full-screen pass %1 (occlusion, present, bloom, sky) bakes").arg(i));
    }
    const QList<QByteArray> splats = RhiRenderer::glowSplatShaders();
    for (int i = 0; i < splats.size(); ++i) {
        QShaderBaker baker;
        baker.setGeneratedShaders({{QShader::SpirvShader, QShaderVersion(100)},
                                   {QShader::GlslShader, QShaderVersion(330)}});
        baker.setGeneratedShaderVariants({QShader::StandardShader});
        baker.setSourceString(splats[i], i == 0 ? QShader::VertexStage : QShader::FragmentStage);
        const QShader shader = baker.bake();
        if (!shader.isValid())
            qWarning().noquote() << baker.errorMessage();
        check(shader.isValid(), QString("glow splat %1 stage bakes").arg(i == 0 ? "vertex" : "fragment"));
    }
    qInfo() << "[tests:rhi-shaders] cases=" << (passed + failed)
            << "passed=" << passed << "failed=" << failed;
    return failed == 0 ? 0 : 1;
}
