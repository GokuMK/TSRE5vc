/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/map/WeatherMapLayer.h>
#include <tsre/environment/Weather.h>
#include <tsre/environment/WeatherDetail.h>
#include <tsre/map/MapPalette.h>
#include <tsre/map/MapView.h>
#include <tsre/ogl/OglObj.h>
#include <tsre/renderer/RenderQueue.h>
#include <tsre/texture/TexLib.h>
#include <tsre/texture/Texture.h>

#include <QImage>
#include <QOpenGLFunctions>
#include <QThreadPool>
#include <algorithm>
#include <cmath>
#include <mutex>
#include <optional>
#include <vector>

struct WeatherMapLayer::Request {
    Weather weather;
    double hours = 0.0;
    double west = 0.0;
    double north = 0.0;
    double size = 0.0;
    int revision = 0;
    QColor colours[WeatherLayer::Count];
};

struct WeatherMapLayer::Shared {
    std::mutex mutex;
    bool running = false;
    std::optional<Request> pending;
    bool hasResult = false;
    QImage result;
    double west = 0.0, north = 0.0, size = 0.0, hours = 0.0;
    int revision = 0;
    std::function<void()> ready;
};

namespace {

// Light backgrounds take dark clouds, dark backgrounds light ones.
bool lightBackground(const MapPalette &palette) {
    return palette.background.lightnessF() > 0.5;
}

QImage makeImage(const WeatherMapLayer::Request &r);

void run(std::shared_ptr<WeatherMapLayer::Shared> shared, WeatherMapLayer::Request request) {
    for (;;) {
        QImage image = makeImage(request);
        std::function<void()> ready;
        {
            std::lock_guard<std::mutex> lock(shared->mutex);
            shared->result = image;
            shared->west = request.west;
            shared->north = request.north;
            shared->size = request.size;
            shared->hours = request.hours;
            shared->revision = request.revision;
            shared->hasResult = true;
            ready = shared->ready;
            if (shared->pending) {
                request = *shared->pending;
                shared->pending.reset();
            } else {
                shared->running = false;
            }
        }
        if (ready)
            ready();
        std::lock_guard<std::mutex> lock(shared->mutex);
        if (!shared->running)
            return;
    }
}

// Straight-alpha "over": colour c at alpha a over what is there.
struct Pixel {
    float r = 0.0f, g = 0.0f, b = 0.0f, a = 0.0f;  // premultiplied
    void over(const QColor &c, float alpha) {
        if (alpha <= 0.0f)
            return;
        r = float(c.redF()) * alpha + r * (1.0f - alpha);
        g = float(c.greenF()) * alpha + g * (1.0f - alpha);
        b = float(c.blueF()) * alpha + b * (1.0f - alpha);
        a = alpha + a * (1.0f - alpha);
    }
};

QImage makeImage(const WeatherMapLayer::Request &r) {
    const int n = WeatherMapLayer::Size;
    QImage image(n, n, QImage::Format_RGBA8888);
    const Weather::Frame frame = r.weather.frame(r.hours);
    const double texel = r.size / n;
    for (int j = 0; j < n; ++j) {
        uchar *line = image.scanLine(j);
        const double north = r.north - (j + 0.5) * texel;
        for (int i = 0; i < n; ++i) {
            const double east = r.west + (i + 0.5) * texel;
            Pixel p;
            // Zoomed out, the coverage itself (as a weather app shows
            // cloudiness); zoomed in, the clouds, their edges at least a
            // texel soft so the image does not step.
            auto cover = [&](int layer) {
                const WeatherLayerValues values = r.weather.layer(frame, east, north, layer);
                const double detail = WeatherDetail::imageDetail(texel, values.type);
                if (detail <= 0.0)
                    return values.coverage;
                const double clouds = WeatherDetail::cover(r.weather, frame, east, north, layer,
                                                           WeatherDetail::imageEdge(texel, values.type), texel);
                return float(values.coverage + (clouds - values.coverage) * detail);
            };
            // High clouds hatched, so the layers under them show.
            p.over(r.colours[WeatherLayer::High], cover(WeatherLayer::High) * (((i + j) % 6) < 2 ? 0.4f : 0.1f));
            p.over(r.colours[WeatherLayer::Mid], cover(WeatherLayer::Mid) * 0.45f);
            p.over(r.colours[WeatherLayer::Low], cover(WeatherLayer::Low) * 0.6f);
            const WeatherSurface surface = WeatherDetail::precipitation(
                    r.weather, frame, east, north,
                    WeatherDetail::imageDetail(texel, r.weather.layer(frame, east, north, WeatherLayer::Low).type));
            if (surface.precipitationRate >= 0.1f)
                p.over(WeatherMapLayer::precipitationColour(surface.precipitationRate,
                                                            surface.precipitation == WeatherPrecipitation::Snow),
                       std::min(0.5f, surface.precipitationRate * 2.0f));
            uchar *out = line + i * 4;
            if (p.a <= 0.0f) {
                out[0] = out[1] = out[2] = out[3] = 0;
                continue;
            }
            out[0] = uchar(std::clamp(p.r / p.a * 255.0f + 0.5f, 0.0f, 255.0f));
            out[1] = uchar(std::clamp(p.g / p.a * 255.0f + 0.5f, 0.0f, 255.0f));
            out[2] = uchar(std::clamp(p.b / p.a * 255.0f + 0.5f, 0.0f, 255.0f));
            out[3] = uchar(std::clamp(p.a * 255.0f + 0.5f, 0.0f, 255.0f));
        }
    }
    return image;
}

// The editor's position of an MSTS world point, relative to the view's tile.
void toView(const MapView &view, double east, double north, float &x, float &z) {
    x = float(east - view.tileX * 2048.0);
    z = float(-north - view.tileZ * 2048.0);
}

// Both windings, so culling never hides it.
void appendTriangle(std::vector<float> &out, const float *a, const float *b, const float *c, float y) {
    for (const float *p : {a, b, c, a, c, b})
        out.insert(out.end(), {p[0], y, p[1]});
}

void appendQuad(std::vector<float> &out, const float *a, const float *b, const float *c, const float *d, float y) {
    appendTriangle(out, a, b, c, y);
    appendTriangle(out, a, c, d, y);
}

void setColour(OglObj &object, const QColor &colour, float alpha) {
    object.setMaterial(float(colour.redF()), float(colour.greenF()), float(colour.blueF()), alpha);
}

} // namespace

WeatherMapLayer::WeatherMapLayer() : shared(std::make_shared<Shared>()) {
}

WeatherMapLayer::~WeatherMapLayer() {
    std::lock_guard<std::mutex> lock(shared->mutex);
    shared->ready = nullptr;
    shared->pending.reset();
}

void WeatherMapLayer::setReadyCallback(std::function<void()> callback) {
    std::lock_guard<std::mutex> lock(shared->mutex);
    shared->ready = std::move(callback);
}

void WeatherMapLayer::invalidate() {
    ++revision;
}

bool WeatherMapLayer::busy() const {
    std::lock_guard<std::mutex> lock(shared->mutex);
    return shared->running;
}

QColor WeatherMapLayer::cloudColour(const MapPalette &palette, int layer) {
    const bool light = lightBackground(palette);
    switch (layer) {
    case WeatherLayer::Low: return light ? QColor(70, 80, 96) : QColor(240, 242, 248);
    case WeatherLayer::Mid: return light ? QColor(50, 100, 185) : QColor(150, 190, 255);
    default: return light ? QColor(100, 100, 150) : QColor(200, 200, 235);
    }
}

QColor WeatherMapLayer::precipitationColour(float rate, bool snow) {
    if (snow) {
        if (rate < 1.0f) return QColor(215, 195, 255);
        if (rate < 3.0f) return QColor(175, 135, 240);
        return QColor(135, 75, 215);
    }
    // Radar colours: light to heavy.
    if (rate < 0.5f) return QColor(150, 215, 255);
    if (rate < 2.0f) return QColor(55, 165, 240);
    if (rate < 5.0f) return QColor(40, 195, 85);
    if (rate < 10.0f) return QColor(245, 215, 40);
    if (rate < 30.0f) return QColor(245, 130, 30);
    return QColor(215, 30, 30);
}

void WeatherMapLayer::request(const Weather &weather, double hours, double west, double north, double size,
                              const MapPalette &palette) {
    Request r;
    r.weather = weather;
    r.hours = hours;
    r.west = west;
    r.north = north;
    r.size = size;
    r.revision = revision;
    for (int l = 0; l < WeatherLayer::Count; ++l)
        r.colours[l] = cloudColour(palette, l);
    std::lock_guard<std::mutex> lock(shared->mutex);
    if (shared->running) {
        shared->pending = r;
        return;
    }
    shared->running = true;
    std::shared_ptr<Shared> s = shared;
    QThreadPool::globalInstance()->start([s, r] { run(s, r); });
}

void WeatherMapLayer::takeResult() {
    QImage result;
    {
        std::lock_guard<std::mutex> lock(shared->mutex);
        if (!shared->hasResult)
            return;
        shared->hasResult = false;
        result = shared->result;
        shownWest = shared->west;
        shownNorth = shared->north;
        shownSize = shared->size;
        shownHours = shared->hours;
        shownRevision = shared->revision;
    }
    if (!texture) {
        texture = new Texture(Size, Size, 32);
        texture->pathid = QStringLiteral("tsre-map-weather-%1.:atlas").arg(quintptr(this), 0, 16);
        textureId = TexLib::addTex(texture);
        texture->GLTextures();
    }
    texture->updateRegion(0, 0, Size, Size, result.constBits());
    shown = true;
}

void WeatherMapLayer::pushRenderItems(RenderQueue &queue, const MapView &view, const MapPalette &palette,
                                      const Weather &weather, double hours, bool cells) {
    takeResult();
    // The ground the view shows, as a square about its centre (MSTS metres).
    double minE = 1e300, maxE = -1e300, minN = 1e300, maxN = -1e300;
    for (int c = 0; c < 4; ++c) {
        float gx, gz;
        view.groundAt(c & 1 ? float(view.width) : 0.0f, c & 2 ? float(view.height) : 0.0f, gx, gz);
        const double east = view.tileX * 2048.0 + gx, north = -(view.tileZ * 2048.0 + gz);
        minE = std::min(minE, east);
        maxE = std::max(maxE, east);
        minN = std::min(minN, north);
        maxN = std::max(maxN, north);
    }
    const double viewSize = std::max(maxE - minE, maxN - minN);
    const double centreE = (minE + maxE) * 0.5, centreN = (minN + maxN) * 0.5;
    // A new image when the shown one does not cover the view, is far too
    // coarse or too wide for it, or is of another time or weather; asked for
    // once for each such need, not every frame while it is made.
    auto fits = [&](double west, double north, double size, double atHours, int atRevision) {
        return minE >= west && maxE <= west + size && maxN <= north && minN >= north - size
                && size <= viewSize * 3.0 && size >= viewSize * 1.05 && atRevision == revision
                && std::abs(atHours - hours) < 1.0 / 120.0;
    };
    if (!(shown && fits(shownWest, shownNorth, shownSize, shownHours, shownRevision))
            && !(requested && fits(requestedWest, requestedNorth, requestedSize, requestedHours, requestedRevision))) {
        const double size = viewSize * 1.6;
        requestedWest = centreE - size * 0.5;
        requestedNorth = centreN + size * 0.5;
        requestedSize = size;
        requestedHours = hours;
        requestedRevision = revision;
        requested = true;
        request(weather, hours, requestedWest, requestedNorth, size, palette);
    }

    if (shown) {
        if (!image)
            image = std::make_unique<OglObj>();
        float x0, z0, x1, z1;
        toView(view, shownWest, shownNorth, x0, z0);
        toView(view, shownWest + shownSize, shownNorth - shownSize, x1, z1);
        // North-west, north-east, south-east, south-west; wound as the
        // terrain map's squares.
        const float corners[4][4] = {{x0, z0, 0.0f, 0.0f}, {x1, z0, 1.0f, 0.0f}, {x1, z1, 1.0f, 1.0f},
                                     {x0, z1, 0.0f, 1.0f}};
        std::vector<float> vertices;
        for (int c : {0, 3, 2, 0, 2, 1})
            vertices.insert(vertices.end(), {corners[c][0], Height, corners[c][1], corners[c][2], corners[c][3], 0.0f});
        image->setMaterialTextureId(textureId);
        image->setOpacity(0.99f);
        image->init(vertices.data(), int(vertices.size()), RenderItem::VT, GL_TRIANGLES);
        image->pushRenderItem(queue);
    }

    // The weather's cells, when they are at least 8 pixels wide.
    const QColor line = lightBackground(palette) ? QColor(60, 70, 90) : QColor(220, 225, 240);
    if (cells && weather.grid.valid && weather.grid.cellSize / view.metresPerPixel >= 8.0f) {
        std::vector<float> lines;
        const WeatherGrid &g = weather.grid;
        const float half = 0.6f * view.metresPerPixel;
        float ax, az, bx, bz;
        for (int c = 0; c <= g.columns; ++c) {
            const double east = g.west() + c * double(g.cellSize);
            if (east < minE - g.cellSize || east > maxE + g.cellSize)
                continue;
            toView(view, east, g.north(), ax, az);
            toView(view, east, g.north() - g.rows * double(g.cellSize), bx, bz);
            const float a[2] = {ax - half, az}, b[2] = {ax + half, az}, c2[2] = {bx + half, bz}, d[2] = {bx - half, bz};
            appendQuad(lines, a, b, c2, d, CellHeight);
        }
        for (int r = 0; r <= g.rows; ++r) {
            const double north = g.north() - r * double(g.cellSize);
            if (north < minN - g.cellSize || north > maxN + g.cellSize)
                continue;
            toView(view, g.west(), north, ax, az);
            toView(view, g.west() + g.columns * double(g.cellSize), north, bx, bz);
            const float a[2] = {ax, az - half}, b[2] = {bx, bz - half}, c2[2] = {bx, bz + half}, d[2] = {ax, az + half};
            appendQuad(lines, a, b, c2, d, CellHeight);
        }
        if (!lines.empty()) {
            if (!cellLines)
                cellLines = std::make_unique<OglObj>();
            setColour(*cellLines, line, 0.5f);
            cellLines->init(lines.data(), int(lines.size()), RenderItem::V, GL_TRIANGLES);
            cellLines->pushRenderItem(queue);
        }
    }

    // The low clouds' wind: arrows every 150 pixels, pointing where it blows.
    const WeatherWind wind = weather.wind(hours, WeatherLayer::Low);
    if (!weather.keyframes.isEmpty() && wind.speed > 0.3f) {
        double towardsE, towardsN;
        Weather::velocity(wind, towardsE, towardsN);
        const double length = std::hypot(towardsE, towardsN);
        // Along the arrow and across it, in the editor's x and z.
        const float ux = float(towardsE / length), uz = float(-towardsN / length);
        const float vx = -uz, vz = ux;
        const float m = view.metresPerPixel;
        std::vector<float> shapes;
        for (int py = 75; py < view.height; py += 150)
            for (int px = 75; px < view.width; px += 150) {
                float gx, gz;
                view.groundAt(float(px), float(py), gx, gz);
                const float tail = -18.0f * m, tip = 18.0f * m, neck = 6.0f * m, w = 1.5f * m, head = 6.0f * m;
                const float a[2] = {gx + ux * tail + vx * w, gz + uz * tail + vz * w};
                const float b[2] = {gx + ux * neck + vx * w, gz + uz * neck + vz * w};
                const float c[2] = {gx + ux * neck - vx * w, gz + uz * neck - vz * w};
                const float d[2] = {gx + ux * tail - vx * w, gz + uz * tail - vz * w};
                appendQuad(shapes, a, b, c, d, ArrowHeight);
                const float e[2] = {gx + ux * neck + vx * head, gz + uz * neck + vz * head};
                const float f[2] = {gx + ux * tip, gz + uz * tip};
                const float h[2] = {gx + ux * neck - vx * head, gz + uz * neck - vz * head};
                appendTriangle(shapes, e, f, h, ArrowHeight);
            }
        if (!shapes.empty()) {
            if (!arrows)
                arrows = std::make_unique<OglObj>();
            setColour(*arrows, line, 0.75f);
            arrows->init(shapes.data(), int(shapes.size()), RenderItem::V, GL_TRIANGLES);
            arrows->pushRenderItem(queue);
        }
    }
}
