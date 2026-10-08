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
#include "RhiRenderSurface.h"
#include "RhiShaderSource.h"
#include "RhiTextures.h"
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QOffscreenSurface>
#include <QSaveFile>
#include <QStandardPaths>
#if TSRE_RHI_VULKAN
#include <QVulkanInstance>
#endif
#include <rhi/qshaderbaker.h>
#include <tsre/Game.h>
#include <tsre/ogl/GLUU.h>
#include <tsre/renderer/Mesh.h>

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

// The driver's compiled pipelines (Vulkan pipeline cache, OpenGL program
// binaries) are kept between runs: each pipeline is a full shader compile
// and link on first use, dozens of them before the first frames show. QRhi
// checks that the data matches the device and driver and ignores it
// otherwise.
QString pipelineCachePath(const QRhi *rhi) {
    const QString directory = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (directory.isEmpty() || rhi->backend() == QRhi::Null)
        return QString();
    return QDir(directory).filePath(QString("rhi-pipelines-%1.bin")
                                    .arg(QString::fromLatin1(rhi->backendName()).toLower()));
}

void loadPipelineCache(QRhi *rhi) {
    QFile file(pipelineCachePath(rhi));
    if (file.fileName().isEmpty() || !file.open(QIODevice::ReadOnly))
        return;
    rhi->setPipelineCacheData(file.readAll());
}

void savePipelineCache(QRhi *rhi) {
    const QString path = pipelineCachePath(rhi);
    const QByteArray data = rhi->pipelineCacheData();
    if (path.isEmpty() || data.isEmpty() || !QDir().mkpath(QFileInfo(path).absolutePath()))
        return;
    QSaveFile file(path);
    if (file.open(QIODevice::WriteOnly) && file.write(data) == data.size())
        file.commit();
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
        // TSRE_RHI_API overrides the setting; "null" measures the renderer's
        // own CPU cost without a GPU driver.
        const bool fromEnvironment = qEnvironmentVariableIsSet("TSRE_RHI_API");
        const QString api = (fromEnvironment ? qEnvironmentVariable("TSRE_RHI_API")
                                             : Game::rhiApi).toLower();
        QList<QRhi::Implementation> order;
        if (api == "vulkan") order = {QRhi::Vulkan};
        else if (api == "opengl") order = {QRhi::OpenGLES2};
        else if (api == "metal") order = {QRhi::Metal};
        // Direct3D only for development (TSRE_RHI_API): it does not draw glTF
        // materials and costs twice Vulkan's CPU time (task 20). A profile
        // that still names it gets the automatic choice.
        else if (api == "d3d11" && fromEnvironment) order = {QRhi::D3D11};
        else if (api == "d3d12" && fromEnvironment) order = {QRhi::D3D12};
        else if (api == "null") order = {QRhi::Null};
        else {
#if defined(Q_OS_MACOS)
            order = {QRhi::Metal, QRhi::OpenGLES2};
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
    // Resources made by producers go before the QRhi does.
    if (holder() && holder()->rhi() != nullptr) {
        savePipelineCache(holder()->rhi());
        RhiRenderSurface::releaseAll();
        Meshes::releaseAllRhi();
        RhiTextures::releaseAll();
    }
    holder().reset();
}

RhiContext::~RhiContext() {
    rhiInstance.reset();
}

bool RhiContext::create(QRhi::Implementation implementation) {
    // Timestamps give the GPU time of frames (the FPS display).
    QRhi::Flags flags = QRhi::EnablePipelineCacheDataSave | QRhi::EnableTimestamps;
    if (qEnvironmentVariableIsSet("TSRE_RHI_DEBUG"))
        flags |= QRhi::EnableDebugMarkers;
    switch (implementation) {
    case QRhi::Vulkan: {
#if TSRE_RHI_VULKAN
        vulkan = std::make_unique<QVulkanInstance>();
        if (qEnvironmentVariableIsSet("TSRE_RHI_DEBUG"))
            vulkan->setLayers({"VK_LAYER_KHRONOS_validation"});
#ifndef Q_OS_MACOS
        // Portability drivers (MoltenVK) exist only on Apple systems, and
        // RenderDoc refuses instances that enable their enumeration.
        vulkan->setFlags(QVulkanInstance::NoPortabilityDrivers);
#endif
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
#ifdef Q_OS_WIN
    case QRhi::D3D11: {
        // The debug layer needs the Windows SDK's layers installed.
        QRhiD3D11InitParams params;
        params.enableDebugLayer = qEnvironmentVariableIsSet("TSRE_RHI_DEBUG");
        rhiInstance.reset(QRhi::create(QRhi::D3D11, &params, flags));
        break;
    }
    case QRhi::D3D12: {
        QRhiD3D12InitParams params;
        params.enableDebugLayer = qEnvironmentVariableIsSet("TSRE_RHI_DEBUG");
        rhiInstance.reset(QRhi::create(QRhi::D3D12, &params, flags));
        break;
    }
#endif
    default:
        // Metal needs its platform's init parameters.
        break;
    }
    if (rhiInstance)
        loadPipelineCache(rhiInstance.get());
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
    return bakeSource(GLUU::shaderSource(directory, vertexName, "vs"),
                      GLUU::shaderSource(directory, fragmentName, "fs"), defines, implementation);
}

RhiContext::Program RhiContext::bakeSource(const QByteArray &vertex, const QByteArray &fragment,
                                           const QStringList &defines,
                                           QRhi::Implementation implementation) {
    Program program;
    const RhiShaderSource::Program converted = RhiShaderSource::convert(vertex, fragment, defines);
    program.error = converted.error;
    program.vertex = bakeStage(converted.vertex, QShader::VertexStage, implementation, program.error);
    program.fragment = bakeStage(converted.fragment, QShader::FragmentStage, implementation,
                                 program.error);
    // TSRE_RHI_DUMP=<directory>: the converted GLSL and the backend's source
    // of every program, to read what a backend's compiler is given.
    static const QString dump = qEnvironmentVariable("TSRE_RHI_DUMP");
    if (!dump.isEmpty() && QDir().mkpath(dump)) {
        static int serial = 0;
        const QString base = QDir(dump).filePath(QString("program%1").arg(++serial, 3, 10, QChar('0')));
        const QList<QShaderBaker::GeneratedShader> generated = targets(implementation);
        auto write = [](const QString &path, const QByteArray &data) {
            QFile file(path);
            if (file.open(QIODevice::WriteOnly))
                file.write(data);
        };
        write(base + ".defines.txt", defines.join('\n').toUtf8());
        write(base + ".vert.glsl", converted.vertex);
        write(base + ".frag.glsl", converted.fragment);
        if (!generated.isEmpty()) {
            const QShaderKey key(generated.first().first, generated.first().second);
            write(base + ".vert.out", program.vertex.shader(key).shader());
            write(base + ".frag.out", program.fragment.shader(key).shader());
        }
    }
    return program;
}

const RhiContext::Program &RhiContext::programFromSource(const QString &key,
                                                         const QByteArray &vertex,
                                                         const QByteArray &fragment) {
    auto found = programs.constFind("source|" + key);
    if (found != programs.constEnd())
        return *found;
    Program program = bakeSource(vertex, fragment, {},
                                 rhiInstance ? rhiInstance->backend() : QRhi::Null);
    if (!program.valid())
        qWarning().noquote() << "QRhi program" << key << "failed:" << program.error;
    return *programs.insert("source|" + key, program);
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
