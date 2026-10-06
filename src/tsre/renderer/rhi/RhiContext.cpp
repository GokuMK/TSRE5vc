/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include "RhiContext.h"
#include "RhiShaderSource.h"
#include <QDebug>
#include <QOffscreenSurface>
#include <QVulkanInstance>
#include <rhi/qshaderbaker.h>
#include <tsre/Game.h>
#include <tsre/ogl/GLUU.h>

namespace {
std::unique_ptr<RhiContext> &holder() {
    static std::unique_ptr<RhiContext> context;
    return context;
}
bool attempted = false;

QList<QShaderBaker::GeneratedShader> targets(QRhi::Implementation implementation) {
    switch (implementation) {
    case QRhi::Vulkan: return {{QShader::SpirvShader, QShaderVersion(100)}};
    case QRhi::D3D11:
    case QRhi::D3D12: return {{QShader::HlslShader, QShaderVersion(50)}};
    case QRhi::Metal: return {{QShader::MslShader, QShaderVersion(12)}};
    case QRhi::OpenGLES2: return {{QShader::GlslShader, QShaderVersion(330)}};
    default: break;
    }
    // Null backend: any valid shader will do.
    return {{QShader::SpirvShader, QShaderVersion(100)}};
}

QShader bakeStage(const QByteArray &source, QShader::Stage stage,
                  QRhi::Implementation implementation, QString &error) {
    QShaderBaker baker;
    baker.setGeneratedShaders(targets(implementation));
    baker.setGeneratedShaderVariants({QShader::StandardShader});
    baker.setSourceString(source, stage);
    QShader shader = baker.bake();
    if (!shader.isValid())
        error += baker.errorMessage() + "\n";
    return shader;
}
}

RhiContext *RhiContext::instance() {
    if (!attempted) {
        attempted = true;
        std::unique_ptr<RhiContext> context(new RhiContext());
        const QString api = Game::rhiApi.toLower();
        QList<QRhi::Implementation> order;
        if (api == "vulkan") order = {QRhi::Vulkan};
        else if (api == "opengl") order = {QRhi::OpenGLES2};
        else if (api == "metal") order = {QRhi::Metal};
        else if (api == "d3d11") order = {QRhi::D3D11};
        else if (api == "d3d12") order = {QRhi::D3D12};
        else if (api == "null") order = {QRhi::Null};
        else {
#if defined(Q_OS_MACOS)
            order = {QRhi::Metal, QRhi::OpenGLES2};
#elif defined(Q_OS_WIN)
            order = {QRhi::D3D11, QRhi::Vulkan, QRhi::OpenGLES2};
#else
            order = {QRhi::Vulkan, QRhi::OpenGLES2};
#endif
        }
        for (QRhi::Implementation implementation : order) {
            if (context->create(implementation)) {
                holder() = std::move(context);
                break;
            }
        }
        if (!holder())
            qWarning() << "QRhi renderer: no graphics API could be initialised";
        else
            qInfo() << "QRhi renderer:" << holder()->info();
    }
    return holder().get();
}

void RhiContext::shutdown() {
    holder().reset();
}

RhiContext::~RhiContext() {
    rhiInstance.reset();
}

bool RhiContext::create(QRhi::Implementation implementation) {
    QRhi::Flags flags;
    if (qEnvironmentVariableIsSet("TSRE_RHI_DEBUG"))
        flags |= QRhi::EnableDebugMarkers;
    switch (implementation) {
    case QRhi::Vulkan: {
#if QT_CONFIG(vulkan)
        vulkan = std::make_unique<QVulkanInstance>();
        if (qEnvironmentVariableIsSet("TSRE_RHI_DEBUG"))
            vulkan->setLayers({"VK_LAYER_KHRONOS_validation"});
        vulkan->setExtensions(QRhiVulkanInitParams::preferredInstanceExtensions());
        if (!vulkan->create()) {
            vulkan.reset();
            return false;
        }
        QRhiVulkanInitParams params;
        params.inst = vulkan.get();
        rhiInstance.reset(QRhi::create(QRhi::Vulkan, &params, flags));
        if (!rhiInstance)
            vulkan.reset();
#endif
        break;
    }
    case QRhi::OpenGLES2: {
        fallbackSurface.reset(QRhiGles2InitParams::newFallbackSurface());
        QRhiGles2InitParams params;
        params.fallbackSurface = fallbackSurface.get();
        rhiInstance.reset(QRhi::create(QRhi::OpenGLES2, &params, flags));
        break;
    }
    case QRhi::Null: {
        QRhiNullInitParams params;
        rhiInstance.reset(QRhi::create(QRhi::Null, &params, flags));
        break;
    }
    default:
        // Metal and Direct3D need their platforms' init parameters.
        break;
    }
    return rhiInstance != nullptr;
}

QSurface::SurfaceType RhiContext::surfaceType() const {
    switch (rhiInstance ? rhiInstance->backend() : QRhi::Null) {
    case QRhi::Vulkan: return QSurface::VulkanSurface;
    case QRhi::OpenGLES2: return QSurface::OpenGLSurface;
    case QRhi::Metal: return QSurface::MetalSurface;
    case QRhi::D3D11:
    case QRhi::D3D12: return QSurface::Direct3DSurface;
    default: break;
    }
    return QSurface::RasterSurface;
}

QString RhiContext::info() const {
    if (!rhiInstance)
        return QString();
    return QString("QRhi %1: %2").arg(QString::fromLatin1(rhiInstance->backendName()),
                                       QString::fromLatin1(rhiInstance->driverInfo().deviceName));
}

RhiContext::Program RhiContext::bake(const QString &directory, const QString &vertexName,
                                     const QString &fragmentName, const QStringList &defines,
                                     QRhi::Implementation implementation) {
    Program program;
    const QByteArray vertex = GLUU::shaderSource(directory, vertexName, "vs");
    const QByteArray fragment = GLUU::shaderSource(directory, fragmentName, "fs");
    const RhiShaderSource::Program converted = RhiShaderSource::convert(vertex, fragment, defines);
    program.error = converted.error;
    program.vertex = bakeStage(converted.vertex, QShader::VertexStage, implementation, program.error);
    program.fragment = bakeStage(converted.fragment, QShader::FragmentStage, implementation,
                                 program.error);
    return program;
}

const RhiContext::Program &RhiContext::program(const QString &vertexName,
                                               const QString &fragmentName,
                                               const QStringList &defines) {
    const QString key = vertexName + "|" + fragmentName + "|" + defines.join(',');
    auto found = programs.constFind(key);
    if (found != programs.constEnd())
        return *found;
    Program program = bake(GLUU::shaderDirectory(), vertexName, fragmentName, defines,
                           rhiInstance ? rhiInstance->backend() : QRhi::Null);
    if (!program.valid())
        qWarning().noquote() << "QRhi program" << key << "failed:" << program.error;
    return *programs.insert(key, program);
}
