/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef RHICONTEXT_H
#define RHICONTEXT_H

#include <QHash>
#include <QString>
#include <QStringList>
#include <QSurface>
#include <memory>
#include <rhi/qrhi.h>

// QRhi's Vulkan backend needs Qt's Vulkan support and the Vulkan SDK
// headers (Qt declares QRhiVulkanInitParams only with both).
#if QT_CONFIG(vulkan) && __has_include(<vulkan/vulkan.h>)
#define TSRE_RHI_VULKAN 1
#else
#define TSRE_RHI_VULKAN 0
#endif

class QOffscreenSurface;
class QVulkanInstance;

// The one QRhi every QRhi view shares, so meshes and textures are uploaded
// once for all windows (as OpenGL contexts share them).
class RhiContext {
public:
    // Created on first use with the API from core.rendering.rhiApi; null when
    // no QRhi could be created.
    static RhiContext *instance();
    // Destroys the QRhi (at exit, after every view released its resources).
    static void shutdown();
    ~RhiContext();

    QRhi *rhi() const { return rhiInstance.get(); }
    // The QWindow surface type windows rendering with this QRhi need.
    QSurface::SurfaceType surfaceType() const;
    QVulkanInstance *vulkanInstance() const { return vulkan.get(); }
    // Backend and device, for reports.
    QString info() const;

    struct Program {
        QShader vertex;
        QShader fragment;
        QString error;
        bool valid() const { return error.isEmpty() && vertex.isValid() && fragment.isValid(); }
    };
    // A program of the shader directory (shaders330, converted), baked for
    // this QRhi's backend and cached.
    const Program &program(const QString &vertexName, const QString &fragmentName,
                           const QStringList &defines);
    // A program from shaders330-style sources held in the code, cached by key.
    const Program &programFromSource(const QString &key, const QByteArray &vertex,
                                     const QByteArray &fragment);
    // Converts and bakes a program for a backend.
    static Program bake(const QString &directory, const QString &vertexName,
                        const QString &fragmentName, const QStringList &defines,
                        QRhi::Implementation implementation);
    static Program bakeSource(const QByteArray &vertex, const QByteArray &fragment,
                              const QStringList &defines, QRhi::Implementation implementation);

private:
    RhiContext() = default;
    bool create(QRhi::Implementation implementation);
    std::unique_ptr<QVulkanInstance> vulkan;
    std::unique_ptr<QOffscreenSurface> fallbackSurface;
    std::unique_ptr<QRhi> rhiInstance;
    QHash<QString, Program> programs;
};

#endif
