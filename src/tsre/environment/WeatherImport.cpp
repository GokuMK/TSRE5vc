/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/environment/WeatherImport.h>

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QStandardPaths>
#include <QThread>
#include <QUrl>
#include <QUrlQuery>
#include <algorithm>
#include <cmath>

namespace {

const char *const Hourly = "cloud_cover_low,cloud_cover_mid,cloud_cover_high,precipitation,snowfall,"
                           "temperature_2m,dew_point_2m,wind_speed_100m,wind_direction_100m,weather_code";

QVector<double> numbers(const QJsonObject &hourly, const char *name) {
    QVector<double> out;
    for (const QJsonValue &v : hourly.value(QLatin1String(name)).toArray())
        out.push_back(v.isDouble() ? v.toDouble() : std::nan(""));
    return out;
}

double at(const QVector<double> &values, int hour, double fallback = 0.0) {
    if (hour < 0 || hour >= values.size() || std::isnan(values[hour]))
        return fallback;
    return values[hour];
}

// What one sample says about one hour.
struct Hour {
    WeatherLayerValues layers[WeatherLayer::Count];
    double precipitation = 0.0;  // mm/h
    double snowShare = 0.0;
    double visibility = 40000.0; // m
    double windEast = 0.0, windNorth = 0.0;  // m/s towards, true north
};

Hour hourOf(const WeatherImport::Series &s, int hour) {
    Hour h;
    const double low = std::clamp(at(s.low, hour) / 100.0, 0.0, 1.0);
    const double mid = std::clamp(at(s.mid, hour) / 100.0, 0.0, 1.0);
    const double high = std::clamp(at(s.high, hour) / 100.0, 0.0, 1.0);
    int code = hour >= 0 && hour < s.weatherCode.size() ? s.weatherCode[hour] : -1;
    if (code < 0) {
        // Without a weather code: steady rain or snow when it falls, else fair.
        const double rate = at(s.precipitation, hour);
        code = rate >= 0.1 ? (at(s.snowfall, hour) > 0.0 ? 73 : 61) : 0;
    }
    const bool fog = code == 45 || code == 48;
    const bool steady = (code >= 51 && code <= 67) || (code >= 71 && code <= 77);
    const bool showers = code >= 80 && code <= 86;
    const bool thunder = code >= 95 && code <= 99;
    // Low clouds: their type from the weather, else from their cover.
    double lowType = low < 0.5 ? 0.67 : low < 0.85 ? 0.33 : 0.0;
    if (steady)
        lowType = 0.0;
    if (showers)
        lowType = 0.67;
    if (thunder)
        lowType = 1.0;
    // The base where rising air condenses: about 125 m a degree of dew
    // point spread above the ground.
    const double t = at(s.temperature, hour, 10.0), td = at(s.dewPoint, hour, t - 5.0);
    double base = s.elevation + std::clamp(125.0 * (t - td), 100.0, 2500.0);
    if (fog)
        base = s.elevation + 30.0;
    const double depth = thunder ? 10000.0 - base : lowType >= 0.6 ? 1500.0 : lowType >= 0.3 ? 800.0
                       : steady ? 1500.0 : 600.0;
    h.layers[WeatherLayer::Low] = {float(lowType), float(low), float(base), float(base + std::max(depth, 300.0)), 1.0f};
    h.layers[WeatherLayer::Mid] = {mid > 0.7 ? 0.0f : 0.6f, float(mid), 3000.0f, steady ? 5000.0f : 4300.0f, 0.8f};
    h.layers[WeatherLayer::High] = {high > 0.7 ? 0.0f : 0.5f, float(high), 8500.0f, 9500.0f, 0.3f};
    h.precipitation = std::max(0.0, at(s.precipitation, hour));
    // Snowfall in cm: about 7 cm of snow to a mm of water.
    const double snowWater = std::max(0.0, at(s.snowfall, hour)) * 10.0 / 7.0;
    h.snowShare = h.precipitation > 0.0 ? std::clamp(snowWater / h.precipitation, 0.0, 1.0) : 0.0;
    h.visibility = fog ? 300.0
                 : std::max(500.0, 40000.0 / (1.0 + h.precipitation * (1.5 + 6.0 * h.snowShare)));
    const double speed = std::max(0.0, at(s.windSpeed, hour));
    const double towards = (at(s.windDirection, hour, 270.0) + 180.0) * M_PI / 180.0;
    h.windEast = std::sin(towards) * speed;
    h.windNorth = std::cos(towards) * speed;
    return h;
}

QString cacheFile(const QString &url) {
    const QString directory = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/weather";
    QDir().mkpath(directory);
    const QByteArray key = QCryptographicHash::hash(url.toUtf8(), QCryptographicHash::Sha1).toHex();
    return directory + "/" + QString::fromLatin1(key) + ".json";
}

} // namespace

QVector<WeatherImport::Model> WeatherImport::models() {
    const QString openMeteo = QStringLiteral("Weather data by Open-Meteo.com (CC BY 4.0); ");
    return {
        {"meteofrance_arome_france_hd", "AROME France HD (1.5 km, France)", 1500.0, openMeteo + "Meteo-France"},
        {"ukmo_uk_deterministic_2km", "UKMO (2 km, UK and Ireland)", 2000.0, openMeteo + "UK Met Office"},
        {"dmi_harmonie_arome_europe", "DMI HARMONIE (2 km, northern and central Europe)", 2000.0, openMeteo + "DMI"},
        {"icon_d2", "ICON-D2 (2 km, Germany and neighbours)", 2200.0, openMeteo + "DWD"},
        {"knmi_harmonie_arome_europe", "KNMI HARMONIE (5.5 km, Europe)", 5500.0, openMeteo + "KNMI"},
        {"icon_eu", "ICON-EU (7 km, Europe)", 7000.0, openMeteo + "DWD"},
        {"ecmwf_ifs", "ECMWF IFS (9 km)", 9000.0, openMeteo + "ECMWF"},
        {"era5", "ERA5 (25 km, from 1940)", 25000.0,
         openMeteo + "contains modified Copernicus Climate Change Service information", true},
    };
}

WeatherImport::Model WeatherImport::model(const QString &id) {
    for (const Model &m : models())
        if (m.id == id)
            return m;
    return models().front();
}

bool WeatherImport::samples(const Request &r, const ToLatLon &toLatLon, QVector<Sample> &out, int &columns,
                            int &rows, QString &error) {
    out.clear();
    const double spacing = WeatherImport::spacing(r);
    if (!(r.east > r.west) || !(r.north > r.south)) {
        error = QStringLiteral("The area is empty");
        return false;
    }
    columns = int(std::ceil((r.east - r.west) / spacing)) + 1;
    rows = int(std::ceil((r.north - r.south) / spacing)) + 1;
    // Each point counts towards Open-Meteo's free daily limit (10000).
    if (columns * rows > 3000) {
        error = QString("%1 sample points; at most 3000 (wider spacing, a smaller area or margin)")
                .arg(columns * rows);
        return false;
    }
    for (int j = 0; j < rows; ++j)
        for (int i = 0; i < columns; ++i) {
            Sample s;
            s.east = r.west + i * spacing;
            s.north = r.north - j * spacing;
            if (!toLatLon(s.east, s.north, s.latitude, s.longitude)) {
                error = QStringLiteral("The route has no geographic reference");
                return false;
            }
            out.push_back(s);
        }
    return true;
}

double WeatherImport::spacing(const Request &r) {
    return r.spacing > 0.0 ? r.spacing : std::max(model(r.model).resolution, 4000.0);
}

bool WeatherImport::cacheable(const Request &r) {
    // Days fully past: the archives do not change them.
    const QDate last = r.date.addDays((r.startHour + r.hours) / 24);
    return last < QDate::currentDate().addDays(-2);
}

QString WeatherImport::url(const Request &r, const QVector<Sample> &samples, int first, int count) {
    if (count < 0)
        count = samples.size() - first;
    QStringList latitudes, longitudes;
    for (int i = first; i < first + count && i < samples.size(); ++i) {
        const Sample &s = samples[i];
        latitudes << QString::number(s.latitude, 'f', 4);
        longitudes << QString::number(s.longitude, 'f', 4);
    }
    const int lastHour = r.startHour + r.hours;
    const QDate last = r.date.addDays(lastHour / 24);
    QUrlQuery query;
    query.addQueryItem("latitude", latitudes.join(','));
    query.addQueryItem("longitude", longitudes.join(','));
    query.addQueryItem("start_date", r.date.toString(Qt::ISODate));
    query.addQueryItem("end_date", last.toString(Qt::ISODate));
    query.addQueryItem("hourly", QString::fromLatin1(Hourly));
    query.addQueryItem("models", r.model);
    query.addQueryItem("wind_speed_unit", "ms");
    query.addQueryItem("timezone", "GMT");
    // ERA5 from the reanalysis archive; the others from the forecast
    // archive for past days, from the forecast service for recent and
    // coming ones.
    const QString endpoint = model(r.model).reanalysis ? "https://archive-api.open-meteo.com/v1/archive"
                           : cacheable(r) ? "https://historical-forecast-api.open-meteo.com/v1/forecast"
                                          : "https://api.open-meteo.com/v1/forecast";
    QUrl u(endpoint);
    u.setQuery(query);
    return u.toString(QUrl::FullyEncoded);
}

bool WeatherImport::parse(const QByteArray &json, int sampleCount, QVector<Series> &out, QString &error) {
    out.clear();
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (document.isNull()) {
        error = "Not JSON: " + parseError.errorString();
        return false;
    }
    // One location gives an object, several a list.
    QJsonArray locations;
    if (document.isArray()) {
        locations = document.array();
    } else {
        const QJsonObject object = document.object();
        if (object.value("error").toBool()) {
            error = "Open-Meteo: " + object.value("reason").toString();
            return false;
        }
        locations.append(object);
    }
    if (locations.size() != sampleCount) {
        error = QString("%1 locations answered, %2 asked").arg(locations.size()).arg(sampleCount);
        return false;
    }
    for (const QJsonValue &value : locations) {
        const QJsonObject location = value.toObject();
        const QJsonObject hourly = location.value("hourly").toObject();
        Series s;
        s.elevation = location.value("elevation").toDouble(0.0);
        s.low = numbers(hourly, "cloud_cover_low");
        s.mid = numbers(hourly, "cloud_cover_mid");
        s.high = numbers(hourly, "cloud_cover_high");
        s.precipitation = numbers(hourly, "precipitation");
        s.snowfall = numbers(hourly, "snowfall");
        s.temperature = numbers(hourly, "temperature_2m");
        s.dewPoint = numbers(hourly, "dew_point_2m");
        s.windSpeed = numbers(hourly, "wind_speed_100m");
        s.windDirection = numbers(hourly, "wind_direction_100m");
        // -1: the model has none (AROME HD).
        for (double code : numbers(hourly, "weather_code"))
            s.weatherCode.push_back(std::isnan(code) ? -1 : int(code));
        if (s.low.isEmpty()) {
            error = QStringLiteral("The answer has no hourly cloud cover");
            return false;
        }
        out.push_back(s);
    }
    return true;
}

bool WeatherImport::build(const Request &r, const QVector<Sample> &samples, int columns, int rows,
                          const QVector<Series> &series, double gridBearing, Weather &weather, QString &error) {
    if (samples.size() != columns * rows || series.size() != samples.size() || columns < 1 || rows < 1) {
        error = QStringLiteral("Samples and answers do not match");
        return false;
    }
    const Model m = model(r.model);
    const double spacing = WeatherImport::spacing(r);
    weather = Weather();
    weather.seed = 1;
    weather.source = QString("Open-Meteo %1, %2 %3:00 UTC").arg(m.id, r.date.toString(Qt::ISODate))
            .arg(r.startHour, 2, 10, QChar('0'));
    weather.attribution = m.attribution + " " + QString::number(r.date.year());
    weather.start = QDateTime(r.date, QTime(r.startHour, 0), Qt::UTC);

    // The grid: its north-west corner on the tile corner at or outside the
    // area's.
    WeatherGrid &g = weather.grid;
    g.cellSize = r.cellSize;
    g.tileX = int(std::floor((r.west + 1024.0) / 2048.0));
    g.tileZ = int(std::ceil((r.north - 1024.0) / 2048.0));
    g.columns = std::max(1, int(std::ceil((r.east - g.west()) / g.cellSize)));
    g.rows = std::max(1, int(std::ceil((g.north() - r.south) / g.cellSize)));
    g.valid = true;

    const int keyframes = r.hours + 1;
    QVector<QVector<Hour>> hours(keyframes);
    for (int k = 0; k < keyframes; ++k)
        for (const Series &s : series)
            hours[k].push_back(hourOf(s, r.startHour + k));

    // Keyframe values: the samples' means; wind the mean vector, turned from
    // true north to the grid's.
    for (int k = 0; k < keyframes; ++k) {
        WeatherKeyframe f;
        f.time = float(k);
        // The data varies by itself; the noise only fills between coarse samples.
        f.variationAmount = float(std::min(0.1, 0.1 * spacing / 25000.0));
        f.variationScale = 15000.0f;
        double e = 0.0, n = 0.0, rate = 0.0, snow = 0.0, visibility = 0.0;
        double sums[WeatherLayer::Count][5] = {};
        for (const Hour &h : hours[k]) {
            e += h.windEast;
            n += h.windNorth;
            rate += h.precipitation;
            snow += h.snowShare * h.precipitation;
            visibility += h.visibility;
            for (int l = 0; l < WeatherLayer::Count; ++l) {
                sums[l][0] += h.layers[l].type;
                sums[l][1] += h.layers[l].coverage;
                sums[l][2] += h.layers[l].base;
                sums[l][3] += h.layers[l].top;
                sums[l][4] += h.layers[l].density;
            }
        }
        const double count = hours[k].size();
        const double speed = std::hypot(e, n) / count;
        double from = std::atan2(e, n) * 180.0 / M_PI + 180.0 - gridBearing;
        from = std::fmod(std::fmod(from, 360.0) + 360.0, 360.0);
        f.wind[WeatherLayer::Low] = {float(from), float(speed)};
        f.wind[WeatherLayer::Mid] = {float(std::fmod(from + 10.0, 360.0)), float(speed * 2.0)};
        f.wind[WeatherLayer::High] = {float(std::fmod(from + 20.0, 360.0)), float(speed * 3.5)};
        f.precipitationRate = float(rate / count);
        // Below a trace (0.05 mm/h) it does not rain.
        f.precipitation = rate / count < 0.05 ? WeatherPrecipitation::None
                        : snow / rate >= 0.5 ? WeatherPrecipitation::Snow : WeatherPrecipitation::Rain;
        f.visibility = float(visibility / count);
        for (int l = 0; l < WeatherLayer::Count; ++l)
            f.layers[l] = {float(sums[l][0] / count), float(sums[l][1] / count), float(sums[l][2] / count),
                           float(sums[l][3] / count), float(sums[l][4] / count)};
        weather.keyframes.push_back(f);
    }

    // The maps: each cell's centre between the four samples around it.
    weather.allocateMaps();
    for (int k = 0; k < keyframes; ++k)
        for (int row = 0; row < g.rows; ++row)
            for (int column = 0; column < g.columns; ++column) {
                const double east = g.west() + (column + 0.5) * g.cellSize;
                const double north = g.north() - (row + 0.5) * g.cellSize;
                const double fx = std::clamp((east - r.west) / spacing, 0.0, columns - 1.0);
                const double fy = std::clamp((r.north - north) / spacing, 0.0, rows - 1.0);
                const int i0 = int(std::floor(fx)), j0 = int(std::floor(fy));
                const int i1 = std::min(i0 + 1, columns - 1), j1 = std::min(j0 + 1, rows - 1);
                const double wx = fx - i0, wy = fy - j0;
                const Hour *corner[4] = {&hours[k][j0 * columns + i0], &hours[k][j0 * columns + i1],
                                         &hours[k][j1 * columns + i0], &hours[k][j1 * columns + i1]};
                const double weight[4] = {(1 - wx) * (1 - wy), wx * (1 - wy), (1 - wx) * wy, wx * wy};
                auto mix = [&](auto value) {
                    double sum = 0.0;
                    for (int c = 0; c < 4; ++c)
                        sum += weight[c] * value(*corner[c]);
                    return sum;
                };
                auto put = [&](int index, double a, double b, double c, double d) {
                    auto *line = reinterpret_cast<QRgba64 *>(
                            weather.mapImage(index).scanLine(k * g.rows + row));
                    auto q = [](double v) { return quint16(std::clamp(std::lround(v), 0L, 65535L)); };
                    line[column] = QRgba64::fromRgba64(q(a), q(b), q(c), q(d));
                };
                for (int l = 0; l < WeatherLayer::Count; ++l)
                    put(l, mix([l](const Hour &h) { return h.layers[l].coverage; }) * 65535.0,
                        mix([l](const Hour &h) { return h.layers[l].type; }) * 65535.0,
                        mix([l](const Hour &h) { return h.layers[l].base; }),
                        mix([l](const Hour &h) { return h.layers[l].top; }));
                put(Weather::SurfaceMap, mix([](const Hour &h) { return h.precipitation; }) / 0.01,
                    mix([](const Hour &h) { return h.snowShare; }) * 65535.0,
                    mix([](const Hour &h) { return h.visibility; }) / 2.0, 65535.0);
            }
    return true;
}

bool WeatherImport::fetch(const QString &url, QByteArray &out, QString &error, const std::function<bool()> &cancelled,
                          bool cache) {
    const QString cached = cacheFile(url);
    QFile file(cached);
    if (cache && file.open(QIODevice::ReadOnly)) {
        out = file.readAll();
        if (!out.isEmpty())
            return true;
    }
    QNetworkAccessManager manager;
    QNetworkRequest request{QUrl(url)};
    request.setHeader(QNetworkRequest::UserAgentHeader, "TSRE5 route editor");
    request.setTransferTimeout(120000);
    QNetworkReply *reply = manager.get(request);
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    while (!reply->isFinished()) {
        loop.processEvents(QEventLoop::AllEvents, 50);
        if (cancelled && cancelled()) {
            reply->abort();
            reply->deleteLater();
            error = QStringLiteral("Cancelled");
            return false;
        }
        if (!reply->isFinished())
            QThread::msleep(10);
    }
    out = reply->readAll();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (reply->error() != QNetworkReply::NoError && out.isEmpty()) {
        error = reply->errorString();
        reply->deleteLater();
        return false;
    }
    reply->deleteLater();
    if (status != 200) {
        // Open-Meteo explains its errors in JSON.
        const QJsonObject object = QJsonDocument::fromJson(out).object();
        error = QString("Open-Meteo answered %1: %2").arg(status)
                .arg(object.value("reason").toString(QString::fromUtf8(out.left(200))));
        return false;
    }
    QSaveFile save(cached);
    if (cache && save.open(QIODevice::WriteOnly)) {
        save.write(out);
        save.commit();
    }
    return true;
}

namespace {

// Open-Meteo's free use counts every location of a request as a call: at
// most 600 a minute. The downloads keep under 500 a minute and wait out the
// rest of the minute when they would go over (or when told they did).
class MinuteLimit {
public:
    static constexpr int PerMinute = 500;
    // Waits until count more fit this minute; false when cancelled.
    bool reserve(int count, const std::function<bool()> &cancelled) {
        if (!started.isValid() || started.elapsed() >= 60000) {
            started.start();
            used = 0;
        }
        if (used + count > PerMinute && !waitMinute(cancelled))
            return false;
        used += count;
        return true;
    }
    bool waitMinute(const std::function<bool()> &cancelled) {
        while (started.isValid() && started.elapsed() < 61000) {
            if (cancelled && cancelled())
                return false;
            QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
            QThread::msleep(100);
        }
        started.start();
        used = 0;
        return true;
    }
private:
    QElapsedTimer started;
    int used = 0;
};

MinuteLimit &minuteLimit() {
    static MinuteLimit limit;
    return limit;
}

} // namespace

bool WeatherImport::download(const Request &r, const QVector<Sample> &samples, QVector<Series> &out, QString &error,
                             const std::function<bool()> &cancelled) {
    out.clear();
    constexpr int Batch = 200;
    const bool cache = cacheable(r);
    for (int first = 0; first < samples.size(); first += Batch) {
        const int count = std::min(Batch, int(samples.size()) - first);
        const QString address = url(r, samples, first, count);
        QByteArray json;
        QVector<Series> part;
        bool fetched = false;
        for (int attempt = 0; attempt < 3 && !fetched; ++attempt) {
            // Answers from the disk cache do not count.
            if (!(cache && QFile::exists(cacheFile(address))) && !minuteLimit().reserve(count, cancelled)) {
                error = QStringLiteral("Cancelled");
                return false;
            }
            fetched = fetch(address, json, error, cancelled, cache);
            // 429: the minute's limit was reached after all; wait it out.
            if (!fetched && error.contains("429") && !minuteLimit().waitMinute(cancelled)) {
                error = QStringLiteral("Cancelled");
                return false;
            }
            if (!fetched && !error.contains("429"))
                return false;
        }
        if (!fetched || !parse(json, count, part, error))
            return false;
        out += part;
    }
    return true;
}

bool WeatherImport::chooseModel(const Request &r, const ToLatLon &toLatLon, QString &chosen, QString &error,
                                const std::function<bool()> &cancelled) {
    // The area's centre and corners must all have data.
    QVector<Sample> probe;
    for (const auto &p : {std::pair<double, double>{(r.west + r.east) * 0.5, (r.north + r.south) * 0.5},
                          {r.west, r.north}, {r.east, r.north}, {r.west, r.south}, {r.east, r.south}}) {
        Sample s;
        s.east = p.first;
        s.north = p.second;
        if (!toLatLon(s.east, s.north, s.latitude, s.longitude)) {
            error = QStringLiteral("The route has no geographic reference");
            return false;
        }
        probe.push_back(s);
    }
    Request one = r;
    one.hours = 0;
    QString lastError;
    for (const Model &m : models()) {
        if (cancelled && cancelled()) {
            error = QStringLiteral("Cancelled");
            return false;
        }
        one.model = m.id;
        QByteArray json;
        QVector<Series> series;
        QString message;
        if (!fetch(url(one, probe), json, message, cancelled, cacheable(one)) || !parse(json, probe.size(), series, message)) {
            lastError = message;
            continue;
        }
        bool covered = true;
        for (const Series &s : series)
            covered = covered && std::isfinite(at(s.low, r.startHour, std::nan("")));
        if (covered) {
            chosen = m.id;
            return true;
        }
    }
    error = lastError.isEmpty() ? QStringLiteral("No model has data for this area and day") : lastError;
    return false;
}
