/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/ui/UiShapeDisplays.h>
#include <tsre/ui/UiDisplayDocument.h>
#include <tsre/shape/ComplexShape.h>
#include <tsre/renderer/Mesh.h>
#include <tsre/renderer/RenderContext.h>
#include <tsre/renderer/RenderItem.h>
#include <tsre/renderer/RenderQueue.h>
#include <tsre/renderer/Renderer.h>
#include <tsre/texture/TexLib.h>

#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <algorithm>

namespace Ui {

namespace {

// A square: one unit across, in the x-y plane facing +z, the document's top
// row at +y (its first texture row) and its left at +x: a shape's space is
// left-handed (MSTS), so seen from +z, +x is on the left. Position, normal,
// texture coordinates and alpha (VNTA, as MSTS shapes).
MeshHandle squareMesh() {
    static MeshHandle mesh;
    if (mesh.valid() || !RenderContext::ready())
        return mesh;
    const float corners[4][4] = {{-0.5f, 0.5f, 1.0f, 0.0f}, {-0.5f, -0.5f, 1.0f, 1.0f},
                                 {0.5f, -0.5f, 0.0f, 1.0f}, {0.5f, 0.5f, 0.0f, 0.0f}};
    MeshData data;
    data.layout = RenderItem::VNTA;
    for (int index : {0, 1, 2, 0, 2, 3}) {
        const float *c = corners[index];
        data.vertices.insert(data.vertices.end(), {c[0], c[1], 0.0f, 0.0f, 0.0f, 1.0f, c[2], c[3], 1.0f});
    }
    mesh = Meshes::create(std::move(data));
    return mesh;
}

}

ShapeDisplays::ShapeDisplays(const ComplexShape &shape, std::vector<std::shared_ptr<DisplayDocument>> documents)
    : blocks(shape.displays().cbegin(), shape.displays().cend()), docs(std::move(documents)) {
    docs.resize(blocks.size());
    shown.resize(blocks.size());
}

ShapeDisplays::~ShapeDisplays() {
    for (const Shown &s : shown)
        if (s.square != nullptr)
            Renderer::retirePacket(s.square);
}

QString ShapeDisplays::documentPath(const ComplexShape &shape, const ::ShapeDisplays::Display &display) {
    const QString folder = ::ShapeDisplays::documentsFolder(shape.getPathId());
    if (folder.isEmpty())
        return QString();
    const QString path = QDir::cleanPath(QDir(folder).filePath(display.document));
    return path.startsWith(QDir::cleanPath(folder) + "/") ? path : QString();
}

void ShapeDisplays::report(const ComplexShape &shape) {
    static QSet<QString> reported;
    const QString shapePath = shape.getPathId();
    if (!shape.isLoaded() || reported.contains(shapePath))
        return;
    reported.insert(shapePath);
    const QString name = shapePath.section('/', -1);
    for (const QString &problem : shape.displayProblems())
        //% "Shape %1: a display (ESD_TSREDisplay) left out: %2."
        displayMessage(false, qtTrId("displays.shape.left.out").arg(name, problem), shapePath);
    const QStringList images = shape.imageNames();
    for (const ::ShapeDisplays::Display &d : shape.displays()) {
        const QString path = documentPath(shape, d);
        if (path.isEmpty())
            //% "Shape %1: the display document %2 is not in its DISPLAYS folder (%3)."
            displayMessage(true, qtTrId("displays.shape.document.outside")
                                         .arg(name, d.document, ::ShapeDisplays::documentsFolder(shapePath)),
                           shapePath);
        else if (!QFileInfo::exists(path))
            //% "Shape %1: the display document %2 was not found."
            displayMessage(true, qtTrId("displays.shape.document.missing").arg(name, path), shapePath);
        if (!d.square && !images.isEmpty()
                && std::none_of(images.cbegin(), images.cend(), [&d](const QString &image) {
                       return image.compare(d.image, Qt::CaseInsensitive) == 0;
                   }))
            //% "Shape %1: its display's Image %2 is not one of the shape's images; nothing shows it."
            displayMessage(false, qtTrId("displays.shape.image").arg(name, d.image), shapePath);
        if (d.square && !d.hierarchy.isEmpty() && squarePart(shape, d) < 0)
            //% "Shape %1: its display's ShapeHierarchy %2 is not one of the shape's matrices; the square hangs on its root."
            displayMessage(false, qtTrId("displays.shape.hierarchy").arg(name, d.hierarchy), shapePath);
    }
}

QSize ShapeDisplays::documentSize(const ComplexShape &shape, const ::ShapeDisplays::Display &display) {
    QSize size(display.resolution[0], display.resolution[1]);
    if ((size.width() < 1 || size.height() < 1) && !display.image.isEmpty())
        size = TexLib::imageSize(QDir(shape.getTexPath()).filePath(display.image));
    if (size.width() < 1 || size.height() < 1)
        size = QSize(512, 256);
    return size;
}

int ShapeDisplays::squarePart(const ComplexShape &shape, const ::ShapeDisplays::Display &display) {
    for (int part = 0; part < shape.partCount() && !display.hierarchy.isEmpty(); ++part)
        if (shape.partName(part).compare(display.hierarchy, Qt::CaseInsensitive) == 0)
            return part;
    return -1;
}

QMatrix4x4 ShapeDisplays::squareMatrix(const ComplexShape &shape, unsigned int stateId, int part,
                                       const ::ShapeDisplays::Display &d) {
    QMatrix4x4 m;
    if (part >= 0) {
        float posed[16];
        if (shape.partMatrix(stateId, part, posed))
            std::copy(posed, posed + 16, m.data());
    }
    m.translate(d.position[0], d.position[1], d.position[2]);
    m.rotate(d.rotation[2], 0.0f, 0.0f, 1.0f);
    m.rotate(d.rotation[1], 0.0f, 1.0f, 0.0f);
    m.rotate(d.rotation[0], 1.0f, 0.0f, 0.0f);
    m.scale(d.size[0], d.size[1], 1.0f);
    return m;
}

std::vector<std::shared_ptr<DisplayDocument>> ShapeDisplays::ownDocuments(const ComplexShape &shape) {
    report(shape);
    std::vector<std::shared_ptr<DisplayDocument>> documents;
    for (const ::ShapeDisplays::Display &d : shape.displays()) {
        const QString path = documentPath(shape, d);
        documents.push_back(path.isEmpty() ? nullptr
                : std::make_shared<DisplayDocument>(path, documentSize(shape, d), d.refreshRate, d.input));
    }
    return documents;
}

void ShapeDisplays::overrideTextures(ComplexShape &shape, unsigned int stateId) {
    for (size_t i = 0; i < blocks.size(); ++i) {
        const ::ShapeDisplays::Display &d = blocks[i];
        DisplayDocument *document = docs[i].get();
        if (d.image.isEmpty() || document == nullptr || document->failed())
            continue;
        const unsigned int texture = document->texture();
        if (texture != shown[i].texture)
            shape.setTextureOverride(stateId, d.image, texture, d.surface);
        shown[i].texture = texture;
    }
}

void ShapeDisplays::push(RenderQueue &queue, ComplexShape &shape, unsigned int stateId, quint32 selectionId,
                         unsigned int frame, float distance) {
    overrideTextures(shape, stateId);
    for (size_t i = 0; i < blocks.size(); ++i) {
        const ::ShapeDisplays::Display &d = blocks[i];
        DisplayDocument *document = docs[i].get();
        Shown &s = shown[i];
        if (document == nullptr || document->failed())
            continue;
        document->see(frame, distance);
        const unsigned int texture = document->texture();
        if (!d.square || texture == 0)
            continue;
        const MeshHandle mesh = squareMesh();
        if (!mesh.valid())
            return;
        if (s.part == -2)
            s.part = squarePart(shape, d);
        if (s.square == nullptr) {
            s.square = new RenderItem();
            RenderItem &r = *s.square;
            r.mesh.handle = mesh;
            r.mesh.first = 0;
            r.mesh.count = 6;
            r.mesh.primitive = RenderItem::PRIMITIVE_TRIANGLES;
            r.setVertexAttributes(RenderItem::VNTA);
            // Glass unless paper.
            ::ShapeDisplays::applySurface(r, d.surface == ::ShapeDisplays::Display::Surface::Paper
                                                     ? d.surface : ::ShapeDisplays::Display::Surface::Glass);
            r.material.surface = RenderItem::SURFACE_OPAQUE;
            r.material.doubleSided = true;
            r.shared = true;
        }
        s.texture = texture;
        s.square->material.textured = true;
        s.square->material.textureId = -1;
        s.square->material.textureObject = texture;
        const QMatrix4x4 m = squareMatrix(shape, stateId, s.part, d);
        s.square->msMatrix = queue.frameMatrix(m.constData());
        s.square->setSelectionId(selectionId);
        queue.submit(s.square, selectionId, RenderQueue::SUBMIT_ORDERED);
    }
}

void ShapeDisplays::release(ComplexShape &shape, unsigned int stateId) {
    for (size_t i = 0; i < blocks.size(); ++i) {
        Shown &s = shown[i];
        if (!blocks[i].image.isEmpty() && s.texture != 0)
            shape.setTextureOverride(stateId, blocks[i].image, 0, blocks[i].surface);
        s.texture = 0;
        if (s.square != nullptr)
            Renderer::retirePacket(s.square);
        s.square = nullptr;
    }
}

}
