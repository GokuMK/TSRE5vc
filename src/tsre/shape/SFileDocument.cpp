#include "SFileDocument.h"
#include <QFile>
#include <QHash>
#include <QSaveFile>
#include <QVarLengthArray>
#include <QtEndian>
#include <algorithm>
#define MINIZ_HEADER_FILE_ONLY
#include <cmath>
#include <cstring>
#include <functional>
#include <limits>
#include <mzip/miniz/miniz.h>
#include <tsre/fileFunctions/SimisTextReader.h>
namespace SFileDetail {
constexpr quint32 none = std::numeric_limits<quint32>::max();
struct Record {
    TS::TokenId id = 0;
    quint32 firstChild = none, nextSibling = none, valueOffset = 0, valueCount = 0;
    quint32 before = 0, extra = none;
    bool unknown = false, damaged = false;
};
struct Extra {
    QString name, label;
    QByteArray opaque, tail;
    quint32 packed = none;
};
struct Storage {
    bool animationDamage = false, otherTextDamage = false;
    std::vector<Record> records;
    std::vector<quint32> words;
    std::vector<unsigned char> types;
    std::vector<QString> strings;
    std::vector<Extra> extras;
    std::vector<PackedTable> packed;
};
namespace {
using Reader = SimisTextReader;
using Kind = Reader::Kind;
struct Layout {
    QByteArray prefix;
    QString pivot;
    QByteArray suffix;
    bool array = false;
    bool container = false;
};
const QHash<QString, Layout> &layouts() {
    static const auto result = [] {
        QHash<QString, Layout> m;
        auto leaves = [&](const char *names, const char *p) {
            for (auto n : QString(names).split(' '))
                m[n] = {p, {}, {}, false, false};
        };
        auto groups = [&](const char *names, const char *p) {
            for (auto n : QString(names).split(' '))
                m[n] = {p, {}, {}, false, true};
        };
        groups("shape indexed_trilist indexed_line_list sub_object "
               "distance_level_header distance_level lod_control anim_node "
               "shape_named_data",
               "");
        groups("volumes shader_names texture_filter_names points uv_points normals "
               "sort_vectors colours matrices images textures light_materials "
               "light_model_cfgs vtx_states prim_states primitives vertex_sets "
               "vertices geometry_nodes sub_objects distance_levels lod_controls "
               "tcb_rot tcb_pos linear_pos controllers anim_nodes animations uv_ops",
               "u");
        leaves("esd_detail_level esd_alternative_texture", "u");
        leaves("esd_bounding_box", "ffffff");
        leaves("esd_complex_box", "ffffffffffff");
        groups("esd_complex esd_snapable", "");
        leaves("shape_header", "hh");
        leaves("vector point", "fff");
        leaves("uv_point", "ff");
        leaves("colour", "ffff");
        leaves("matrix", "ffffffffffff");
        leaves("named_shader named_filter_mode image", "s");
        leaves("texture", "uufh");
        leaves("light_material", "huuuuf");
        leaves("vtx_state", "huiuhi");
        leaves("uv_op_share uv_op_copy point_list", "uu");
        leaves("uvop_copy uv_op_reflectmap uv_op_reflectmapfull uv_op_spheremap "
               "uv_op_spheremapfull uv_op_specularmap prim_state_idx "
               "shape_named_data_header",
               "u");
        leaves("uv_op_uniformscale uv_op_embossbump", "uuf");
        leaves("uv_op_nonuniformscale", "uuff");
        leaves("uv_op_transform", "uuffffff");
        leaves("uv_op_user_uninformscale uv_op_user_nonuninformscale "
               "uv_op_user_transform",
               "uuu");
        leaves("vertex_set cullable_prims", "uuu");
        leaves("dlevel_selection", "f");
        leaves("distance_levels_header", "uf");
        leaves("linear_key", "ufff");
        leaves("tcb_key", "ufffffffff");
        groups("slerp_rot", "u");
        groups("animation", "uu");
        groups("shape_named_geometry", "su");
        leaves("shape_geom_ref", "uuuuu");
        groups("geometry_info", "uuuuuuuuuu");
        groups("geometry_node", "uuuuu");
        groups("light_model_cfg", "h");
        m["vol_sphere"] = {"", "vector", "f", false, true};
        m["prim_state"] = {"hu", "tex_idxs", "fiuuu", false, true};
        m["vertex"] = {"huuhh", "vertex_uvs", "f", false, true};
        m["sub_object_header"] = {"hiihh", {}, "u", false, true};
        for (auto name : QString("tex_idxs vertex_idxs normal_idxs vertex_uvs "
                                 "subobject_shaders subobject_light_cfgs")
                             .split(' '))
            m[name] = {"u", {}, "u", true, false};
        m["flags"] = {"u", {}, "h", true, false};
        m["hierarchy"] = {"u", {}, "i", true, false};
        // Do not retain a QHash value reference across an insertion/rehash.
        m["geometry_node_map"] = {"u", {}, "i", true, false};
        return m;
    }();
    return result;
}
TS::TokenId tokenId(const QString &name) {
    static const auto ids = [] {
        QHash<QString, TS::TokenId> m;
        for (auto &e : TS::IdName)
            m.insert(QString(e.second).toLower(), e.first);
        return m;
    }();
    return ids.value(name.toLower(), 0);
}
void word(QByteArray &b, quint32 v) {
    const auto n = b.size();
    b.resize(n + 4);
    qToLittleEndian(v, reinterpret_cast<uchar *>(b.data() + n));
}
void shortWord(QByteArray &b, quint16 v) {
    const auto n = b.size();
    b.resize(n + 2);
    qToLittleEndian(v, reinterpret_cast<uchar *>(b.data() + n));
}
QByteArray utf16(const QString &s) {
    QByteArray b;
    b.reserve(s.size() * 2);
    for (auto c : s)
        shortWord(b, c.unicode());
    return b;
}
// Unlike qUncompress, this never grows beyond the validated envelope size.
QByteArray inflatePayload(const QByteArray &compressed, quint32 expected) {
    if (!expected || expected > 512 * 1024 * 1024)
        return {};
    QByteArray result(expected, Qt::Uninitialized);
    mz_ulong length = expected;
    const int status = mz_uncompress(
        reinterpret_cast<unsigned char *>(result.data()), &length,
        reinterpret_cast<const unsigned char *>(compressed.constData()), compressed.size());
    if (status != MZ_OK || length != expected)
        return {};
    return result;
}
const QHash<TS::TokenId, Layout> &idLayouts() {
    static const auto table = [] {
        QHash<TS::TokenId, Layout> out;
        for (auto it = layouts().cbegin(); it != layouts().cend(); ++it)
            if (auto id = tokenId(it.key()))
                out.insert(id, it.value());
        return out;
    }();
    return table;
}
const QString &knownName(TS::TokenId id) {
    static const auto names = [] {
        QHash<TS::TokenId, QString> out;
        for (const auto &entry : TS::IdName)
            out.insert(entry.first, QString::fromLatin1(entry.second).toLower());
        return out;
    }();
    static const QString unknown = "unknown";
    auto it = names.constFind(id);
    return it == names.cend() ? unknown : it.value();
}
Extra &extra(Storage &s, Record &r) {
    if (r.extra == none) {
        r.extra = s.extras.size();
        s.extras.emplace_back();
    }
    return s.extras[r.extra];
}
struct Value {
    quint32 bits = 0;
    unsigned char type = 0;
};
using Values = QVarLengthArray<Value, 16>;
Value textValue(Storage &s, char type, const Reader::Token &t) {
    Value v{0, static_cast<unsigned char>(type)};
    bool valid = false;
    double f = 0;
    qint32 i = 0;
    if (type == 'f') {
        valid = Reader::number(t, f) && std::abs(f) <= std::numeric_limits<float>::max();
        if (valid) {
            float native = float(f);
            std::memcpy(&v.bits, &native, 4);
        }
    } else if (type == 'i') {
        valid = Reader::integer(t, i);
        v.bits = quint32(i);
    } else if (type == 'u' || type == 'h') {
        valid = Reader::unsignedInteger(t, v.bits, type == 'h' ? 16 : 10);
    }
    if (!valid) {
        v.type |= 0x80;
        v.bits = s.strings.size();
        // Atom tokens may borrow input storage. Retained exceptional values own it.
        s.strings.emplace_back(t.text.constData(), t.text.size());
    }
    return v;
}
template <class T> void reserveAdditional(std::vector<T> &array, size_t count) {
    const auto needed = array.size() + count;
    if (needed > array.capacity())
        array.reserve(std::max(needed, array.capacity() + array.capacity() / 2 + 16));
}
void reserveValues(Storage &s, size_t count) {
    reserveAdditional(s.words, count);
    reserveAdditional(s.types, count);
}
void finishValues(Storage &s, Record &r, const Values &v) {
    reserveValues(s, v.size());
    r.valueOffset = s.words.size();
    r.valueCount = v.size();
    for (const auto &x : v) {
        s.words.push_back(x.bits);
        s.types.push_back(x.type);
    }
}
bool runtimeBlock(TS::TokenId id, const QString &name = {}) {
    if (!id) {
        const auto n = name.toLower();
        return n == "esd_detail_level" || n == "esd_alternative_texture" ||
               n == "esd_bounding_box" || n == "esd_complex" || n == "esd_complex_box" ||
               n == "esd_snapable";
    }
    switch (id) {
    case TS::ESD_Detail_Level:
    case TS::ESD_Alternative_Texture:
    case TS::ESD_Bounding_Box:
    case TS::ESD_Complex:
    case TS::ESD_Complex_Box:
    case TS::ESD_Snapable:
    case TS::shape:
    case TS::points:
    case TS::point:
    case TS::normals:
    case TS::vector:
    case TS::uv_points:
    case TS::uv_point:
    case TS::matrices:
    case TS::matrix:
    case TS::images:
    case TS::image:
    case TS::textures:
    case TS::texture:
    case TS::shader_names:
    case TS::named_shader:
    case TS::vtx_states:
    case TS::vtx_state:
    case TS::prim_states:
    case TS::prim_state:
    case TS::tex_idxs:
    case TS::lod_controls:
    case TS::lod_control:
    case TS::distance_levels:
    case TS::distance_level:
    case TS::distance_level_header:
    case TS::dlevel_selection:
    case TS::hierarchy:
    case TS::sub_objects:
    case TS::sub_object:
    case TS::sub_object_header:
    case TS::geometry_info:
    case TS::geometry_node_map:
    case TS::vertices:
    case TS::vertex:
    case TS::vertex_uvs:
    case TS::primitives:
    case TS::prim_state_idx:
    case TS::indexed_trilist:
    case TS::indexed_line_list:
    case TS::point_list:
    case TS::vertex_idxs:
    case TS::animations:
    case TS::animation:
    case TS::anim_nodes:
    case TS::anim_node:
    case TS::controllers:
    case TS::linear_pos:
    case TS::linear_key:
    case TS::tcb_pos:
    case TS::tcb_rot:
    case TS::tcb_key:
    case TS::slerp_rot:
        return true;
    default:
        return false;
    }
}
int packedWidth(TS::TokenId id) {
    return id == TS::uv_points ? 2
           : id == TS::points || id == TS::normals || id == TS::vertices ? 3 : 0;
}
void storePacked(Storage &s, Record &n, PackedTable table, quint32 count) {
    extra(s, n).packed = s.packed.size();
    s.packed.push_back(std::move(table));
    Values header;
    header.push_back({count, 'u'});
    finishValues(s, n, header);
}
// Speculate with an independent reader. Any noncanonical or invalid field falls
// back to the general permissive parser with the original cursor untouched.
// Reserve only as rows arrive: a declared count never determines allocation.
bool packedText(Reader &reader, TS::TokenId id, PackedTable &table, quint32 &count) {
    table.width = packedWidth(id);
    if (!table.width)
        return false;
    Reader r = reader;
    if (!r.readUnsignedInteger(count))
        return false;
    const auto rowName = QLatin1StringView(id == TS::vertices ? "vertex" : id == TS::points ? "point"
                                         : id == TS::normals ? "vector" : "uv_point");
    auto open = [&](QLatin1StringView name) {
        const auto token = r.next();
        if (token.kind != Kind::Atom || token.text.compare(name, Qt::CaseInsensitive) != 0)
            return false;
        auto delimiter = r.next();
        if (delimiter.kind == Kind::Atom || delimiter.kind == Kind::String)
            delimiter = r.next(); // Optional label; Compact does not use it.
        return delimiter.kind == Kind::Open;
    };
    auto numeric = [&](quint32 &bits) {
        double number;
        if (!r.readNumber(number) ||
            std::abs(number) > std::numeric_limits<float>::max())
            return false;
        const float value = float(number);
        std::memcpy(&bits, &value, sizeof(value));
        return true;
    };
    while (r.peekKind() != Kind::Close) {
        if (!open(rowName))
            return false;
        quint32 row[3] = {};
        if (id == TS::vertices) {
            quint32 ignored;
            if (!r.readUnsignedInteger(ignored, 16) ||
                !r.readUnsignedInteger(row[0]) ||
                !r.readUnsignedInteger(row[1]) ||
                !r.readUnsignedInteger(ignored, 16) ||
                !r.readUnsignedInteger(ignored, 16) || !open(QLatin1StringView("vertex_uvs")))
                return false;
            quint32 declaredUvs;
            if (!r.readUnsignedInteger(declaredUvs))
                return false;
            row[2] = none;
            quint32 actualUvs = 0;
            while (r.peekKind() != Kind::Close) {
                if (!r.readUnsignedInteger(ignored))
                    return false;
                if (!actualUvs++)
                    row[2] = ignored;
            }
            r.next();
            if (actualUvs != declaredUvs)
                return false;
            if (r.peekKind() != Kind::Close && !numeric(ignored))
                return false; // Optional vertex scalar.
        } else {
            for (int i = 0; i < table.width; ++i)
                if (!numeric(row[i]))
                    return false;
        }
        if (r.next().kind != Kind::Close)
            return false;
        table.words.insert(table.words.end(), row, row + table.width);
    }
    r.next();
    reader = std::move(r);
    return true;
}
void link(Storage &s, Record &parent, quint32 &last, quint32 child, quint32 before) {
    s.records[child].before = before;
    if (last == none)
        parent.firstChild = child;
    else
        s.records[last].nextSibling = child;
    last = child;
}
char valueType(const Layout *l, int position, bool suffix) {
    if (!l)
        return 'a';
    const auto &p = suffix ? l->suffix : l->prefix;
    if (l->array && position >= p.size())
        return l->suffix[0];
    if (position < p.size())
        return p[position];
    if (l->container && !l->suffix.isEmpty())
        return l->suffix[0];
    return 'a';
}
// Flat integer arrays append native values directly in both retention modes.
// Irregular content rolls back the cursor and appended words before retrying
// the general parser. Declared counts never determine allocation.
bool textArray(Reader &reader, Storage &s, Record &n, const Layout &layout) {
    Reader r = reader;
    const auto start = s.words.size();
    while (r.peekKind() != Kind::Close) {
        const auto position = s.words.size() - start;
        const char type = position < size_t(layout.prefix.size()) ? layout.prefix[int(position)]
                                                                : layout.suffix[0];
        quint32 bits = 0;
        bool valid = false;
        if (type == 'u' || type == 'h')
            valid = r.readUnsignedInteger(bits, type == 'h' ? 16 : 10);
        else if (type == 'i') {
            qint32 value = 0;
            valid = r.readInteger(value);
            bits = quint32(value);
        }
        if (!valid) {
            s.words.resize(start);
            s.types.resize(start);
            return false;
        }
        s.words.push_back(bits);
        s.types.push_back(type);
    }
    r.next();
    n.valueOffset = start;
    n.valueCount = s.words.size() - start;
    reader = std::move(r);
    return true;
}
bool textHeader(Reader &r, Document &doc, QString &label) {
    const auto offset = r.position();
    bool recovered = false;
    if (!r.readBlockHeader(label, recovered))
        return false;
    if (recovered)
        doc.diagnostics << QString("Recovered unquoted multi-word label at %1: %2")
                               .arg(offset).arg(label);
    return true;
}
// Keep label allocation/diagnostics out of the dense numeric parsing routine.
Q_NEVER_INLINE bool textLabel(Reader &r, Document &doc, Extra &header) {
    if ((r.peekKind() == Kind::Atom || r.peekKind() == Kind::String) &&
        r.peekKind(1) == Kind::Open) {
        const auto label = r.next();
        header.label = QString(label.text.constData(), label.text.size());
        r.next();
        return true;
    }
    QString label;
    if (!textHeader(r, doc, label))
        return false;
    header.label = std::move(label);
    return true;
}
quint32 textBlock(Reader &r, Reader::Token name, Document &doc, bool first, int depth,
                  bool inAnimations = false, bool mainRoot = false) {
    auto &s = *doc.storage;
    const auto index = quint32(s.records.size());
    reserveAdditional(s.records, 1);
    s.records.emplace_back();
    Record n;
    n.id = tokenId(name.text);
    auto markDamage = [&](bool animationOnly) {
        n.damaged = doc.damaged = true;
        (animationOnly ? s.animationDamage : s.otherTextDamage) = true;
    };
    const auto normalized = name.text.toLower();
    auto it = layouts().constFind(normalized);
    const Layout *layout = it == layouts().cend() ? nullptr : &it.value();
    n.unknown = !layout;
    if (n.unknown || !n.id)
        extra(s, n).name = QString(name.text.constData(), name.text.size());
    if (r.peekKind() == Kind::Open) {
        r.next();
    } else if (!textLabel(r, doc, extra(s, n))) {
        markDamage(inAnimations);
        doc.diagnostics << QString("Invalid block header for %1 at %2")
                               .arg(normalized).arg(r.position());
        s.records[index] = n;
        return index;
    }
    if (depth > 128) {
        markDamage(inAnimations);
        doc.diagnostics << "Text nesting limit";
        r.skipBlock();
        s.records[index] = n;
        return index;
    }
    if (layout && layout->array && textArray(r, s, n, *layout)) {
        s.records[index] = n;
        return index;
    }
    if (doc.compact && depth < 127 && packedWidth(n.id)) {
        PackedTable table;
        quint32 count;
        if (packedText(r, n.id, table, count)) {
            storePacked(s, n, std::move(table), count);
            s.records[index] = n;
            return index;
        }
    }
    Values values;
    int lod = 0, position = 0;
    bool suffix = false;
    quint32 last = none;
    for (;;) {
        auto t = r.next();
        if (t.kind == Kind::Close)
            break;
        if (t.kind == Kind::End || t.kind == Kind::Error) {
            // An unclosed terminal animation subtree can consume the shape's
            // closing bracket. Earlier/static damage must still remain fatal.
            const bool animationTail = t.kind == Kind::End && mainRoot &&
                n.id == TS::shape && last != none && s.records[last].id == TS::animations;
            markDamage(inAnimations || animationTail);
            doc.diagnostics << QString("Unclosed %1 at %2").arg(normalized).arg(t.begin);
            break;
        }
        const bool leadingString =
            values.empty() && last == none && layout && layout->prefix.startsWith('s');
        bool block = false;
        if (!leadingString && t.kind == Kind::Atom && !t.text.isEmpty() &&
            (t.text.at(0).isLetter() || t.text.at(0) == '_') &&
            !(n.id == TS::shape && doc.fileKind == 't' && values.empty() && last == none)) {
            const auto k = r.peekKind();
            block = k == Kind::Open;
            if (!block && (k == Kind::Atom || k == Kind::String) && r.peekKind(1) == Kind::Open) {
                quint32 hex;
                block = !Reader::unsignedInteger(t, hex, 16);
            }
            // Recovery is a block-header operation, after the parent's values.
            // Numeric data never enters this branch.
            if (!block && layout && layout->container &&
                position >= (suffix ? layout->suffix.size() : layout->prefix.size()) &&
                tokenId(t.text) != 0)
                block = true;
        }
        if (block) {
            const auto id = tokenId(t.text);
            const bool omittedLod =
                first && n.id == TS::distance_levels && id == TS::distance_level && lod++ > 0;
            if (omittedLod || (doc.compact && !runtimeBlock(id, t.text))) {
                QString label;
                if (!textHeader(r, doc, label) || !r.skipBlock())
                    markDamage(inAnimations);
                if (omittedLod)
                    doc.partial = true;
                ++doc.skippedBlocks;
            } else {
                auto child = textBlock(r, t, doc, first, depth + 1,
                    inAnimations || (mainRoot && n.id == TS::shape && id == TS::animations));
                link(s, n, last, child, values.size());
            }
            if (layout && !layout->pivot.isEmpty() &&
                t.text.compare(layout->pivot, Qt::CaseInsensitive) == 0) {
                suffix = true;
                position = 0;
            }
        } else if (t.kind == Kind::Atom || t.kind == Kind::String) {
            char type = valueType(layout, position++, suffix);
            if (t.kind == Kind::String ||
                (n.id == TS::shape && doc.fileKind == 't' && values.empty()))
                type = 's';
            values.push_back(textValue(s, type, t));
        } else {
            markDamage(inAnimations);
            doc.diagnostics << "Unexpected opening delimiter";
            r.skipBlock();
        }
    }
    if (n.id == TS::slerp_rot && n.firstChild == none && values.size() >= 5) {
        for (int i = 1; i < 5; ++i) {
            const auto old = values[i];
            if (old.type & 0x80) {
                Reader::Token t{Kind::Atom, s.strings[old.bits]};
                values[i] = textValue(s, 'f', t);
            }
        }
    }
    finishValues(s, n, values);
    s.records[index] = n;
    return index;
}
struct BinaryReader {
    const QByteArray &b;
    Document &doc;
    bool first;
    quint32 u(int &p, int end) {
        if (p < 0 || end - p < 4)
            throw QString("Truncated scalar");
        auto v = qFromLittleEndian<quint32>(b.constData() + p);
        p += 4;
        return v;
    }
    QString string(int &p, int end, int countBytes) {
        if (end - p < countBytes)
            throw QString("Truncated string length");
        int count = countBytes == 1 ? uchar(b[p]) : qFromLittleEndian<quint16>(b.constData() + p);
        p += countBytes;
        if (count > (end - p) / 2)
            throw QString("Truncated UTF-16 string");
        QString out(count, Qt::Uninitialized);
        for (int i = 0; i < count; ++i)
            out[i] = QChar(qFromLittleEndian<quint16>(b.constData() + p + i * 2));
        p += count * 2;
        return out;
    }
    Value atom(char type, int &p, int end) {
        if (type != 's')
            return {u(p, end), static_cast<unsigned char>(type)};
        const auto index = quint32(doc.storage->strings.size());
        doc.storage->strings.push_back(string(p, end, 2));
        return {index, static_cast<unsigned char>(type | 0x80)};
    }
    bool packedBinary(int start, int end, TS::TokenId id, PackedTable &table, quint32 &count) {
        table.width = packedWidth(id);
        if (!table.width || end - start < 4)
            return false;
        int p = start;
        count = u(p, end);
        const auto rowId = id == TS::vertices ? TS::vertex : id == TS::points ? TS::point
                           : id == TS::normals ? TS::vector : TS::uv_point;
        auto open = [&](int &at, int limit, TS::TokenId expected, int &stop) {
            if (limit - at < 9 || qFromLittleEndian<quint32>(b.constData() + at) != expected)
                return false;
            const auto bytes = qFromLittleEndian<quint32>(b.constData() + at + 4);
            if (!bytes || bytes > quint32(limit - at - 8))
                return false;
            at += 8;
            stop = at + bytes;
            const int labelBytes = 1 + 2 * uchar(b[at]);
            if (labelBytes > stop - at)
                return false;
            at += labelBytes;
            return true;
        };
        // Minimum row size bounds capacity independently of the declared count.
        const int rowBytes = id == TS::vertices ? 42 : 9 + table.width * 4;
        table.words.reserve(size_t(end - p) / rowBytes * table.width);
        while (p < end) {
            int stop;
            if (!open(p, end, rowId, stop))
                return false;
            quint32 row[3] = {};
            if (id == TS::vertices) {
                if (stop - p < 20)
                    return false;
                row[0] = qFromLittleEndian<quint32>(b.constData() + p + 4);
                row[1] = qFromLittleEndian<quint32>(b.constData() + p + 8);
                p += 20;
                int uvStop;
                if (!open(p, stop, TS::vertex_uvs, uvStop) || uvStop - p < 4)
                    return false;
                const auto uvCount = u(p, uvStop);
                if ((uvStop - p) % 4 || uvCount != quint32((uvStop - p) / 4))
                    return false;
                row[2] = uvCount ? u(p, uvStop) : none;
                p = uvStop;
                if (stop - p != 0 && stop - p != 4)
                    return false;
                p = stop;
            } else {
                if (stop - p != table.width * 4)
                    return false;
                for (int i = 0; i < table.width; ++i)
                    row[i] = u(p, stop);
            }
            table.words.insert(table.words.end(), row, row + table.width);
        }
        return true;
    }
    quint32 block(int &p, int end, int depth = 0) {
        auto &s = *doc.storage;
        Record n;
        n.id = u(p, end);
        const auto size = u(p, end);
        const int start = p;
        if (size > quint32(end - p))
            throw QString("Child length exceeds parent");
        const int stop = p + int(size);
        if (depth > 128)
            throw QString("Binary nesting limit");
        const auto index = quint32(s.records.size());
        reserveAdditional(s.records, 1);
        s.records.emplace_back();
        Values values;
        try {
            auto label = string(p, stop, 1);
            if (!label.isEmpty())
                extra(s, n).label = std::move(label);
            auto it = idLayouts().constFind(n.id);
            if (it == idLayouts().cend()) {
                n.unknown = true;
                extra(s, n).opaque = b.mid(start, size);
                p = stop;
                s.records[index] = n;
                return index;
            }
            static const Layout directQuaternion{"uffff", {}, {}, false, false};
            const auto &l = n.id == TS::slerp_rot && stop - p == 20 ? directQuaternion : it.value();
            if (doc.compact && depth < 127 && packedWidth(n.id)) {
                PackedTable table;
                quint32 count;
                if (packedBinary(p, stop, n.id, table, count)) {
                    storePacked(s, n, std::move(table), count);
                    p = stop;
                    s.records[index] = n;
                    return index;
                }
            }
            if (!l.container && !l.prefix.contains('s')) {
                // Numeric leaves and index arrays are already contiguous words on
                // disk. Copy once into typed storage; no scalar objects or scratch
                // arrays, and no allocations based on the declared element count.
                const auto count =
                    l.array ? (stop - p) / 4 : std::min<int>(l.prefix.size(), (stop - p) / 4);
                reserveValues(s, count);
                n.valueOffset = s.words.size();
                n.valueCount = count;
                s.words.resize(s.words.size() + count);
#if Q_BYTE_ORDER == Q_LITTLE_ENDIAN
                if (count)
                    std::memcpy(s.words.data() + n.valueOffset, b.constData() + p, count * 4);
#else
                for (int i = 0; i < count; ++i)
                    s.words[n.valueOffset + i] =
                        qFromLittleEndian<quint32>(b.constData() + p + i * 4);
#endif
                for (int i = 0; i < count; ++i)
                    s.types.push_back(i < l.prefix.size() ? l.prefix[i] : l.suffix[0]);
                p += count * 4;
                if (p < stop) {
                    if (l.array || count < l.prefix.size()) {
                        n.damaged = doc.damaged = true;
                        doc.diagnostics << knownName(n.id) + ": Truncated scalar";
                    } else if (!doc.compact)
                        doc.diagnostics << "Preserved extra payload in " + knownName(n.id);
                    if (!doc.compact || n.damaged)
                        extra(s, n).tail = b.mid(p, stop - p);
                }
                p = stop;
                s.records[index] = n;
                return index;
            }
            for (char type : l.prefix) {
                if (p == stop)
                    break;
                values.push_back(atom(type, p, stop));
            }
            if (l.array) {
                values.reserve(values.size() + (stop - p) / 4);
                while (p < stop)
                    values.push_back(atom(l.suffix[0], p, stop));
            } else if (l.container) {
                if (doc.compact && !values.empty()) {
                    int width = n.id == TS::points || n.id == TS::normals ? 3
                                : n.id == TS::uv_points                   ? 2
                                : n.id == TS::vertices                    ? 7
                                                                          : 0;
                    if (width) {
                        const size_t count = std::min<size_t>(values[0].bits, (stop - p) / 9);
                        reserveAdditional(s.records, count * (n.id == TS::vertices ? 2 : 1));
                        reserveValues(s, count * width);
                    }
                }
                int lod = 0;
                quint32 last = none;
                while (p < stop) {
                    if (n.id == TS::sub_object_header && stop - p == 4) {
                        values.push_back(atom('u', p, stop));
                        break;
                    }
                    if (stop - p < 8)
                        throw QString("Truncated child header");
                    const auto id = qFromLittleEndian<quint32>(b.constData() + p);
                    const bool omittedLod = first && n.id == TS::distance_levels &&
                                            id == TS::distance_level && lod++ > 0;
                    if (omittedLod || (doc.compact && !runtimeBlock(id))) {
                        u(p, stop);
                        const auto skip = u(p, stop);
                        if (skip > quint32(stop - p))
                            throw QString("Invalid skipped block length");
                        p += skip;
                        if (omittedLod)
                            doc.partial = true;
                        ++doc.skippedBlocks;
                    } else {
                        const auto c = block(p, stop, depth + 1);
                        link(s, n, last, c, values.size());
                    }
                    if (!l.pivot.isEmpty() && id == tokenId(l.pivot)) {
                        for (char type : l.suffix) {
                            if (p == stop)
                                break;
                            values.push_back(atom(type, p, stop));
                        }
                        break;
                    }
                }
            }
            if (p < stop && !doc.compact) {
                extra(s, n).tail = b.mid(p, stop - p);
                doc.diagnostics << "Preserved extra payload in " + knownName(n.id);
            }
        } catch (const QString &e) {
            n.damaged = doc.damaged = true;
            doc.diagnostics << knownName(n.id) + ": " + e;
            extra(s, n).tail = b.mid(p, stop - p);
        }
        p = stop;
        finishValues(s, n, values);
        s.records[index] = n;
        return index;
    }
};
void writeText(const Node &node, int depth, QString &out, QString &error) {
    const auto &s = *node.store;
    const auto &n = s.records[node.index];
    if (n.extra != none &&
        (!s.extras[n.extra].opaque.isEmpty() || !s.extras[n.extra].tail.isEmpty())) {
        error = "Opaque binary data cannot be converted to text";
        return;
    }
    out += QString(depth * 2, ' ') + node.name();
    if (!node.label().isEmpty())
        out += ' ' + Reader::quote(node.label());
    out += " (";
    quint32 v = 0;
    auto scalar = [&] {
        out += ' ';
        out += node.scalarType(v) == 's' ? Reader::quote(node.scalar(v)) : node.scalar(v);
        ++v;
    };
    for (auto c : node.children()) {
        while (v < s.records[c.index].before && v < n.valueCount)
            scalar();
        out += '\n';
        writeText(c, depth + 1, out, error);
    }
    while (v < n.valueCount)
        scalar();
    if (n.firstChild != none)
        out += '\n' + QString(depth * 2, ' ');
    out += " )";
}
QString textNode(const Node &n, int depth, QString &error) {
    QString out;
    writeText(n, depth, out, error);
    return out;
}
void writeBinary(const Node &node, QByteArray &out, QString &error) {
    const auto &s = *node.store;
    const auto &n = s.records[node.index];
    if ((!n.id || n.unknown) && (n.extra == none || s.extras[n.extra].opaque.isEmpty())) {
        error = "No binary token/schema for " + node.name();
        return;
    }
    word(out, n.id);
    const auto sizeAt = out.size();
    word(out, 0);
    const auto begin = out.size();
    if (n.extra != none && !s.extras[n.extra].opaque.isEmpty())
        out += s.extras[n.extra].opaque;
    else {
        const auto label = node.label();
        if (label.size() > 255) {
            error = "Binary label exceeds 255 UTF-16 units";
            return;
        }
        out.append(char(label.size()));
        out += utf16(label);
        quint32 v = 0;
        auto scalar = [&] {
            const auto at = n.valueOffset + v++;
            const auto type = s.types[at];
            if (!(type & 0x80)) {
                word(out, s.words[at]);
                return;
            }
            const auto &value = s.strings[s.words[at]];
            if ((type & 0x7f) == 's') {
                if (value.size() > 65535) {
                    error = "Binary string exceeds 65535 units";
                    return;
                }
                shortWord(out, value.size());
                out += utf16(value);
                return;
            }
            error = "No valid binary representation for scalar: " + value;
        };
        for (auto c : node.children()) {
            while (v < s.records[c.index].before && v < n.valueCount)
                scalar();
            writeBinary(c, out, error);
        }
        while (v < n.valueCount)
            scalar();
        if (n.extra != none)
            out += s.extras[n.extra].tail;
    }
    qToLittleEndian<quint32>(out.size() - begin, out.data() + sizeAt);
}
QByteArray binaryNode(const Node &n, QString &error) {
    QByteArray out;
    writeBinary(n, out, error);
    return out;
}
} // namespace
QString Node::name() const {
    if (!store)
        return {};
    const auto &r = store->records[index];
    if (r.extra != none && !store->extras[r.extra].name.isEmpty())
        return store->extras[r.extra].name;
    return knownName(r.id);
}
QString Node::label() const {
    if (!store || store->records[index].extra == none)
        return {};
    return store->extras[store->records[index].extra].label;
}
QByteArray Node::tail() const {
    if (!store || store->records[index].extra == none)
        return {};
    return store->extras[store->records[index].extra].tail;
}
TS::TokenId Node::id() const { return store ? store->records[index].id : 0; }
Children Node::children(const QString &name) const {
    return {*this, tokenId(name), name, !name.isEmpty()};
}
Node Node::child(TS::TokenId id) const {
    if (!store)
        return {};
    for (auto c = store->records[index].firstChild; c != none; c = store->records[c].nextSibling)
        if (store->records[c].id == id)
            return {store, c};
    return {};
}
Node Node::child(const QString &name) const {
    for (auto c : children(name))
        return c;
    return {};
}
void Children::Iterator::seek() {
    if (!range->filtered || current == none)
        return;
    const auto &s = *range->parent.store;
    while (current != none) {
        const Node n{range->parent.store, current};
        if (range->filter ? n.id() == range->filter
                          : n.name().compare(range->unknownName, Qt::CaseInsensitive) == 0)
            break;
        current = s.records[current].nextSibling;
    }
}
Children::Iterator &Children::Iterator::operator++() {
    current = range->parent.store->records[current].nextSibling;
    seek();
    return *this;
}
Children::Iterator Children::begin() const {
    Iterator it{this, parent ? parent.store->records[parent.index].firstChild : none};
    it.seek();
    return it;
}
Children::Iterator Children::end() const { return {this, none}; }
bool Children::empty() const { return begin().current == none; }
size_t Children::size() const {
    size_t count = 0;
    for (auto n : *this) {
        (void)n;
        ++count;
    }
    return count;
}
Node Children::operator[](size_t index) const {
    for (auto n : *this)
        if (index-- == 0)
            return n;
    return {};
}
float PackedTable::number(size_t index) const {
    float result;
    std::memcpy(&result, &words[index], sizeof(result));
    return result;
}
int PackedTable::integer(size_t index) const {
    const auto value = words[index];
    return value <= quint32(std::numeric_limits<int>::max()) ? int(value) : -1;
}
const PackedTable *Node::packed() const {
    if (!store)
        return nullptr;
    const auto e = store->records[index].extra;
    if (e == none || store->extras[e].packed == none)
        return nullptr;
    return &store->packed[store->extras[e].packed];
}
int Node::scalarCount() const { return store ? store->records[index].valueCount : 0; }
char Node::scalarType(int i) const {
    return i >= 0 && i < scalarCount() ? store->types[store->records[index].valueOffset + i] & 0x7f
                                       : 0;
}
QString Node::scalar(int i, const QString &fallback) const {
    if (i < 0 || i >= scalarCount())
        return fallback;
    const auto at = store->records[index].valueOffset + i;
    const auto type = store->types[at];
    const auto bits = store->words[at];
    if (type & 0x80)
        return store->strings[bits];
    if (type == 'f') {
        float f;
        std::memcpy(&f, &bits, 4);
        return QString::number(f, 'g', 9);
    }
    if (type == 'i')
        return QString::number(qint32(bits));
    return QString::number(bits, type == 'h' ? 16 : 10);
}
double Node::number(int i, double fallback) const {
    if (i < 0 || i >= scalarCount())
        return fallback;
    const auto at = store->records[index].valueOffset + i;
    const auto type = store->types[at];
    const auto bits = store->words[at];
    if (type & 0x80) {
        bool ok;
        auto v = store->strings[bits].toDouble(&ok);
        return ok && std::isfinite(v) ? v : fallback;
    }
    if (type == 'f') {
        float f;
        std::memcpy(&f, &bits, 4);
        return std::isfinite(f) ? double(f) : fallback;
    }
    return type == 'i' ? double(qint32(bits)) : double(bits);
}
int Node::integer(int i, int fallback) const {
    if (i < 0 || i >= scalarCount())
        return fallback;
    const auto at = store->records[index].valueOffset + i;
    const auto type = store->types[at];
    const auto bits = store->words[at];
    if (type & 0x80) {
        bool ok;
        const auto n = store->strings[bits].toInt(&ok);
        return ok ? n : fallback;
    }
    if (type == 'i')
        return qint32(bits);
    if (type == 'u' || type == 'h')
        return bits <= quint32(std::numeric_limits<int>::max()) ? int(bits) : fallback;
    const auto n = number(i, double(fallback));
    return n >= std::numeric_limits<int>::min() && n <= std::numeric_limits<int>::max() &&
                   std::trunc(n) == n
               ? int(n)
               : fallback;
}
bool Node::setScalar(int i, const QString &value, QString *error) {
    if (error)
        error->clear();
    auto fail = [&](const QString &e) {
        if (error)
            *error = e;
        return false;
    };
    if (i < 0 || i >= scalarCount())
        return fail("Missing scalar field");
    const auto at = store->records[index].valueOffset + i;
    const char type = scalarType(i);
    Reader::Token t{Kind::Atom, value};
    auto v = textValue(*store, type, t);
    if ((v.type & 0x80) && type != 's' && type != 'a') {
        store->strings.pop_back();
        return fail("Value does not match field type/range");
    }
    if (type == 'a' &&
        (value.isEmpty() || value.contains('(') || value.contains(')') || value.contains('"') ||
         std::any_of(value.cbegin(), value.cend(), [](QChar c) { return c.isSpace(); }))) {
        store->strings.pop_back();
        return fail("Extra unquoted scalar must be a single atom");
    }
    if (store->types[at] & 0x80) {
        const auto previous = store->words[at];
        if (v.type & 0x80) {
            store->strings[previous] = std::move(store->strings.back());
            store->strings.pop_back();
            v.bits = previous;
        } else
            store->strings[previous].clear();
    }
    store->words[at] = v.bits;
    store->types[at] = v.type;
    return true;
}
void Node::appendCopy(Node source) {
    if (!*this || !source)
        return;
    // Copy through an independent snapshot so growing the destination arenas
    // cannot invalidate source references, even when cloning a sibling in the
    // same document.
    const Storage snapshot = *source.store;
    std::function<quint32(quint32)> copy = [&](quint32 from) {
        const Record original = snapshot.records[from];
        Record r = original;
        const auto to = quint32(store->records.size());
        store->records.emplace_back();
        r.firstChild = r.nextSibling = none;
        r.valueOffset = store->words.size();
        if (r.extra != none) {
            r.extra = store->extras.size();
            store->extras.push_back(snapshot.extras[original.extra]);
            auto &e = store->extras.back();
            if (e.packed != none) {
                const auto fromTable = e.packed;
                e.packed = store->packed.size();
                store->packed.push_back(snapshot.packed[fromTable]);
            }
        }
        for (quint32 i = 0; i < r.valueCount; ++i) {
            auto type = snapshot.types[original.valueOffset + i];
            auto bits = snapshot.words[original.valueOffset + i];
            if (type & 0x80) {
                auto text = snapshot.strings[bits];
                bits = store->strings.size();
                store->strings.push_back(text);
            }
            store->words.push_back(bits);
            store->types.push_back(type);
        }
        quint32 last = none;
        for (auto c = original.firstChild; c != none; c = snapshot.records[c].nextSibling) {
            const auto child = copy(c);
            link(*store, r, last, child, snapshot.records[c].before);
        }
        store->records[to] = r;
        return to;
    };
    const auto child = copy(source.index);
    auto &r = store->records[index];
    auto last = r.firstChild;
    if (last != none)
        while (store->records[last].nextSibling != none)
            last = store->records[last].nextSibling;
    link(*store, r, last, child, r.valueCount);
}
Document::Document() : storage(std::make_unique<Storage>()) {}
Document::~Document() = default;
Document::Document(Document &&) noexcept = default;
Document &Document::operator=(Document &&) noexcept = default;
Document::Document(const Document &o) : Document() { *this = o; }
Document &Document::operator=(const Document &o) {
    if (this == &o)
        return *this;
    storage = std::make_unique<Storage>(*o.storage);
    root = o.root ? Node{storage.get(), o.root.index} : Node{};
    extraRoots.clear();
    for (auto n : o.extraRoots)
        extraRoots.push_back({storage.get(), n.index});
    diagnostics = o.diagnostics;
    fileKind = o.fileKind;
    binary = o.binary;
    compressed = o.compressed;
    damaged = o.damaged;
    partial = o.partial;
    compact = o.compact;
    skippedBlocks = o.skippedBlocks;
    original = o.original;
    return *this;
}
bool Document::read(const QString &path, bool first, bool wantCompact) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        diagnostics << f.errorString();
        return false;
    }
    if (f.size() > 512 * 1024 * 1024) {
        *this = Document();
        damaged = true;
        diagnostics << "Shape input exceeds 512 MiB";
        return false;
    }
    return readBytes(f.readAll(), first, wantCompact);
}
bool Document::readBytes(QByteArray bytes, bool first, bool wantCompact) {
    *this = Document();
    original = bytes;
    compact = wantCompact;
    if (bytes.size() > 512 * 1024 * 1024) {
        diagnostics << "Shape input exceeds 512 MiB";
        damaged = true;
        return false;
    }
    const bool wide = bytes.startsWith("\xff\xfe");
    const bool wideCompressed = wide && bytes.mid(2, 16) == utf16("SIMISA@F");
    if (wideCompressed) {
        compressed = true;
        if (bytes.size() < 34) {
            damaged = true;
            diagnostics << "Truncated Unicode envelope";
            return false;
        }
        auto expected = qFromLittleEndian<quint32>(bytes.constData() + 18);
        if (expected > 512 * 1024 * 1024) {
            damaged = true;
            diagnostics << "Inflated shape exceeds 512 MiB";
            return false;
        }
        bytes = inflatePayload(bytes.mid(34), expected);
        if (bytes.size() != expected || bytes.isEmpty()) {
            damaged = true;
            diagnostics << "Invalid compressed Unicode envelope";
            return false;
        }
        bytes = QByteArray("\xff\xfe", 2) + utf16("SIMISA@@@@@@@@@@") + bytes;
    }
    if (bytes.startsWith("SIMISA@F")) {
        compressed = true;
        if (bytes.size() < 16) {
            damaged = true;
            return false;
        }
        auto expected = qFromLittleEndian<quint32>(bytes.constData() + 8);
        if (expected > 512 * 1024 * 1024) {
            diagnostics << "Inflated shape exceeds 512 MiB";
            damaged = true;
            return false;
        }
        bytes = inflatePayload(bytes.mid(16), expected);
        if (bytes.size() != expected || bytes.isEmpty()) {
            diagnostics << "Invalid compressed shape envelope";
            damaged = true;
            return false;
        }
        bytes = QByteArray("SIMISA@@@@@@@@@@") + bytes;
    }
    binary = bytes.size() >= 32 && bytes.mid(16, 4) == "JINX" && bytes[23] == 'b';
    // Reserve modestly from bounded input size, never from untrusted declared
    // counts.
    if (!compact) {
        storage->records.reserve(bytes.size() / (binary ? 24 : 100));
        storage->words.reserve(bytes.size() / (binary ? 5 : 20));
        storage->types.reserve(storage->words.capacity());
    }
    if (binary) {
        fileKind = bytes[21];
        BinaryReader r{bytes, *this, first};
        int p = 32;
        try {
            root = {storage.get(), r.block(p, bytes.size())};
            while (p < bytes.size()) {
                if (compact) {
                    r.u(p, bytes.size());
                    auto count = r.u(p, bytes.size());
                    if (count > quint32(bytes.size() - p))
                        throw QString("Invalid extra root length");
                    p += count;
                    ++skippedBlocks;
                } else
                    extraRoots.push_back({storage.get(), r.block(p, bytes.size())});
            }
        } catch (const QString &e) {
            damaged = true;
            diagnostics << e;
        }
    } else {
        QString source, error;
        if (!Reader::decode(bytes, source, error)) {
            damaged = true;
            diagnostics << error;
            return false;
        }
        Reader r(source);
        auto t = r.next();
        if (t.text.startsWith("SIMISA", Qt::CaseInsensitive)) {
            if (t.text.size() > 21)
                fileKind = t.text.at(21).toLatin1();
            t = r.next();
        }
        if (t.kind != Kind::Atom || (tokenId(t.text) != TS::shape &&
            r.peekKind() != Kind::Open && r.peekKind(1) != Kind::Open)) {
            damaged = true;
            diagnostics << "Missing shape root";
            return false;
        }
        root = {storage.get(), textBlock(r, t, *this, first, 0, false, true)};
        while (r.peekKind() != Kind::End) {
            auto t = r.next();
            if (t.kind == Kind::Atom && t.text.compare("EOF", Qt::CaseInsensitive) == 0)
                continue;
            if (t.kind != Kind::Atom ||
                (r.peekKind() != Kind::Open && r.peekKind(1) != Kind::Open)) {
                damaged = true;
                storage->otherTextDamage = true;
                diagnostics << "Unframed text after root";
                break;
            }
            if (compact) {
                if (r.peekKind() != Kind::Open)
                    r.next();
                r.next();
                if (!r.skipBlock()) {
                    damaged = true;
                    storage->otherTextDamage = true;
                }
                ++skippedBlocks;
            } else
                extraRoots.push_back({storage.get(), textBlock(r, t, *this, first, 0)});
        }
    }
    if (!damaged)
        original.clear();
    return bool(root);
}
bool Document::damageConfinedToAnimations() const {
    return damaged && !binary && root && root.id() == TS::shape &&
        storage->animationDamage && !storage->otherTextDamage;
}
QByteArray Document::encode(bool bin, bool zip, QString &error) const {
    error.clear();
    if (damaged || partial || compact || !storage->packed.empty()) {
        error = "Document requires a complete, undamaged load before saving";
        return {};
    }
    QByteArray payload;
    if (bin) {
        payload = QByteArray("JINX0") + fileKind + "1b________" + binaryNode(root, error);
        for (auto &n : extraRoots)
            payload += binaryNode(n, error);
    } else {
        QString text = QString("SIMISA@@@@@@@@@@JINX0") + QChar(fileKind) + "1t______\r\n" +
                       textNode(root, 0, error) + "\r\n";
        for (auto &n : extraRoots)
            text += textNode(n, 0, error) + "\r\n";
        payload = utf16(text);
    }
    if (!error.isEmpty())
        return {};
    if (!bin) {
        if (zip) {
            auto body = payload.mid(32);
            QByteArray out("\xff\xfe", 2);
            out += utf16("SIMISA@F");
            word(out, body.size());
            out += utf16("@@@@@@");
            return out + qCompress(body, 6).mid(4);
        }
        return QByteArray("\xff\xfe", 2) + payload;
    }
    if (!zip)
        return QByteArray("SIMISA@@@@@@@@@@") + payload;
    QByteArray out = "SIMISA@F";
    word(out, payload.size());
    out += "@@@@";
    return out + qCompress(payload, 6).mid(4);
}
bool Document::save(const QString &path, bool bin, bool zip, QString &error) const {
    auto bytes = encode(bin, zip, error);
    if (!error.isEmpty())
        return false;
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(bytes) != bytes.size() || !f.commit()) {
        error = f.errorString();
        return false;
    }
    return true;
}
qsizetype Document::storageBytes() const {
    qsizetype bytes = sizeof(Storage) + storage->records.capacity() * sizeof(Record) +
                      storage->words.capacity() * 4 + storage->types.capacity() +
                      storage->strings.capacity() * sizeof(QString) +
                      storage->extras.capacity() * sizeof(Extra) +
                      storage->packed.capacity() * sizeof(PackedTable) + original.size();
    for (const auto &table : storage->packed)
        bytes += table.words.capacity() * sizeof(quint32);
    for (const auto &s : storage->strings)
        bytes += s.size() * 2;
    for (const auto &e : storage->extras)
        bytes += (e.name.size() + e.label.size()) * 2 + e.opaque.size() + e.tail.size();
    return bytes;
}
size_t Document::blockCount() const { return storage->records.size(); }
size_t Document::scalarCount() const {
    size_t count = storage->words.size();
    for (const auto &table : storage->packed)
        count += table.words.size();
    return count;
}
size_t Document::recordBytes() { return sizeof(Record); }
} // namespace SFileDetail
