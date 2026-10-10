#include <tsre/tests/WeatherTestSuite.h>

#include <tsre/environment/EnvFile.h>
#include <tsre/environment/WeatherDetail.h>
#include <tsre/environment/WeatherPresets.h>

#include <QDebug>
#include <QDirIterator>
#include <QFile>
#include <QTemporaryDir>
#include <cmath>

namespace {

const char *const Sample = R"(SIMISA@@@@@@@@@@JINX0w0t______

world (
	world_fog_distance ( 450 )
	world_fog_day_colour ( ffffffff )
	world_fog_night_colour ( ff000000 )
	world_sky (
		world_sky_nlayers_behind_satellites ( 2 )
		world_sky_layers ( 1
			world_sky_layer (
				world_sky_layer_fadein ( 18:00:00 20:00:00 )
				world_sky_layer_top (
					world_sky_layer_top_nfaces ( 8 )
					world_sky_layer_top_radius ( 660 )
					world_sky_layer_top_height ( 1000 )
				)
				world_sky_layer_edge (
					world_sky_layer_edge_steps ( 2
						world_sky_layer_edge_step_height ( 500 )
						world_sky_layer_edge_step_radius ( 1400 )
						world_sky_layer_edge_step_height ( 240 )
						world_sky_layer_edge_step_radius ( 1650 )
					)
				)
				world_anim_shader (
					world_anim_shader_framelen ( 0.6 )
					world_anim_shader_frames ( 1
						world_anim_shader_frame (
							world_anim_shader_frame_uvtiles ( 2 2 )
							world_anim_shader_frame_uvscroll ( 0 0.01 )
						)
					)
					world_shader ( BlendATexDiff
						terrain_texslots ( 1
							terrain_texslot ( skylow.ace 1 0 )
						)
						terrain_uvcalcs ( 1
							terrain_uvcalc ( 1 0 0 0 )
						)
					)
				)
			)
		)
		world_sky_horizon (
			world_shader ( Diffuse )
		)
		world_sky_satellites ( 1
			world_sky_satellite (
				world_sky_satellite_rise_time ( 06:00:00 )
				world_sky_satellite_dir_rise_colour ( ffffee8b )
				world_sky_satellite_light ( 1 )
				world_sky_satellite_fog ( 96 )
			)
		)
	)
	world_water (
		world_water_terrain_patch_map ( 256 Wsib-W.raw )
		world_water_wave_height ( 0 )
		world_water_layers ( 1
			world_water_layer (
				world_water_layer_height ( -0.5 )
				world_anim_shader (
					world_shader ( TexDiff
						terrain_texslots ( 1
							terrain_texslot ( watermid.ace 1 0 )
						)
					)
				)
			)
		)
	)
	world_wind (
		_world_wind_layers ( 2 world_wind_layer ( world_wind_layer_speed ( 9 ) ) )
		world_wind_layers ( 1
			world_wind_layer (
				world_wind_layer_maxheight ( 100000 )
				world_wind_layer_direction ( 1 0 0 )
				world_wind_layer_speed ( 3 )
				world_wind_layer_turbulencep ( 0.2 )
			)
			world_wind_layer (
				world_wind_layer_direction ( 0 -1 0 )
			)
		)
	)
	world_precipitation (
		world_precipitation_type ( world_precipitation_type_rain )
		world_precipitation_density ( 2048 )
		world_precipitation_speed ( 40 )
	)
	tsre_other_tool ( 1 "two words" )
)
)";

const char *const WeatherSample = R"(SIMISA@@@@@@@@@@JINX0w0t______
world ( world_fog_distance ( 2000 ) )
tsre_weather (
	tsre_weather_version ( 1 )
	tsre_weather_preset ( scattered )
	tsre_weather_seed ( 77 )
	tsre_weather_source ( "Open-Meteo ERA5" "Contains modified Copernicus Climate Change Service information 2026" )
	tsre_weather_start ( 2024-06-21T04:00:00Z )
	tsre_weather_grid ( -6200 14950 4096 20 15 )
	tsre_weather_keyframe (
		tsre_weather_time ( 0 )
		tsre_weather_layer ( low stratocumulus 0.6 700 1500 0.9 )
	)
	tsre_weather_keyframe (
		tsre_weather_time ( 6 )
		tsre_weather_wind ( low 200 12 )
		tsre_weather_precipitation ( rain 1.5 )
		tsre_weather_future_token ( 5 )
	)
)
)";

struct Checker {
    int passed = 0;
    int failed = 0;
    bool verbose = false;
    void check(bool ok, const QString &name) {
        if (ok) {
            ++passed;
            if (verbose) qInfo().noquote() << "[tests:weather] PASS" << name;
        } else {
            ++failed;
            qWarning().noquote() << "[tests:weather] FAIL" << name;
        }
    }
};

bool near(double a, double b, double tolerance) {
    return std::abs(a - b) <= tolerance;
}

// Read, write, read again: the model gives the same token tree.
bool roundTrip(const Env::File &file, QString *difference = nullptr) {
    const QVector<EnvNode> first = file.toNodes();
    Env::File again;
    QString error;
    if (!again.parse(EnvText::write(first), error)) {
        if (difference) *difference = error;
        return false;
    }
    const QVector<EnvNode> second = again.toNodes();
    if (first != second && difference)
        *difference = "the token trees differ";
    return first == second;
}

// Every value of a token tree with the names of its blocks, numbers in one
// form and text in lower case: two trees with the same signature hold the
// same values, in whatever order and number format.
void signature(const QVector<EnvNode> &items, const QString &path, QStringList &out) {
    for (const EnvNode &item : items) {
        if (item.block) {
            signature(item.items, path + "/" + item.text.toLower(), out);
            continue;
        }
        bool number = false;
        const double value = item.text.toDouble(&number);
        out << path + "=" + (number ? EnvNode::floatText(float(value)) : item.text.toLower());
    }
}

QStringList signature(const QVector<EnvNode> &items) {
    QStringList out;
    signature(items, QString(), out);
    out.sort();
    return out;
}

// A weather with one uniform keyframe and no variation.
Weather uniform(float coverage, float type, float windFrom, float windSpeed) {
    Weather w;
    WeatherKeyframe k;
    k.variationAmount = 0.0f;
    for (auto &wind : k.wind)
        wind = {windFrom, windSpeed};
    k.layers[WeatherLayer::Low] = {type, coverage, 1000.0f, 2000.0f, 1.0f};
    w.keyframes << k;
    return w;
}

} // namespace

int TsreTests::runWeatherSuite(const QString &casesDirectory, bool verbose) {
    Checker c;
    c.verbose = verbose;
    QString error;

    // Malformed text is rejected; the sample's values are read.
    Env::File stray;
    c.check(stray.parse("world ( ) )", error) && stray.world && stray.warnings.size() == 1
            && stray.warnings[0].contains("unmatched"), "stray-close-skipped-with-warning");
    c.check(!Env::File().parse("world ( world_fog_distance ( 1 )", error) && error.contains("ends inside"),
            "truncated-rejected");
    Env::File clean;
    const bool parsed = clean.parse(QString::fromUtf8(Sample), error);
    c.check(parsed, "sample-parses " + error);
    if (parsed && clean.world) {
        const Env::World &w = *clean.world;
        c.check(w.fogDistance && *w.fogDistance == 450.0f && w.fogDayColour && *w.fogDayColour == 0xffffffffu,
                "sample-fog");
        c.check(w.sky && w.sky->layerCount == 1 && w.sky->layers.size() == 1
                && w.sky->layers[0].edgeSteps.size() == 2 && w.sky->layers[0].edgeSteps[1][1] == 1650.0f
                && w.sky->layers[0].shader && w.sky->layers[0].shader->frameLength
                && near(*w.sky->layers[0].shader->frameLength, 0.6, 1e-6)
                && w.sky->layers[0].shader->shader && w.sky->layers[0].shader->shader->name == "BlendATexDiff"
                && w.sky->layers[0].shader->shader->texSlots.value(0).file == "skylow.ace",
                "sample-sky-layer");
        c.check(w.sky && w.sky->satellites.size() == 1 && w.sky->satellites[0].colours[0]
                && *w.sky->satellites[0].colours[0] == 0xffffee8bu && w.sky->satellites[0].riseTime == QString("06:00:00"),
                "sample-satellite");
        c.check(w.water && w.water->patchMapFile == QString("Wsib-W.raw") && w.water->layers.size() == 1
                && w.water->layers[0].height == -0.5f, "sample-water");
        // Declared count kept although two layers follow.
        c.check(w.wind && w.wind->layerCount == 1 && w.wind->layers.size() == 2
                && w.wind->layers[0].turbulence && *w.wind->layers[0].turbulence == 0.2f, "sample-wind-declared-count");
        c.check(w.wind && w.wind->unknown.size() == 1 && w.wind->unknown[0].text == "_world_wind_layers",
                "sample-commented-block-kept");
        c.check(w.unknown.size() == 1 && w.unknown[0].text == "tsre_other_tool", "sample-unknown-block-kept");
    }

    QString difference;
    c.check(roundTrip(clean, &difference), "sample-round-trip " + difference);
    const QString written = EnvText::write(clean.toNodes());
    c.check(written.contains("tsre_other_tool ( 1 \"two words\" )") && written.contains("_world_wind_layers"),
            "sample-unknown-written");

    // Numbers as MSTS writes them: plain decimals, the fewest that read back.
    c.check(EnvNode::floatText(230.0f) == "230" && EnvNode::floatText(0.25f) == "0.25"
            && EnvNode::floatText(-0.008f) == "-0.008" && EnvNode::floatText(40000.0f) == "40000"
            && EnvNode::floatText(0.6f) == "0.6" && EnvNode::floatText(-0.0f) == "0"
            && EnvNode::floatText(1e-6f).toFloat() == 1e-6f, "number-text " + EnvNode::floatText(230.0f));

    // Saving: UTF-16 LE with a byte order mark and CRLF lines.
    QTemporaryDir temp;
    const QString envPath = temp.path() + "/test.env";
    c.check(clean.save(envPath, error), "save " + error);
    QFile saved(envPath);
    QByteArray bytes;
    if (saved.open(QIODevice::ReadOnly))
        bytes = saved.readAll();
    c.check(bytes.startsWith(QByteArray("\xff\xfeS\0I\0M\0I\0S\0A\0", 14)) && bytes.contains(QByteArray("\r\0\n\0", 4)),
            "save-utf16-crlf");
    Env::File reread;
    c.check(reread.read(envPath, error) && reread.toNodes() == clean.toNodes(), "save-read-back " + error);

    // The weather block: preset values, overrides, unknown tokens.
    Env::File weatherFile;
    c.check(weatherFile.parse(QString::fromUtf8(WeatherSample), error) && weatherFile.weather, "weather-parses " + error);
    if (weatherFile.weather) {
        const Weather &w = *weatherFile.weather;
        c.check(w.preset == "scattered" && w.seed == 77 && w.keyframes.size() == 2, "weather-header");
        c.check(w.start.isValid() && w.start.toUTC().toString(Qt::ISODate) == "2024-06-21T04:00:00Z" && !w.relative(),
                "weather-start");
        c.check(w.grid.valid && w.grid.cellSize == 4096 && w.grid.columns == 20, "weather-grid");
        // Keyframe 0: the preset's values with the low layer overridden.
        const WeatherKeyframe preset = WeatherPresets::at("scattered", 0);
        c.check(near(w.keyframes[0].layers[WeatherLayer::Low].coverage, 0.6, 1e-6)
                && near(w.keyframes[0].layers[WeatherLayer::Low].type, 0.33, 1e-6)
                && near(w.keyframes[0].layers[WeatherLayer::Mid].coverage, preset.layers[WeatherLayer::Mid].coverage, 1e-6),
                "weather-preset-override");
        c.check(w.keyframes[1].precipitation == WeatherPrecipitation::Rain && near(w.keyframes[1].precipitationRate, 1.5, 1e-6)
                && near(w.keyframes[1].wind[WeatherLayer::Low].speed, 12, 1e-6), "weather-keyframe-values");
        c.check(w.keyframes[1].unknown.size() == 1 && w.keyframes[1].unknown[0].text == "tsre_weather_future_token",
                "weather-unknown-kept");
        c.check(w.attribution.startsWith("Contains modified Copernicus"), "weather-attribution");
        c.check(roundTrip(weatherFile, &difference), "weather-round-trip " + difference);
    }
    Env::File badGrid;
    badGrid.parse("tsre_weather ( tsre_weather_grid ( 0 0 3000 4 4 ) )", error);
    c.check(badGrid.weather && !badGrid.weather->grid.valid && !badGrid.warnings.isEmpty(), "weather-grid-power-of-two");

    // A weather derived from a plain env file.
    const Weather derived = clean.weatherOrDerived();
    c.check(derived.preset == "rain" && near(derived.keyframes[0].visibility, 450, 1e-3), "derived-rain-and-fog");
    // Wind towards +x (east) comes from the west: 270 degrees.
    c.check(near(derived.keyframes[0].wind[WeatherLayer::Low].direction, 270, 1e-3)
            && near(derived.keyframes[0].wind[WeatherLayer::Low].speed, 3, 1e-6), "derived-wind");
    Env::File plain;
    plain.parse("world ( world_fog_distance ( 3000 ) )", error);
    c.check(plain.weatherOrDerived(Env::SlotWeather::Snow).preset == "snow"
            && plain.weatherOrDerived().preset == "fair_cumulus", "derived-slot");

    // Presets.
    bool presetsOk = true;
    for (const QString &name : WeatherPresets::names()) {
        const auto frames = WeatherPresets::keyframes(name);
        presetsOk = presetsOk && !frames.isEmpty();
        for (int i = 1; i < frames.size(); ++i)
            presetsOk = presetsOk && frames[i].time > frames[i - 1].time;
    }
    c.check(presetsOk && WeatherPresets::keyframes("front").size() > 3, "presets");

    // Keyframe blend, clamping and looping.
    Weather timed = WeatherPresets::weather("front");
    const double length = timed.duration();
    Weather::Blend b = timed.blend(1.5);
    c.check(b.a == 0 && b.b == 1 && near(b.weight, 0.5, 1e-9), "blend-between");
    b = timed.blend(length + 1.5);
    c.check(b.a == 0 && b.b == 1 && near(b.weight, 0.5, 1e-9), "blend-relative-loops");
    timed.start = QDateTime(QDate(2024, 1, 1), QTime(0, 0), Qt::UTC);
    b = timed.blend(length + 1.5);
    c.check(b.a == timed.keyframes.size() - 1 && b.b == b.a, "blend-absolute-holds-last");

    // Drift: 10 m/s from the west moves 36 km east in an hour.
    const Weather steady = uniform(0.5f, 0.67f, 270.0f, 10.0f);
    double east, north;
    steady.drift(1.0, WeatherLayer::Low, east, north);
    c.check(near(east, 36000.0, 1.0) && near(north, 0.0, 1.0), "drift-steady");

    // Advection: a cloudy cell moves with the wind.
    // 16 cells of 2048 m an hour, so maps shift by whole cells.
    const float cellWind = float(2048.0 * 16.0 / 3600.0);
    Weather moving = uniform(0.0f, 0.67f, 270.0f, cellWind);
    moving.grid = {true, 0, 0, 2048, 40, 4};
    moving.maps = "maps";
    moving.allocateMaps();
    auto setCoverage = [&moving](int keyframe, int column, float value) {
        QImage &image = moving.mapImage(WeatherLayer::Low);
        for (int r = 0; r < moving.grid.rows; ++r) {
            auto *line = reinterpret_cast<QRgba64 *>(image.scanLine(keyframe * moving.grid.rows + r));
            line[column].setRed(quint16(value * 65535.0f));
        }
    };
    setCoverage(0, 5, 1.0f);
    const double cellEast = moving.grid.west() + 5.5 * 2048.0;
    const double centre = moving.grid.north() - 2.0 * 2048.0;
    c.check(near(moving.layer(0.0, cellEast, centre, WeatherLayer::Low).coverage, 1.0, 1e-3),
            "advection-at-start");
    const double moved = cellWind * 3600.0 * 0.5;
    c.check(near(moving.layer(0.5, cellEast + moved, centre, WeatherLayer::Low).coverage, 1.0, 1e-3)
            && moving.layer(0.5, cellEast, centre, WeatherLayer::Low).coverage < 0.01,
            "advection-one-keyframe-moves");
    // Two keyframes whose cell moved with the wind: halfway, the cell is
    // halfway and whole, not two half-faded cells.
    WeatherKeyframe second = moving.keyframes[0];
    second.time = 1.0f;
    moving.keyframes << second;
    moving.allocateMaps();
    setCoverage(0, 5, 1.0f);
    const int later = 5 + 16;
    setCoverage(1, later, 1.0f);
    const double halfway = cellEast + 8 * 2048.0;
    c.check(moving.layer(0.5, halfway, centre, WeatherLayer::Low).coverage > 0.85, "advection-keyframes-move-not-fade");
    // Maps written and read back.
    const QString mapsDir = temp.path();
    c.check(moving.saveMaps(mapsDir, error), "maps-save " + error);
    Weather loaded = moving;
    c.check(loaded.loadMaps(mapsDir, error)
            && loaded.mapImage(WeatherLayer::Low).pixelColor(later, moving.grid.rows) == moving.mapImage(WeatherLayer::Low).pixelColor(later, moving.grid.rows)
            && near(loaded.layer(0.0, cellEast, centre, WeatherLayer::Low).coverage, 1.0, 1e-3),
            "maps-load " + error);

    // The generated detail covers the coverage's share of the area.
    for (float coverage : {0.2f, 0.5f, 0.8f}) {
        const Weather w = uniform(coverage, 0.67f, 270.0f, 0.0f);
        double sum = 0.0;
        int n = 0;
        for (int i = 0; i < 160; ++i)
            for (int j = 0; j < 160; ++j, ++n)
                sum += WeatherDetail::cover(w, 0.0, i * 397.0, j * 397.0, WeatherLayer::Low);
        c.check(near(sum / n, coverage, 0.04), QString("detail-share-%1 (%2)").arg(coverage).arg(sum / n));
    }

    // Every env file in the cases directory round-trips.
    if (!casesDirectory.isEmpty()) {
        int files = 0, failedFiles = 0, warned = 0;
        QDirIterator it(casesDirectory, {"*.env", "*.ENV"}, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString path = it.next();
            ++files;
            Env::File env;
            QString fileError;
            QVector<EnvNode> original;
            if (!env.read(path, fileError) || !roundTrip(env, &fileError)) {
                ++failedFiles;
                qWarning().noquote() << "[tests:weather] env file" << path << fileError;
            } else if (EnvText::read(path, original, fileError)
                       && signature(original) != signature(env.toNodes())) {
                // A value lost, added or moved to another block.
                ++failedFiles;
                const QStringList a = signature(original), b = signature(env.toNodes());
                QStringList missing, added;
                for (const QString &v : a) if (!b.contains(v)) missing << v;
                for (const QString &v : b) if (!a.contains(v)) added << v;
                qWarning().noquote() << "[tests:weather] env file" << path << "values differ; missing:"
                                     << missing.mid(0, 5) << "added:" << added.mid(0, 5);
            }
            if (!env.warnings.isEmpty())
                ++warned;
        }
        qInfo().noquote() << QString("[tests:weather] env files: %1, failed %2, with warnings %3")
                             .arg(files).arg(failedFiles).arg(warned);
        c.check(files > 0 && failedFiles == 0, "env-files-round-trip");
    }

    qInfo() << "[tests:weather] cases=" << (c.passed + c.failed) << "passed=" << c.passed << "failed=" << c.failed;
    return c.failed == 0 ? 0 : 1;
}
