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
#include <tsre/ui/UiDisplay.h>
#include <tsre/shape/ComplexShape.h>
#include <tsre/shape/MstsMaterials.h>
#include <tsre/shape/ShapeDisplays.h>
#include <tsre/renderer/Mesh.h>
#include <tsre/renderer/RenderContext.h>
#include <tsre/renderer/RenderItem.h>
#include <tsre/renderer/RenderQueue.h>
#include <tsre/renderer/Renderer.h>
#include <tsre/texture/TexLib.h>

#include <RmlUi/Core.h>
#include <QDir>
#include <QMatrix4x4>

namespace Ui {

namespace {

// The data model's row.
struct ModelRow {
    Rml::String time, train, destination, origin, platform, delay;
};

ModelRow modelRow(const ShapeDisplays::Row &row) {
    return {row.time.toStdString(), row.train.toStdString(), row.destination.toStdString(),
            row.origin.toStdString(), row.platform.toStdString(), row.delay.toStdString()};
}

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

struct ShapeDisplays::Entry {
    ::ShapeDisplays::Display definition;
    std::unique_ptr<Display> display;
    bool failed = false;
    bool drawn = false;
    QDateTime last;
    // The data model `display`.
    Rml::DataModelHandle handle;
    Rml::String time, date, station, platform;
    int hours = 0, minutes = 0, seconds = 0;
    std::vector<ModelRow> departures, arrivals;
    // A square's item (its producer's) and the part it hangs on (-1 the
    // root; -2 not found yet).
    RenderItem *square = nullptr;
    int part = -2;
};

// The data model's values: the clock's, and the departures and arrivals
// for a platform's or a station's display.
void ShapeDisplays::fill(Entry &e, const QDateTime &now, const Data &data) {
    e.time = now.toString("HH:mm").toStdString();
    e.date = now.toString("yyyy-MM-dd").toStdString();
    e.hours = now.time().hour();
    e.minutes = now.time().minute();
    e.seconds = now.time().second();
    e.station = data.station.toStdString();
    e.platform = data.platform.toStdString();
    e.departures.clear();
    e.arrivals.clear();
    if (e.definition.input == ::ShapeDisplays::Display::Input::Platform
            || e.definition.input == ::ShapeDisplays::Display::Input::Station) {
        for (const Row &row : data.departures)
            e.departures.push_back(modelRow(row));
        for (const Row &row : data.arrivals)
            e.arrivals.push_back(modelRow(row));
    }
}

ShapeDisplays::ShapeDisplays(const QString &shapePath) : folder(::ShapeDisplays::documentsFolder(shapePath)) {
    for (const ::ShapeDisplays::Display &definition : ::ShapeDisplays::read(shapePath)) {
        entries.push_back(std::make_unique<Entry>());
        entries.back()->definition = definition;
    }
}

ShapeDisplays::~ShapeDisplays() {
    for (const std::unique_ptr<Entry> &entry : entries)
        if (entry->square != nullptr)
            Renderer::retirePacket(entry->square);
}

bool ShapeDisplays::empty() const {
    return entries.empty();
}

void ShapeDisplays::update(Renderer &renderer, ComplexShape &shape, unsigned int stateId, const QDateTime &now,
                           const Data &data) {
    for (const std::unique_ptr<Entry> &entry : entries) {
        Entry &e = *entry;
        if (e.failed)
            continue;
        const ::ShapeDisplays::Display &d = e.definition;
        if (!e.display) {
            // Its size: the resolution, else the replaced texture's.
            QSize size(d.resolution[0], d.resolution[1]);
            if ((size.width() < 1 || size.height() < 1) && !d.image.isEmpty())
                size = TexLib::imageSize(QDir(shape.getTexPath()).filePath(d.image));
            if (size.width() < 1 || size.height() < 1)
                size = QSize(512, 256);
            e.display = std::make_unique<Display>(size.width(), size.height());
            Rml::Context *context = e.display->context();
            Rml::DataModelConstructor model = context != nullptr ? context->CreateDataModel("display")
                                                                 : Rml::DataModelConstructor();
            if (!model) {
                e.failed = true;
                continue;
            }
            if (auto row = model.RegisterStruct<ModelRow>()) {
                row.RegisterMember("time", &ModelRow::time);
                row.RegisterMember("train", &ModelRow::train);
                row.RegisterMember("destination", &ModelRow::destination);
                row.RegisterMember("origin", &ModelRow::origin);
                row.RegisterMember("platform", &ModelRow::platform);
                row.RegisterMember("delay", &ModelRow::delay);
            }
            model.RegisterArray<std::vector<ModelRow>>();
            model.Bind("time", &e.time);
            model.Bind("date", &e.date);
            model.Bind("hours", &e.hours);
            model.Bind("minutes", &e.minutes);
            model.Bind("seconds", &e.seconds);
            model.Bind("station", &e.station);
            model.Bind("platform", &e.platform);
            model.Bind("departures", &e.departures);
            model.Bind("arrivals", &e.arrivals);
            e.handle = model.GetModelHandle();
            fill(e, now, data);
            if (folder.isEmpty() || !e.display->load(QDir(folder).filePath(d.document))) {
                e.failed = true;
                continue;
            }
        }
        // Due: once, or at its refresh rate.
        const bool due = !e.drawn
                || (d.refreshRate > 0.0f && e.last.msecsTo(now) >= qint64(1000.0f / d.refreshRate));
        if (!due)
            continue;
        fill(e, now, data);
        e.handle.DirtyAllVariables();
        e.display->update();
        const unsigned int texture = e.display->draw(renderer);
        if (!d.image.isEmpty())
            shape.setTextureOverride(stateId, d.image, texture);
        e.drawn = true;
        e.last = now;
    }
}

void ShapeDisplays::pushSquares(RenderQueue &queue, ComplexShape &shape, unsigned int stateId, quint32 selectionId) {
    for (const std::unique_ptr<Entry> &entry : entries) {
        Entry &e = *entry;
        const ::ShapeDisplays::Display &d = e.definition;
        if (!d.square || e.failed || !e.display || e.display->texture() == 0)
            continue;
        const MeshHandle mesh = squareMesh();
        if (!mesh.valid())
            return;
        // Its part, by name (any case); the root without one.
        if (e.part == -2) {
            e.part = -1;
            for (int part = 0; part < shape.partCount() && !d.hierarchy.isEmpty(); ++part)
                if (shape.partName(part).compare(d.hierarchy, Qt::CaseInsensitive) == 0) {
                    e.part = part;
                    break;
                }
        }
        if (e.square == nullptr) {
            e.square = new RenderItem();
            RenderItem &r = *e.square;
            r.mesh.handle = mesh;
            r.mesh.first = 0;
            r.mesh.count = 6;
            r.mesh.primitive = RenderItem::PRIMITIVE_TRIANGLES;
            r.setVertexAttributes(RenderItem::VNTA);
            // Glass shines as a full bright part does (its own night
            // texture); paper is lit as the shape's normal parts.
            MstsMaterials::applyLight(r, d.surface == ::ShapeDisplays::Display::Surface::Glass ? -8 : -5);
            r.material.surface = RenderItem::SURFACE_OPAQUE;
            r.material.doubleSided = true;
            r.shared = true;
        }
        e.square->material.textured = true;
        e.square->material.textureId = -1;
        e.square->material.textureObject = e.display->texture();
        // The part as posed, then the square's place, turn (about x, then
        // y, then z) and size.
        QMatrix4x4 m;
        if (e.part >= 0) {
            float part[16];
            if (shape.partMatrix(stateId, e.part, part))
                std::copy(part, part + 16, m.data());
        }
        m.translate(d.position[0], d.position[1], d.position[2]);
        m.rotate(d.rotation[2], 0.0f, 0.0f, 1.0f);
        m.rotate(d.rotation[1], 0.0f, 1.0f, 0.0f);
        m.rotate(d.rotation[0], 1.0f, 0.0f, 0.0f);
        m.scale(d.size[0], d.size[1], 1.0f);
        e.square->msMatrix = queue.frameMatrix(m.constData());
        e.square->setSelectionId(selectionId);
        queue.submit(e.square, selectionId, RenderQueue::SUBMIT_ORDERED);
    }
}

void ShapeDisplays::release(Renderer &renderer, ComplexShape *shape, unsigned int stateId) {
    for (const std::unique_ptr<Entry> &entry : entries) {
        if (shape != nullptr && !entry->definition.image.isEmpty())
            shape->setTextureOverride(stateId, entry->definition.image, 0);
        if (entry->display)
            entry->display->release(renderer);
        entry->drawn = false;
        if (entry->square != nullptr)
            Renderer::retirePacket(entry->square);
        entry->square = nullptr;
    }
}

}
