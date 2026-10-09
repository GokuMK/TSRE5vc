/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include "MapLabelLayer.h"
#include "MapPalette.h"
#include "MapView.h"
#include "TrackMapLayer.h"
#include <QDebug>
#include <QElapsedTimer>
#include <QFontMetrics>
#include <QOpenGLFunctions>
#include <QPainter>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <tsre/Game.h>
#include <tsre/ogl/OglObj.h>
#include <tsre/renderer/RenderItem.h>
#include <tsre/renderer/rhi/RhiTextures.h>
#include <tsre/texture/TexLib.h>
#include <tsre/texture/Texture.h>

namespace {

// Pixel sizes at a pixel ratio of 1.
constexpr int MinorPixels = 12;
constexpr int MajorPixels = 14;
constexpr float HaloPixels = 3.0f;

}

double placeLabelPriority(const QString &featureCode, qint64 population, bool *major) {
    int tier = 1;
    if (featureCode == QLatin1String("PPLC")) tier = 5;
    else if (featureCode == QLatin1String("PPLA")) tier = 4;
    else if (featureCode == QLatin1String("PPLA2")) tier = 3;
    else if (featureCode == QLatin1String("PPLA3")) tier = 2;
    else if (featureCode == QLatin1String("PPLX")) tier = 0;  // a section of a city
    if (major) *major = tier >= 4;
    return tier * 1e10 + double(std::max<qint64>(population, 0));
}

// ---------------------------------------------------------------- atlas

struct MapLabelAtlas::Page {
    QImage image{PageSize, PageSize, QImage::Format_RGBA8888};
    Texture *texture = nullptr;
    int textureId = -1;
    int shelfY = 0;
    int shelfHeight = 0;
    int cursorX = 0;
    QRect dirty;
};

MapLabelAtlas::MapLabelAtlas() = default;

MapLabelAtlas::~MapLabelAtlas() = default;

QFont MapLabelAtlas::font(bool major) const {
    QFont f;
    f.setPixelSize(std::max(1, int(std::lround((major ? MajorPixels : MinorPixels) * ratio))));
    f.setBold(major);
    return f;
}

QSize MapLabelAtlas::measure(const QString &text, bool major) const {
    const QFontMetrics metrics(font(major));
    const int halo = int(std::ceil(HaloPixels * ratio));
    return QSize(metrics.horizontalAdvance(text) + 2 * halo + 2, metrics.height() + 2 * halo);
}

void MapLabelAtlas::setStyle(const QColor &text, const QColor &halo, float pixelRatio) {
    if (text == textColor && halo == haloColor && pixelRatio == ratio)
        return;
    textColor = text;
    haloColor = halo;
    ratio = pixelRatio;
    clear();
}

void MapLabelAtlas::clear() {
    entries.clear();
    for (auto &page : pages) {
        page->image.fill(Qt::transparent);
        page->shelfY = page->shelfHeight = page->cursorX = 0;
        page->dirty = QRect(0, 0, PageSize, PageSize);
    }
    ++clears;
}

const MapLabelAtlas::Entry *MapLabelAtlas::get(const QString &text, bool major) {
    const QString key = (major ? QStringLiteral("1") : QStringLiteral("0")) + text;
    auto found = entries.constFind(key);
    if (found != entries.constEnd())
        return &found.value();
    return paint(key, text, major);
}

const MapLabelAtlas::Entry *MapLabelAtlas::paint(const QString &key, const QString &text, bool major) {
    const QSize size = measure(text, major);
    if (size.width() > PageSize || size.height() > PageSize)
        return nullptr;
    // Shelves: a row of labels as high as its tallest; a new row when one is full.
    Page *page = nullptr;
    for (auto &p : pages) {
        if (p->cursorX + size.width() > PageSize) {
            p->shelfY += p->shelfHeight;
            p->shelfHeight = 0;
            p->cursorX = 0;
        }
        if (p->shelfY + size.height() <= PageSize) {
            page = p.get();
            break;
        }
    }
    if (page == nullptr) {
        if (int(pages.size()) < MaxPages) {
            pages.push_back(std::make_unique<Page>());
            pages.back()->image.fill(Qt::transparent);
            page = pages.back().get();
        } else {
            return nullptr;  // full: the caller clears and asks again
        }
    }
    const QRect rect(page->cursorX, page->shelfY, size.width(), size.height());
    page->cursorX += size.width();
    page->shelfHeight = std::max(page->shelfHeight, size.height());

    // The text drawn once as a coverage mask (Qt's glyph cache makes this cheap); the
    // halo is that mask grown by the halo radius. Stored straight, as the shader blends.
    QImage mask(size, QImage::Format_ARGB32_Premultiplied);
    mask.fill(Qt::transparent);
    {
        QPainter p(&mask);
        const QFont f = font(major);
        p.setFont(f);
        p.setPen(Qt::white);
        const int halo = int(std::ceil(HaloPixels * ratio));
        p.drawText(halo + 1, halo + QFontMetrics(f).ascent(), text);
    }
    const int w = size.width(), h = size.height();
    std::vector<uint8_t> cover(size_t(w) * h), grown(size_t(w) * h, 0), row(size_t(w) * h, 0);
    for (int y = 0; y < h; ++y) {
        const QRgb *line = reinterpret_cast<const QRgb *>(mask.constScanLine(y));
        for (int x = 0; x < w; ++x) cover[size_t(y) * w + x] = uint8_t(qAlpha(line[x]));
    }
    // Grown by a disc of the halo radius: rows, then columns of a square with round
    // corners is close enough at these sizes; a max filter in two passes.
    const int r = std::max(1, int(std::lround(HaloPixels * ratio * 0.75f)));
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            uint8_t m = 0;
            for (int k = std::max(0, x - r); k <= std::min(w - 1, x + r); ++k) m = std::max(m, cover[size_t(y) * w + k]);
            row[size_t(y) * w + x] = m;
        }
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            uint8_t m = 0;
            for (int k = std::max(0, y - r); k <= std::min(h - 1, y + r); ++k) m = std::max(m, row[size_t(k) * w + x]);
            grown[size_t(y) * w + x] = m;
        }
    const float tr = textColor.redF(), tg = textColor.greenF(), tb = textColor.blueF();
    const float hr = haloColor.redF(), hg = haloColor.greenF(), hb = haloColor.blueF();
    for (int y = 0; y < h; ++y) {
        uchar *out = page->image.scanLine(rect.y() + y) + rect.x() * 4;
        for (int x = 0; x < w; ++x) {
            const float at = cover[size_t(y) * w + x] / 255.0f, ah = grown[size_t(y) * w + x] / 255.0f;
            const float a = at + ah * (1.0f - at);
            if (a <= 0.0f) { std::memset(out + x * 4, 0, 4); continue; }
            const float k = ah * (1.0f - at);
            out[x * 4 + 0] = uchar(std::lround(255.0f * (tr * at + hr * k) / a));
            out[x * 4 + 1] = uchar(std::lround(255.0f * (tg * at + hg * k) / a));
            out[x * 4 + 2] = uchar(std::lround(255.0f * (tb * at + hb * k) / a));
            out[x * 4 + 3] = uchar(std::lround(255.0f * a));
        }
    }
    page->dirty = page->dirty.united(rect);
    Entry entry;
    entry.page = int(std::find_if(pages.begin(), pages.end(), [&](const auto &p) { return p.get() == page; }) - pages.begin());
    entry.rect = rect;
    return &*entries.insert(key, entry);
}

void MapLabelAtlas::upload() {
    for (size_t i = 0; i < pages.size(); ++i) {
        Page &page = *pages[i];
        if (page.texture == nullptr) {
            page.texture = new Texture(PageSize, PageSize, 32);
            page.texture->pathid = QStringLiteral("tsre-map-labels-%1-%2.:atlas")
                    .arg(quintptr(this), 0, 16).arg(i);
            page.textureId = TexLib::addTex(page.texture);
            page.dirty = QRect(0, 0, PageSize, PageSize);
        }
        if (page.dirty.isEmpty())
            continue;
        Texture &t = *page.texture;
        // The page image is the copy kept; the texture's own pixels exist only while it
        // uploads (the first upload frees them).
        auto pixels = [&] {
            if (t.imageData == nullptr)
                t.imageData = new unsigned char[size_t(PageSize) * PageSize * 4];
            std::memcpy(t.imageData, page.image.constBits(), size_t(PageSize) * PageSize * 4);
        };
        if (!t.glLoaded) {
            pixels();
            t.GLTextures();
        } else if (Game::renderBackend == "qrhi") {
            // The changed rows, whole: tightly packed.
            const int y0 = page.dirty.top(), rows = page.dirty.height();
            const QByteArray data(reinterpret_cast<const char *>(page.image.constScanLine(y0)), qsizetype(rows) * PageSize * 4);
            RhiTextures::updateRegion(t.tex[0], 0, y0, PageSize, rows, data);
        } else {
            pixels();
            t.update();
            delete[] t.imageData;
            t.imageData = nullptr;
        }
        page.dirty = QRect();
    }
}

int MapLabelAtlas::textureId(int page) const {
    return page >= 0 && page < int(pages.size()) ? pages[size_t(page)]->textureId : -1;
}

// ---------------------------------------------------------------- placement

std::vector<QPoint> MapLabelLayer::place(const std::vector<Candidate> &candidates, int screenWidth, int screenHeight,
                                         float dotPixels, float gapPixels, float duplicatePixels) {
    std::vector<QPoint> out(candidates.size(), QPoint(-1, -1));
    QHash<unsigned int, std::vector<QPointF>> placedNames;
    // Taken rectangles, found through a grid of cells.
    constexpr int Cell = 64;
    const int cols = std::max(1, (screenWidth + Cell - 1) / Cell), rows = std::max(1, (screenHeight + Cell - 1) / Cell);
    std::vector<std::vector<QRect>> grid(size_t(cols) * rows);
    auto cells = [&](const QRect &r, auto fn) {
        for (int cy = std::max(0, r.top() / Cell); cy <= std::min(rows - 1, r.bottom() / Cell); ++cy)
            for (int cx = std::max(0, r.left() / Cell); cx <= std::min(cols - 1, r.right() / Cell); ++cx)
                if (!fn(grid[size_t(cy) * cols + cx])) return false;
        return true;
    };
    auto isFree = [&](const QRect &r) {
        return cells(r, [&](const std::vector<QRect> &taken) {
            for (const QRect &t : taken) if (t.intersects(r)) return false;
            return true;
        });
    };
    auto take = [&](const QRect &r) { cells(r, [&](std::vector<QRect> &taken) { taken.push_back(r); return true; }); };
    const QRect screen(0, 0, screenWidth, screenHeight);
    const int dot = int(std::ceil(dotPixels)), gap = int(std::ceil(gapPixels));
    for (size_t i = 0; i < candidates.size(); ++i) {
        const Candidate &c = candidates[i];
        const int x = int(std::lround(c.x)), y = int(std::lround(c.y));
        const QRect dotRect(x - dot, y - dot, 2 * dot + 1, 2 * dot + 1);
        if (!screen.intersects(dotRect) || !isFree(dotRect))
            continue;
        if (c.key != 0 && duplicatePixels > 0.0f) {
            bool duplicate = false;
            for (const QPointF &p : placedNames.value(c.key))
                duplicate |= std::hypot(p.x() - c.x, p.y() - c.y) < duplicatePixels;
            if (duplicate)
                continue;
        }
        const int w = c.width, h = c.height, reach = dot + gap;
        const QPoint tries[4] = {{x - w / 2, y - reach - h},  // above
                                 {x + reach, y - h / 2},      // right
                                 {x - w / 2, y + reach + 1},  // below
                                 {x - reach - w, y - h / 2}}; // left
        for (const QPoint &p : tries) {
            const QRect r(p, QSize(w, h));
            if (!screen.contains(r) || !isFree(r)) continue;
            take(r);
            take(dotRect);
            out[i] = p;
            if (c.key != 0)
                placedNames[c.key].push_back(QPointF(c.x, c.y));
            break;
        }
    }
    return out;
}

// ---------------------------------------------------------------- layer

MapLabelLayer::MapLabelLayer() : dotHalos(std::make_unique<OglObj>()) {
    for (auto &d : dots)
        d = std::make_unique<OglObj>();
}

QColor MapLabelLayer::dotColour(const MapPalette &palette, MapLabelKind kind) {
    switch (kind) {
    case MapLabelKind::Station:
    case MapLabelKind::Platform: return palette.platform;
    case MapLabelKind::Siding: return palette.siding;
    case MapLabelKind::Event: return palette.event;
    case MapLabelKind::Place: return palette.place;
    case MapLabelKind::OsmStation: return palette.osmStation;
    case MapLabelKind::Measure: return palette.pointer;
    default: return palette.marker;
    }
}

MapLabelLayer::~MapLabelLayer() = default;

void MapLabelLayer::setLabels(std::vector<MapLabel> next) {
    std::stable_sort(next.begin(), next.end(), [](const MapLabel &a, const MapLabel &b) { return a.priority > b.priority; });
    labels = std::move(next);
    dirty = true;
}

void MapLabelLayer::build(const MapView &view, const MapPalette &palette, float pixelRatio) {
    QElapsedTimer timer;
    timer.start();
    atlas.setStyle(palette.label, palette.labelHalo, pixelRatio);
    // Candidates in priority order: on screen (with a margin for names reaching in).
    std::vector<Candidate> candidates;
    std::vector<size_t> source;
    candidates.reserve(labels.size());
    const float margin = 200.0f * pixelRatio;
    for (size_t i = 0; i < labels.size(); ++i) {
        const MapLabel &l = labels[i];
        if (l.maxMetresPerPixel > 0.0f && view.metresPerPixel > l.maxMetresPerPixel)
            continue;
        float px, py;
        view.screenAt(float(l.tileX - view.tileX) * 2048.0f + l.x, float(l.tileZ - view.tileZ) * 2048.0f + l.z, px, py);
        if (px < -margin || py < -margin || px > view.width + margin || py > view.height + margin)
            continue;
        const QSize size = atlas.measure(l.text, l.major);
        candidates.push_back({px, py, size.width(), size.height(), uint(qHash(l.text)) | 1u});
        source.push_back(i);
    }
    const float dotPixels = DotPixels * pixelRatio, gapPixels = GapPixels * pixelRatio;
    const std::vector<QPoint> corners = place(candidates, view.width, view.height, dotPixels, gapPixels,
                                              DuplicatePixels * pixelRatio);

    // Quads at whole pixels, so text stays sharp; screen corners back to the ground.
    std::vector<std::vector<float>> quads;
    std::vector<float> dotTriangles[int(MapLabelKind::Count)], haloTriangles;
    const float mpp = view.metresPerPixel;
    auto ground = [&](float sx, float sy, float &gx, float &gz) { view.groundAt(sx, sy, gx, gz); };
    placed = 0;
    for (int attempt = 0; attempt < 2; ++attempt) {
        quads.assign(size_t(MapLabelAtlas::MaxPages), {});
        for (auto &d : dotTriangles)
            d.clear();
        haloTriangles.clear();
        placed = 0;
        const int generation = atlas.generation();
        bool full = false;
        for (size_t k = 0; k < candidates.size(); ++k) {
            if (corners[k].x() < 0) continue;  // left out
            const MapLabel &l = labels[source[k]];
            const MapLabelAtlas::Entry *entry = atlas.get(l.text, l.major);
            if (entry == nullptr) { full = true; break; }
            const float x0 = float(corners[k].x()), y0 = float(corners[k].y());
            const float x1 = x0 + entry->rect.width(), y1 = y0 + entry->rect.height();
            const float u0 = float(entry->rect.left()) / MapLabelAtlas::PageSize, v0 = float(entry->rect.top()) / MapLabelAtlas::PageSize;
            const float u1 = float(entry->rect.left() + entry->rect.width()) / MapLabelAtlas::PageSize;
            const float v1 = float(entry->rect.top() + entry->rect.height()) / MapLabelAtlas::PageSize;
            float g[4][2];
            ground(x0, y0, g[0][0], g[0][1]);
            ground(x1, y0, g[1][0], g[1][1]);
            ground(x1, y1, g[2][0], g[2][1]);
            ground(x0, y1, g[3][0], g[3][1]);
            const float uv[4][2] = {{u0, v0}, {u1, v0}, {u1, v1}, {u0, v1}};
            // Wound as the map's ribbons (front faces have a negative cross in x, z).
            const float cross = (g[1][0] - g[0][0]) * (g[2][1] - g[0][1]) - (g[1][1] - g[0][1]) * (g[2][0] - g[0][0]);
            const int order[2][6] = {{0, 2, 1, 0, 3, 2}, {0, 1, 2, 0, 2, 3}};
            std::vector<float> &q = quads[size_t(entry->page)];
            for (int c : order[cross > 0 ? 0 : 1])
                q.insert(q.end(), {g[c][0], TextHeight, g[c][1], uv[c][0], uv[c][1], 0.0f});
            float dx, dz;
            ground(candidates[k].x, candidates[k].y, dx, dz);
            TrackMapLayer::appendOctagon(haloTriangles, dx, DotHaloHeight, dz, (DotPixels + 2.0f) * pixelRatio * mpp);
            TrackMapLayer::appendOctagon(dotTriangles[int(l.kind)], dx, DotHeight, dz, DotPixels * pixelRatio * mpp);
            ++placed;
        }
        // The atlas filled up mid-build: start it again with this view's names only.
        if (!full || attempt == 1 || generation != atlas.generation()) break;
        atlas.clear();
    }
    atlas.upload();
    textObjects.resize(size_t(atlas.pageCount()));
    for (int p = 0; p < atlas.pageCount(); ++p) {
        auto &object = textObjects[size_t(p)];
        if (!object) object = std::make_unique<OglObj>();
        object->setMaterialTextureId(atlas.textureId(p));
        object->init(quads[size_t(p)].data(), int(quads[size_t(p)].size()), RenderItem::VT, GL_TRIANGLES);
    }
    for (int k = 0; k < int(MapLabelKind::Count); ++k) {
        const QColor c = dotColour(palette, MapLabelKind(k));
        dots[k]->setMaterial(float(c.redF()), float(c.greenF()), float(c.blueF()));
        dots[k]->init(dotTriangles[k].data(), int(dotTriangles[k].size()), RenderItem::V, GL_TRIANGLES);
    }
    dotHalos->setMaterial(float(palette.labelHalo.redF()), float(palette.labelHalo.greenF()), float(palette.labelHalo.blueF()));
    dotHalos->init(haloTriangles.data(), int(haloTriangles.size()), RenderItem::V, GL_TRIANGLES);
    static const bool trace = qEnvironmentVariableIsSet("TSRE_MAP_TRACE");
    if (trace)
        qInfo().noquote() << "map-trace labels" << labels.size() << "candidates" << candidates.size() << "placed" << placed
                          << "pages" << atlas.pageCount() << "ms" << timer.nsecsElapsed() / 1e6;
}

void MapLabelLayer::pushRenderItems(RenderQueue &queue, const MapView &view, const MapPalette &palette, float pixelRatio) {
    if (labels.empty())
        return;
    const float now[6] = {view.x, view.z, view.metresPerPixel, view.heading, float(view.width), float(view.height)};
    if (dirty || view.tileX != builtTile[0] || view.tileZ != builtTile[1] || !std::equal(now, now + 6, builtView)
            || palette.name != builtPalette || pixelRatio != builtRatio) {
        build(view, palette, pixelRatio);
        dirty = false;
        builtTile[0] = view.tileX;
        builtTile[1] = view.tileZ;
        std::copy(now, now + 6, builtView);
        builtPalette = palette.name;
        builtRatio = pixelRatio;
    }
    dotHalos->pushRenderItem(queue);
    for (auto &d : dots)
        d->pushRenderItem(queue);
    for (auto &object : textObjects)
        object->pushRenderItem(queue);
}
