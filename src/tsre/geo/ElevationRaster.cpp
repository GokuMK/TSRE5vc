#include <tsre/geo/ElevationRaster.h>

#include <QMap>
#include <QtEndian>
#include <QXmlStreamReader>
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstring>

namespace Elevation {
namespace {
constexpr qsizetype MaxPixels = 16 * 1024 * 1024;
constexpr qsizetype MaxBytes = 128 * 1024 * 1024;
bool dimensions(int w, int h) {
    return w > 0 && h > 0 && qint64(w) * h <= MaxPixels;
}
bool fail(QString &error, const char *message) {
    error = QString::fromLatin1(message);
    return false;
}
Sample blend(const Raster &r, const int indices[4], const double weights[4], bool zero) {
    double height = 0;
    for (int i = 0; i < 4; ++i) {
        if (weights[i] <= 0) continue;
        const float value = r.values[indices[i]];
        if (!std::isfinite(value) || (r.hasNoData && value == r.noData)
                || (zero && value == 0))
            return {0, SampleStatus::NoData};
        height += value * weights[i];
    }
    return {float(height), SampleStatus::Valid};
}

struct TiffReader {
    const QByteArray &bytes;
    bool little = true;
    bool range(quint64 offset, quint64 length) const {
        return offset <= quint64(bytes.size()) && length <= quint64(bytes.size()) - offset;
    }
    quint16 u16(quint64 p) const {
        return little ? qFromLittleEndian<quint16>(bytes.constData() + p)
                      : qFromBigEndian<quint16>(bytes.constData() + p);
    }
    quint32 u32(quint64 p) const {
        return little ? qFromLittleEndian<quint32>(bytes.constData() + p)
                      : qFromBigEndian<quint32>(bytes.constData() + p);
    }
    double f64(quint64 p) const {
        quint64 bits = little ? qFromLittleEndian<quint64>(bytes.constData() + p)
                             : qFromBigEndian<quint64>(bytes.constData() + p);
        double value;
        std::memcpy(&value, &bits, sizeof(value));
        return value;
    }
};
struct Tag { quint16 type = 0; quint32 count = 0; quint64 offset = 0; };

// Whitespace tokenizer avoids making a QString allocation for every height.
struct Tokens {
    QByteArray bytes;
    qsizetype cursor = 0;
    QByteArrayView next() {
        while (cursor < bytes.size() && uchar(bytes[cursor]) <= 32) ++cursor;
        const auto first = cursor;
        while (cursor < bytes.size() && uchar(bytes[cursor]) > 32) ++cursor;
        return QByteArrayView(bytes.constData() + first, cursor - first);
    }
};
bool number(QByteArrayView token, double &value) {
    if (token.empty()) return false;
    const char *first = token.data(), *last = first + token.size();
    if (*first == '+') ++first;
    const auto parsed = std::from_chars(first, last, value);
    return parsed.ec == std::errc() && parsed.ptr == last;
}
int tmZoneForCrs(int epsg) {
    // ETRS89 / UTM zones 28N..38N.
    if (epsg >= 25828 && epsg <= 25838)
        return epsg - 25800;

    // ETRS89 / UTM zone 33N with northing/easting axis declaration.
    if (epsg == 3045)
        return 33;

    // EUREF-FIN / TM35FIN uses the same TM parameters as UTM zone 35N.
    if (epsg == 3067)
        return 35;

    return 0;
}
}

bool supportedCrs(int epsg) {
    return epsg == 2180 || epsg == 3794
        || epsg == 4326 || epsg == 3857
        || tmZoneForCrs(epsg) != 0;
}

bool project(Point p, int epsg, XY &out) {
    if (!std::isfinite(p.latitude) || !std::isfinite(p.longitude)
            || p.latitude < -90 || p.latitude > 90
            || p.longitude < -180 || p.longitude > 180) 
            return false;
    if (epsg == 4326) { 
        out = {p.longitude, p.latitude}; 
        return true; 
    }
    if (epsg == 3857) {
        constexpr double pi = 3.14159265358979323846;
        constexpr double radius = 6378137.0;
        constexpr double maxLatitude = 85.0511287798066;

        if (p.latitude < -maxLatitude || p.latitude > maxLatitude)
            return false;

        const double lon = p.longitude * pi / 180.0;
        const double lat = p.latitude * pi / 180.0;

        out.x = radius * lon;
        out.y = radius * std::log(std::tan(pi / 4.0 + lat / 2.0));

        return std::isfinite(out.x) && std::isfinite(out.y);
    }
    const bool cs92 = epsg == 2180;
    const bool sloveniaD96 = epsg == 3794;
    const int tmZone = tmZoneForCrs(epsg);

    double meridian;
    double factor;
    double falseNorth;

    if (cs92) {
        if (p.latitude < 48 || p.latitude > 57
                || p.longitude < 13 || p.longitude > 25)
            return false;

        meridian = 19;
        factor = .9993;
        falseNorth = -5300000;
    } else if (sloveniaD96) {
        if (p.latitude < 45 || p.latitude > 47.5
                || p.longitude < 13 || p.longitude > 17)
            return false;

        meridian = 15;
        factor = .9999;
        falseNorth = -5000000;
    } else {
        if (!tmZone || p.latitude < 0 || p.latitude > 84)
            return false;

        meridian = tmZone * 6.0 - 183.0;

        // Safety bound for this lightweight TM implementation.
        // Wider than a nominal UTM zone because national datasets may use
        // one projected CRS beyond the normal 6-degree zone.
        if (std::abs(p.longitude - meridian) > 30.0)
            return false;

        factor = .9996;
        falseNorth = 0;
    }
    // Fourth-order Krueger series, with analytic conformal latitude.
    // Equations: PROJ Transverse Mercator documentation, mathematical definition.
    // This bounded forward projection is not a general CRS/datum engine.
    constexpr double pi = 3.14159265358979323846;
    constexpr double a = 6378137.0, f = 1.0 / 298.257222101;
    constexpr double n = f / (2 - f), n2 = n*n, n3 = n2*n, n4 = n2*n2;
    constexpr double A = a / (1+n) * (1 + n2/4 + n4/64);
    const double alpha[] = {
        n/2 - 2*n2/3 + 5*n3/16 + 41*n4/180,
        13*n2/48 - 3*n3/5 + 557*n4/1440,
        61*n3/240 - 103*n4/140,
        49561*n4/161280
    };
    const double phi = p.latitude*pi/180, lambda = (p.longitude-meridian)*pi/180;
    const double e = std::sqrt(f*(2-f));
    const double t = std::sinh(std::asinh(std::tan(phi)) - e*std::atanh(e*std::sin(phi)));
    const double xi = std::atan2(t, std::cos(lambda));
    const double eta = std::asinh(std::sin(lambda)/std::hypot(t, std::cos(lambda)));
    double north = xi, east = eta;
    for (int j = 1; j <= 4; ++j) {
        north += alpha[j-1]*std::sin(2*j*xi)*std::cosh(2*j*eta);
        east += alpha[j-1]*std::cos(2*j*xi)*std::sinh(2*j*eta);
    }
    out = {500000 + factor*A*east, falseNorth + factor*A*north};
    return std::isfinite(out.x) && std::isfinite(out.y);
}

Sample Raster::sample(XY p, bool zero) const {
    if (width <= 0 || height <= 0 || values.size() > 32*1024*1024
            || values.size() != qint64(width)*height)
        return {0, SampleStatus::Unavailable};
    const auto &t = transform;
    const double det = t[1]*t[5] - t[2]*t[4];
    if (!std::isfinite(det) || det == 0) return {0, SampleStatus::Unavailable};
    double col = ((p.x-t[0])*t[5] - (p.y-t[3])*t[2])/det - .5;
    double row = ((p.y-t[3])*t[1] - (p.x-t[0])*t[4])/det - .5;
    if (!std::isfinite(col) || !std::isfinite(row) || col < -1e-7 || row < -1e-7
            || col > width-1+1e-7 || row > height-1+1e-7)
        return {0, SampleStatus::Outside};
    col = std::clamp(col, 0.0, double(width-1));
    row = std::clamp(row, 0.0, double(height-1));
    const int x = int(col), y = int(row), x1 = std::min(x+1,width-1), y1 = std::min(y+1,height-1);
    const double dx = col-x, dy = row-y;
    const int ids[] = {y*width+x, y*width+x1, y1*width+x, y1*width+x1};
    const double weights[] = {(1-dx)*(1-dy), dx*(1-dy), (1-dx)*dy, dx*dy};
    return blend(*this, ids, weights, zero);
}

int fillNoData(Raster &r, bool zero, std::atomic_bool &cancel, const QBitArray &available) {
    const qsizetype count = r.values.size();
    if (r.width <= 0 || r.height <= 0 || count != qint64(r.width)*r.height
            || count > 32*1024*1024 || (!available.isEmpty() && available.size() != count))
        return 0;
    // 0 = hole, 1 = valid, 2 = queued for this layer, 3 = unavailable.
    QByteArray state(count,0);
    for (int i=0; i<count; ++i) {
        if (i%65536 == 0 && cancel) return 0;
        const float h = r.values[i];
        if (!available.isEmpty() && !available.testBit(i)) state[i] = 3;
        else if (std::isfinite(h) && !(r.hasNoData && h == r.noData) && !(zero && h == 0))
            state[i] = 1;
        if (state[i] != 1) r.values[i] = std::numeric_limits<float>::quiet_NaN();
    }
    const auto neighbours = [&](int i, const auto &visit) {
        if (i%r.width) visit(i-1);
        if (i%r.width != r.width-1) visit(i+1);
        if (i >= r.width) visit(i-r.width);
        if (i < count-r.width) visit(i+r.width);
    };
    QVector<int> frontier, next;
    for (int i=0; i<count; ++i) {
        if (i%65536 == 0 && cancel) return 0;
        if (state[i] != 0) continue;
        bool adjacent = false;
        neighbours(i,[&](int j) { adjacent |= state[j] == 1; });
        if (adjacent) { state[i] = 2; frontier.push_back(i); }
    }
    int filled = 0;
    while (!frontier.isEmpty()) {
        if (cancel) return filled;
        // Read only previous layers: results do not depend on scan order.
        for (int i : frontier) {
            if (cancel) return filled;
            double sum = 0; int n = 0;
            neighbours(i,[&](int j) { if (state[j] == 1) { sum += r.values[j]; ++n; } });
            r.values[i] = float(sum/n);
        }
        for (int i : frontier) state[i] = 1;
        filled += int(frontier.size());
        next.clear();
        for (int i : frontier) neighbours(i,[&](int j) {
            if (state[j] == 0) { state[j] = 2; next.push_back(j); }
        });
        frontier.swap(next);
    }
    // Unfilled cells remain NaN; a filled zero is now a valid estimate.
    r.hasNoData = false;
    return filled;
}

static bool readTiff(const QByteArray &bytes, int metadataEpsg, Raster &output, QString &error) {
    error.clear();
    if (bytes.size() < 8 || bytes.size() > MaxBytes)
        return fail(error, "Invalid TIFF size");
    TiffReader rd{bytes};
    if (bytes.startsWith("II")) rd.little = true;
    else if (bytes.startsWith("MM")) rd.little = false;
    else return fail(error, "Response is not a TIFF raster");
    if (rd.u16(2) != 42) return fail(error, "Only classic TIFF is supported");
    const quint64 ifd = rd.u32(4);
    if (!rd.range(ifd, 2)) return fail(error, "Invalid TIFF directory");
    const quint32 count = rd.u16(ifd);
    if (count > 256 || !rd.range(ifd+2, quint64(count)*12+4))
        return fail(error, "Invalid TIFF directory size");
    QMap<int, Tag> tags;
    for (quint32 i = 0; i < count; ++i) {
        const quint64 p = ifd+2+i*12;
        Tag tag{rd.u16(p+2), rd.u32(p+4), p+8};
        const int sizes[] = {0,1,1,2,4,8,1,1,2,4,8,4,8};
        if (tag.type > 12 || !sizes[tag.type]) return fail(error, "Unsupported TIFF field type");
        const quint64 size = quint64(tag.count)*sizes[tag.type];
        if (size > 4) tag.offset = rd.u32(p+8);
        if (!rd.range(tag.offset, size) || tags.contains(rd.u16(p)))
            return fail(error, "Invalid or duplicate TIFF field");
        tags.insert(rd.u16(p), tag);
    }
    const auto integer = [&](int id, quint32 fallback, quint32 index = 0) -> quint32 {
        const Tag t = tags.value(id);
        if (index >= t.count) return fallback;
        if (t.type == 3) return rd.u16(t.offset + index*2);
        if (t.type == 4) return rd.u32(t.offset + index*4);
        return fallback;
    };
    Raster r;
    r.width = int(integer(256, 0)); r.height = int(integer(257, 0));
    const int bits = int(integer(258, 0)), sampleFormat = int(integer(339, 1));

    if (!dimensions(r.width, r.height))
        return fail(error, "Invalid TIFF dimensions");

    const bool float32  = bits == 32 && sampleFormat == 3;
    const bool signed16 = bits == 16 && sampleFormat == 2;
    const bool unsigned16 = bits == 16 && sampleFormat == 1;

    if (integer(277,1) != 1 || integer(262,1) != 1
            || !(float32 || signed16 || unsigned16))
        return fail(error,
            "TIFF must contain one float32, int16 or uint16 height band; RGB is not elevation");
    if (integer(259,1) != 1 || integer(317,1) != 1 || integer(274,1) != 1
            || integer(284,1) != 1)
        return fail(error, "Only uncompressed, top-down TIFF is currently supported");
    if (rd.u32(ifd+2+count*12) != 0)
        return fail(error, "Multiple TIFF directories are not supported");
    const Tag keys = tags.value(34735);
    if (tags.contains(34735) && (keys.type != 3 || keys.count < 4 || integer(34735,0,0) != 1))
        return fail(error, "Invalid GeoTIFF coordinate system");
    if (!tags.contains(34735) && !metadataEpsg)
        return fail(error, "Missing GeoTIFF coordinate system");
    const quint32 keyCount = integer(34735,0,3);
    if (keys.count && keyCount > (keys.count-4)/4) return fail(error, "Invalid GeoTIFF key directory");
    bool point = false;
    int linearUnits = 9001, angularUnits = 9102;
    for (quint32 k = 0; k < keyCount; ++k) {
        const quint32 key = integer(34735,0,4+4*k);
        if (integer(34735,1,5+4*k) != 0 || integer(34735,0,6+4*k) != 1) continue;
        const int v = int(integer(34735,0,7+4*k));
        if (key == 1025) {
            if (v != 1 && v != 2) return fail(error, "Unsupported GeoTIFF raster registration");
            point = v == 2;
        }
        if (key == 3072) r.epsg = v;
        if (key == 2048 && r.epsg == 0) r.epsg = v;
        if (key == 3076) linearUnits = v;
        if (key == 2054) angularUnits = v;
    }
    if (!tags.contains(34735)) r.epsg = metadataEpsg;
    if (!supportedCrs(r.epsg)) return fail(error, "Unsupported raster CRS");
    if (metadataEpsg && metadataEpsg != r.epsg) return fail(error, "TIFF and WCS metadata CRS disagree");
    if ((r.epsg != 4326 && linearUnits != 9001) || (r.epsg == 4326 && angularUnits != 9102))
        return fail(error, "Raster coordinate units conflict with its CRS");
    const Tag matrix = tags.value(34264), scale = tags.value(33550), tie = tags.value(33922);
    if (matrix.type == 12 && matrix.count == 16) {
        if (rd.f64(matrix.offset+96) != 0 || rd.f64(matrix.offset+104) != 0
                || rd.f64(matrix.offset+112) != 0 || rd.f64(matrix.offset+120) != 1)
            return fail(error, "Only affine GeoTIFF matrices are supported");
        r.transform = {{rd.f64(matrix.offset+24), rd.f64(matrix.offset), rd.f64(matrix.offset+8),
                        rd.f64(matrix.offset+56), rd.f64(matrix.offset+32), rd.f64(matrix.offset+40)}};
    } else if (scale.type == 12 && scale.count == 3 && tie.type == 12 && tie.count == 6) {
        const double sx = rd.f64(scale.offset), sy = rd.f64(scale.offset+8);
        if (sx <= 0 || sy <= 0) return fail(error, "Invalid GeoTIFF scale");
        r.transform = {{rd.f64(tie.offset+24)-rd.f64(tie.offset)*sx, sx, 0,
                        rd.f64(tie.offset+32)+rd.f64(tie.offset+8)*sy, 0, -sy}};
    } else return fail(error, "Missing or unsupported GeoTIFF grid transform");
    for (double v : r.transform) if (!std::isfinite(v)) return fail(error, "Non-finite GeoTIFF transform");
    if (r.transform[1]*r.transform[5] == r.transform[2]*r.transform[4])
        return fail(error, "Singular GeoTIFF transform");
    if (point) {
        r.transform[0] -= .5*(r.transform[1]+r.transform[2]);
        r.transform[3] -= .5*(r.transform[4]+r.transform[5]);
    }
    if (tags.contains(42113)) {
        const Tag nd = tags.value(42113);
        if (nd.type != 2 || nd.count == 0 || nd.count > 128)
            return fail(error, "Invalid TIFF NoData field");
        bool ok = false;
        r.noData = bytes.mid(nd.offset, nd.count).replace('\0', ' ').trimmed().toFloat(&ok);
        if (!ok) return fail(error, "Invalid TIFF NoData value");
        r.hasNoData = true;
    }
    const auto valueAt = [&](quint64 offset) {
        float value;

        if (bits == 32) {
            const quint32 raw = rd.u32(offset);
            std::memcpy(&value, &raw, 4);
        } else if (sampleFormat == 2) {
            value = qint16(rd.u16(offset));
        } else {
            value = rd.u16(offset);
        }

        return value;
    };
    r.values.fill(std::numeric_limits<float>::quiet_NaN(),qsizetype(r.width)*r.height);
    if (tags.contains(322) || tags.contains(323) || tags.contains(324) || tags.contains(325)) {
        const quint32 tw = integer(322,0), th = integer(323,0);
        if (!tw || !th || tw > MaxPixels || th > MaxPixels || quint64(tw)*th > MaxPixels
                || tags.contains(273) || tags.contains(279))
            return fail(error, "Invalid TIFF tile dimensions or mixed storage");
        const quint32 columns = (r.width+tw-1)/tw, rows = (r.height+th-1)/th;
        const quint64 tiles = quint64(columns)*rows, size = quint64(tw)*th*(bits/8);
        if (tags.value(324).count != tiles || tags.value(325).count != tiles
                || tags.value(324).type != 4 || tags.value(325).type != 4)
            return fail(error, "Invalid TIFF tile table");
        for (quint32 tile = 0; tile < tiles; ++tile) {
            const quint64 offset = integer(324,0,tile), length = integer(325,0,tile);
            // Sparse service tiles contain no samples. Never treat absent data as sea level.
            if (offset == 0 && length == 0) continue;
            if (offset < 8 || length != size || !rd.range(offset,size))
                return fail(error, "Truncated or inconsistent TIFF tile");
            const quint32 x = (tile%columns)*tw, y = (tile/columns)*th;
            for (quint32 row = 0; row < std::min(th,quint32(r.height)-y); ++row)
                for (quint32 col = 0; col < std::min(tw,quint32(r.width)-x); ++col)
                    r.values[(y+row)*r.width+x+col] = valueAt(offset+(quint64(row)*tw+col)*(bits/8));
        }
    } else {
        const quint32 rows = integer(278, quint32(r.height));
        if (!rows) return fail(error, "Invalid TIFF rows per strip");
        const quint32 strips = quint32((quint64(r.height)+rows-1)/rows);
        if (tags.value(273).count != strips || tags.value(279).count != strips)
            return fail(error, "Invalid TIFF strip table");
        qsizetype destination = 0;
        for (quint32 s = 0; s < strips; ++s) {
            const quint64 samples = quint64(std::min(rows, quint32(r.height)-s*rows))*r.width;
            const quint64 offset = integer(273, quint32(bytes.size()), s);
            const quint64 size = samples*(bits/8);
            if (integer(279,0,s) != size || !rd.range(offset,size))
                return fail(error, "Truncated or inconsistent TIFF strip");
            for (quint64 i = 0; i < samples; ++i) {
                r.values[destination++] = valueAt(offset+i*(bits/8));
            }
        }
    }
    output = std::move(r);
    return true;
}

bool readGeoTiff(const QByteArray &bytes, Raster &output, QString &error) {
    return readTiff(bytes,0,output,error);
}

bool readWcsTiff(const QByteArray &bytes, int expectedEpsg, Raster &output, QString &error) {
    error.clear();
    Raster raster;
    if (!bytes.startsWith("--")) {
        if (!readTiff(bytes, expectedEpsg, raster, error)) return false;
    } else {
        if (bytes.size() > MaxBytes) return fail(error,"Oversized WCS multipart response");
        const qsizetype line = bytes.indexOf('\n');
        if (line < 3 || line > 200) return fail(error,"Invalid WCS multipart boundary");
        const QByteArray boundary = bytes.left(line).trimmed();
        const QByteArray newline = bytes[line-1] == '\r' ? QByteArray("\r\n") : QByteArray("\n");
        QByteArray tiff, xml, contentId;
        qsizetype cursor = line+1;
        bool closed = false;
        for (int part = 0; part < 8; ++part) {
            QMap<QByteArray,QByteArray> headers;
            const qsizetype headerStart = cursor;
            for (;;) {
                const qsizetype end = bytes.indexOf('\n',cursor);
                if (end < 0 || end-headerStart > 16384) return fail(error,"Invalid WCS part headers");
                const QByteArray header = bytes.mid(cursor,end-cursor).trimmed();
                cursor = end+1;
                if (header.isEmpty()) break;
                const qsizetype colon = header.indexOf(':');
                if (colon <= 0) return fail(error,"Invalid WCS part header");
                const auto name = header.left(colon).toLower();
                if (headers.contains(name)) return fail(error,"Duplicate WCS part header");
                headers.insert(name,header.mid(colon+1).trimmed());
            }
            const qsizetype end = bytes.indexOf(newline+boundary,cursor);
            if (end < 0) return fail(error,"Truncated WCS multipart body");
            const qsizetype bodyEnd = end;
            const QByteArray type = headers.value("content-type").split(';').first().trimmed().toLower();
            const QByteArray encoding = headers.value("content-transfer-encoding").toLower();
            if (!encoding.isEmpty() && encoding != "binary" && encoding != "8bit")
                return fail(error,"Unsupported WCS part encoding");
            if (type == "image/tiff") {
                if (!tiff.isEmpty()) return fail(error,"Multiple WCS TIFF parts");
                tiff = bytes.mid(cursor,bodyEnd-cursor);
                contentId = headers.value("content-id");
                if (contentId.startsWith('<') && contentId.endsWith('>')) contentId = contentId.mid(1,contentId.size()-2);
            } else if (type == "text/xml" || type == "application/xml" || type == "application/gml+xml") {
                if (!xml.isEmpty() || bodyEnd-cursor > 1024*1024) return fail(error,"Ambiguous or oversized WCS GML");
                xml = bytes.mid(cursor,bodyEnd-cursor);
            } else return fail(error,"Unsupported WCS multipart content");
            cursor = end+newline.size()+boundary.size();
            if (bytes.mid(cursor,2) == "--") {
                if (!bytes.mid(cursor+2).trimmed().isEmpty()) return fail(error,"Extra WCS multipart content");
                closed = true; break;
            }
            if (bytes.mid(cursor,2) == "\r\n") cursor += 2;
            else if (bytes.mid(cursor,1) == "\n") ++cursor;
            else return fail(error,"Invalid WCS multipart separator");
        }
        if (!closed || tiff.isEmpty() || xml.isEmpty()) return fail(error,"Incomplete WCS multipart response");
        QXmlStreamReader reader(xml);
        int metadataEpsg = 0, envelopes = 0;
        QString reference;
        while (!reader.atEnd()) {
            reader.readNext();
            if (reader.isDTD()) return fail(error,"DTD is not supported in WCS metadata");
            if (!reader.isStartElement() || reader.namespaceUri() != QLatin1String("http://www.opengis.net/gml/3.2")) continue;
            if (reader.name() == QLatin1String("Envelope")) {
                ++envelopes;
                const QString crs = reader.attributes().value("srsName").toString();
                const QString prefix = QStringLiteral("http://www.opengis.net/def/crs/EPSG/0/");
                if (crs.startsWith(prefix)) metadataEpsg = crs.mid(prefix.size()).toInt();
            } else if (reader.name() == QLatin1String("fileReference")) {
                if (!reference.isEmpty()) return fail(error,"Multiple WCS file references");
                reference = reader.readElementText();
            }
        }
        if (reader.hasError() || envelopes != 1 || !supportedCrs(metadataEpsg)
                || metadataEpsg != expectedEpsg || contentId.isEmpty()
                || reference != QStringLiteral("cid:")+QString::fromLatin1(contentId))
            return fail(error,"Invalid or conflicting WCS GML raster reference/CRS");
        if (!readTiff(tiff,metadataEpsg,raster,error)) return false;
    }
    if (raster.epsg != expectedEpsg) return fail(error,"WCS raster CRS differs from requested dataset");
    output = std::move(raster);
    return true;
}

bool readAsciiGrid(const QByteArray &bytes, int epsg, Raster &output, QString &error) {
    error.clear();
    if (bytes.isEmpty() || bytes.size() > MaxBytes) return fail(error, "Invalid ASCII Grid size");
    // Geoportal returns MIME parts (grid, auxiliary XML, projection), not a bare grid.
    QByteArray grid = bytes;
    if (bytes.trimmed().startsWith("--")) {
        const qsizetype first = bytes.indexOf("--"), end = bytes.indexOf('\n',first);
        if (end < 0 || end-first > 200) return fail(error, "Invalid multipart boundary");
        const QByteArray boundary = bytes.mid(first,end-first).trimmed();
        qsizetype start = bytes.indexOf("Content-Type: image/x-aaigrid");
        if (start < 0) return fail(error, "Multipart response has no ASCII height grid");
        qsizetype body = bytes.indexOf("\r\n\r\n",start), skip = 4;
        if (body < 0) { body = bytes.indexOf("\n\n",start); skip = 2; }
        const qsizetype finish = bytes.indexOf(boundary,body+skip);
        if (body < 0 || finish < 0) return fail(error, "Truncated multipart grid");
        grid = bytes.mid(body+skip,finish-body-skip);
    }
    Tokens tokens{grid};
    QMap<QByteArray,double> header;
    QByteArrayView token;
    for (;;) {
        token = tokens.next();
        const QByteArray key = QByteArray(token.data(),token.size()).toLower();
        if (key != "ncols" && key != "nrows" && key != "xllcorner" && key != "yllcorner"
                && key != "xllcenter" && key != "yllcenter" && key != "cellsize" && key != "nodata_value") break;
        double v;
        if (header.contains(key) || !number(tokens.next(),v) || !std::isfinite(v))
            return fail(error, "Invalid ASCII Grid header");
        header.insert(key,v);
    }
    Raster r;
    const double w = header.value("ncols"), h = header.value("nrows"), step = header.value("cellsize");
    if (w < 1 || h < 1 || w > MaxPixels || h > MaxPixels || w != std::floor(w) || h != std::floor(h)
            || !dimensions(int(w),int(h)) || step <= 0)
        return fail(error, "Invalid ASCII Grid dimensions or cell size");
    const bool xc = header.contains("xllcenter"), yc = header.contains("yllcenter");
    if (header.contains("xllcorner") == xc || header.contains("yllcorner") == yc)
        return fail(error, "Missing or ambiguous ASCII Grid origin");
    r.width = int(w); r.height = int(h); r.epsg = epsg;
    const double x = header.value(xc ? "xllcenter" : "xllcorner") - (xc ? step/2 : 0);
    const double y = header.value(yc ? "yllcenter" : "yllcorner") - (yc ? step/2 : 0);
    r.transform = {{x,step,0,y+h*step,0,-step}};
    r.hasNoData = header.contains("nodata_value");
    if (r.hasNoData) r.noData = float(header.value("nodata_value"));
    r.values.resize(qsizetype(r.width)*r.height);
    for (qsizetype i = 0; i < r.values.size(); ++i) {
        double value;
        if (!number(token,value) || (std::isfinite(value) && std::abs(value) > std::numeric_limits<float>::max()))
            return fail(error, "Invalid or truncated ASCII height data");
        r.values[i] = float(value);
        token = tokens.next();
    }
    if (!token.empty()) return fail(error, "Extra data after ASCII height grid");
    output = std::move(r);
    return true;
}

bool readHgt(const QByteArray &bytes, int lat, int lon, Raster &output, QString &error) {
    error.clear();
    const int side = int(std::sqrt(double(bytes.size()/2)));
    if (side < 2 || !dimensions(side,side) || qint64(side)*side*2 != bytes.size()
            || lat < -90 || lat >= 90 || lon < -180 || lon >= 180)
        return fail(error, "Invalid HGT size or geographic cell");
    Raster r;
    r.width = r.height = side; r.epsg = 4326;
    const double step = 1.0/(side-1);
    r.transform = {{lon-step/2,step,0,lat+1+step/2,0,-step}};
    r.hasNoData = true; r.noData = -32768;
    r.values.resize(qsizetype(side)*side);
    for (qsizetype i = 0; i < r.values.size(); ++i)
        r.values[i] = qFromBigEndian<qint16>(bytes.constData()+2*i);
    output = std::move(r);
    return true;
}

Sample sampleLegacyHgt(const Raster &r, Point p) {
    if (r.width < 2 || r.width != r.height || r.values.size() != qsizetype(r.width)*r.height)
        return {0,SampleStatus::Unavailable};
    const float lat = float(p.latitude), lon = float(p.longitude);
    if (!std::isfinite(lat) || !std::isfinite(lon)) return {0,SampleStatus::Outside};
    const float ly = r.width*(lat-std::floor(lat)), lx = r.width*(lon-std::floor(lon));
    int y = int(ly), x = int(lx);
    const float dy = ly-y, dx = lx-x;
    y = std::clamp(y,1,r.height-1); x = std::clamp(x,0,r.width-2);
    const int ids[] = {(r.height-y)*r.width+x, (r.height-y)*r.width+x+1,
                       (r.height-y-1)*r.width+x, (r.height-y-1)*r.width+x+1};
    const double weights[] = {(1-dy)*(1-dx),(1-dy)*dx,dy*(1-dx),dy*dx};
    return blend(r,ids,weights,false);
}
}
