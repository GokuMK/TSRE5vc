/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/shape/ShapeMetadata.h>
#include <tsre/fileFunctions/ContentPath.h>
#include <tsre/shape/SFileDocument.h>
#include <QFileInfo>

ShapeMetadata ShapeMetadata::from(const SFileDetail::Node &root, ShapeAxes::Axes model) {
    ShapeMetadata out;
    out.detail = root.child("esd_detail_level").integer(0, -1);
    out.alternative = root.child("esd_alternative_texture").integer(0, 0);
    out.snapable = bool(root.child("esd_snapable"));
    out.soundFileName = root.child("esd_soundfilename").scalar(0);
    const auto box = [&out](const SFileDetail::Node &n, int start) {
        std::array<float, 6> b;
        for (int i = 0; i < 6; ++i) {
            if (n.scalar(start + i).isEmpty())
                return;
            b[size_t(i)] = float(n.number(start + i));
        }
        out.boxes.push_back(b);
    };
    for (auto b : root.children("esd_bounding_box"))
        box(*b, 0);
    out.boundingBoxes = int(out.boxes.size());
    for (auto b : root.child("esd_complex").children("esd_complex_box"))
        box(*b, 6);
    out.displays = ShapeDisplays::read(root, &out.displayProblems);
    out.lights = ShapeLights::read(root, &out.lightProblems);
    out.emitters = ShapeEmitters::read(root, &out.emitterProblems);
    out.extension = ShapeLod::extensionFrom(root.child("esd_tsredistancelevelextension").scalar(0));
    out.lightSchedule = ShapeLights::scheduleFrom(root.child("esd_tsrelightschedule").scalar(0));
    const QString selfLit = root.child("esd_tsreselflit").scalar(0);
    out.selfLit = ShapeLights::selfLitFrom(selfLit);
    if (out.selfLit == ShapeLights::SelfLit::Unknown) {
        out.lightProblems << ShapeLights::selfLitProblem(selfLit);
        out.selfLit = ShapeLights::SelfLit::On;
    }
    out.walk = root.child("esd_tsrewalk").scalar(0).trimmed();
    out.collision = root.child("esd_tsrecollision").scalar(0).trimmed();
    out.seats = ShapeSeats::read(root, &out.seatProblems);
    // Its positions in the model's own axes (task shapes 20); a value
    // neither MSTS nor glTF read as MSTS, told with the blocks it moved.
    const QString axesName = root.child("esd_tsreaxes").scalar(0);
    out.axes = ShapeAxes::fromName(axesName);
    if (out.axes == ShapeAxes::Axes::Unknown) {
        const QString problem = ShapeAxes::unknown(axesName);
        if (!out.displays.isEmpty())
            out.displayProblems << problem;
        if (!out.lights.isEmpty())
            out.lightProblems << problem;
        if (!out.emitters.isEmpty())
            out.emitterProblems << problem;
        if (!out.seats.none())
            out.seatProblems << problem;
    }
    if (ShapeAxes::mirrors(out.axes, model))
        out.mirror();
    return out;
}

void ShapeMetadata::mirror() {
    for (std::array<float, 6> &box : boxes)
        ShapeAxes::mirrorBox(box.data());
    for (ShapeDisplays::Display &display : displays)
        ShapeDisplays::mirror(display);
    for (ShapeLights::Light &light : lights)
        ShapeLights::mirror(light);
    for (ShapeEmitters::Emitter &emitter : emitters)
        ShapeEmitters::mirror(emitter);
    ShapeSeats::mirror(seats);
}

bool ShapeMetadata::read(const QString &shapePath, ShapeMetadata &out, QString *error) {
    out = ShapeMetadata();
    if (error != nullptr)
        error->clear();
    const QString path = ContentPath::withExtension(shapePath, "sd");
    if (!QFileInfo::exists(path))
        return false;
    SFileDetail::Document document;
    if (!document.read(path)) {
        if (error != nullptr)
            *error = document.diagnostics.join(QLatin1String("; "));
        return false;
    }
    out = from(document.root, ShapeAxes::ofModel(shapePath));
    return true;
}
