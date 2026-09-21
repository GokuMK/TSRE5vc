#include "ContentCaseDocument.h"
#include <tsre/fileFunctions/SimisTextReader.h>
#include <tsre/fileFunctions/FileBuffer.h>
#include <tsre/fileFunctions/TS.h>
#include <QSet>
#include <QtEndian>
#include <QStringDecoder>
#include <QMap>
#include <algorithm>
#include <memory>
#define MINIZ_HEADER_FILE_ONLY
#include <mzip/miniz/miniz.h>

namespace ContentCase {
namespace {
constexpr int MaxDocument = 256 * 1024 * 1024;
constexpr int MaxDepth = 128;
using Reader = SimisTextReader;
using Kind = Reader::Kind;
QString copy(const QString &s) { return QString(s.constData(), s.size()); }
bool interesting(const QString &name) {
    static const QSet<QString> names = {
        "include", "filename", "filenames", "image", "shape", "wagonshape",
        "freightanim", "cabview", "sound", "file", "graphic", "texture",
        "treetexture", "terrain_texslot", "enginedata", "wagondata", "train_config",
        "pathid", "player_service_definition", "service_definition", "traffic_definition",
        "esd_alternative_texture", "carspawneritem", "forest", "soundregion",
        "lighttex", "signalshape", "speedwarningsignshape", "speedpostsignshape",
        "speedresumesignshape", "milepostshape", "ortssoundfilename", "ortscranesound",
        "terrain_sample_ybuffer", "terrain_sample_ebuffer", "terrain_sample_nbuffer",
        "terrain_sample_fbuffer", "terrain_sample_cbuffer", "terrain_sample_dbuffer",
        "terrain_patchset_fbuffer", "terrain_shape", "tsreterrainmaterialbuffer", "world_water_terrain_patch_map"
    };
    return names.contains(name);
}
bool ignored(const QString &name) {
    return name == "comment" || name == "skip" || name == "_skip" || name == "_info";
}
void retain(Document &doc, Field &&f) {
    if (f.values.isEmpty()) return;
    f.index = doc.fields.size();
    if (interesting(f.name)) { doc.fields.push_back(std::move(f)); return; }
    for (const auto &s : f.values) if (looksLikeResource(s.text)) {
        doc.fields.push_back(std::move(f)); return;
    }
}
void warning(Document &doc,const QString &message) {
    doc.syntaxWarning=true;
    if(doc.diagnostics.size()<50 && !doc.diagnostics.contains(message))doc.diagnostics<<message;
}
void discoveryFailure(Document &doc,const QString &message) {
    if(!doc.discoveryFailures.contains(message))doc.discoveryFailures<<message;
    warning(doc,message);
}
bool skipNonReferenceBlock(Reader &reader,Document &doc,const QString &region,bool imagesKnown=false) {
    QString detail;
    if(region.startsWith("shape/")) {
        if(reader.skipBlock())return true;
        detail=reader.diagnostics().isEmpty()?"EOF before closing delimiter":reader.diagnostics().last().message;
    } else {
        // Use the lexer's token boundaries for annotations/comments. A quote
        // inside an unquoted atom (e.g. full\") is not a string opener. The
        // generic fast skip treats it as one and can swallow later filenames.
        int nesting=1;
        for(;;) {
            const auto token=reader.next();
            if(token.kind==Kind::Error){detail=token.text;break;}
            if(token.kind==Kind::End){detail="EOF before closing delimiter";break;}
            if(token.kind==Kind::Open && ++nesting>256){detail="Nesting limit exceeded";break;}
            if(token.kind==Kind::Close && --nesting==0)return true;
        }
    }
    const QString message=QString("%1 at UTF-16 offset %2 while skipping %3").arg(detail).arg(reader.position()).arg(region);
    // The S schema's image table is authoritative for its texture names. A
    // rendering-geometry failure after that complete table need not lose them.
    if(imagesKnown)warning(doc,message+"; complete shape image table retained");
    else discoveryFailure(doc,message+"; reference scan could not finish");
    return false;
}
bool textBlock(Reader &reader, const QString &name, QStringList &parents,
               Document &doc, quint64 &ordinal, int depth, const QString &family) {
    if (depth > MaxDepth) { discoveryFailure(doc,"Text nesting limit exceeded"); return false; }
    if (ignored(name)) return skipNonReferenceBlock(reader,doc,name);
    // Shape geometry cannot contain texture paths; images and SD metadata can.
    // Skip numeric geometry without building a rendering/document tree.
    if (family == "s" && parents == QStringList{"shape"} && name != "images")
        return skipNonReferenceBlock(reader,doc,"shape/"+name,doc.shapeImagesComplete);
    Field f; f.name = name; f.parents = parents;
    f.location = (parents + QStringList{name}).join('/') + "#" + QString::number(++ordinal);
    for (;;) {
        auto t = reader.next();
        if (t.kind == Kind::Close) {
            if(family=="s" && name=="images" && parents==QStringList{"shape"})doc.shapeImagesComplete=true;
            retain(doc, std::move(f)); return true;
        }
        if(t.kind==Kind::Open) {
            const auto first=reader.peek();
            if(first.kind==Kind::Atom && (first.text.startsWith("#_")||first.text.startsWith("_#"))) {
                warning(doc,QString("Anonymous legacy annotation at UTF-16 offset %1; source references frozen").arg(t.begin));
                if(!skipNonReferenceBlock(reader,doc,"anonymous legacy annotation"))return false;
                continue;
            }
        }
        if(t.kind==Kind::End) {
            // Completed child fields remain usable when only enclosing record
            // delimiters are absent. An unfinished filename field is different.
            const bool wrapper=(parents.isEmpty() && (name=="shape"||name=="tr_routefile")) || !interesting(name);
            bool resourceScalar=false;for(const auto &v:f.values)resourceScalar|=looksLikeResource(v.text);
            if(wrapper && !resourceScalar) {
                warning(doc,QString("Missing closing delimiter for %1 at EOF; completed child references retained").arg(f.location));
                return true;
            }
            discoveryFailure(doc,QString("Unfinished reference field %1 at EOF").arg(f.location));
            return false;
        }
        if (t.kind == Kind::Error || t.kind == Kind::Open) {
            const QString detail=t.kind==Kind::Error?t.text:"Unexpected '(' without a field keyword";
            discoveryFailure(doc,QString("%1 in %2 at UTF-16 offset %3; remaining references unread").arg(detail,f.location).arg(t.begin));
            return false;
        }
        if (reader.peekKind() == Kind::Open) {
            reader.next();
            parents.push_back(name);
            const bool ok = textBlock(reader, copy(t.text).toLower(), parents, doc, ordinal, depth+1, family);
            parents.removeLast();
            if (!ok) return false;
        } else if (f.values.size() < 128) {
            f.values.push_back({copy(t.text), t.begin, t.end});
        } else if (interesting(name)) {
            discoveryFailure(doc,"Reference field scalar limit exceeded: " + f.location);
            return false;
        }
    }
}
QString tokenName(TS::TokenId id) {
    const auto it = TS::IdName.find(id);
    return it == TS::IdName.end() ? QString() : QString::fromLatin1(it->second).toLower();
}
void stringValue(FileBuffer &data, Field &f) {
    const int begin = data.off;
    const QString value = data.readString();
    f.values.push_back({value, begin, data.off});
}
void binaryBlock(FileBuffer &data, Document &doc, QStringList &parents, int depth,
                 const QString &family, QVector<int> lengths = {}) {
    if (depth > MaxDepth) throw FileBuffer::ParseError("Binary nesting limit exceeded");
    const int start = data.off;
    const auto block = data.readBlock();
    FileBuffer::ScopedLimit limit(data, block.end);
    const QString name = tokenName(block.id);
    if (name.isEmpty()) {
        discoveryFailure(doc,QString("Unclassified binary token %1 at %2; reference coverage unknown").arg(block.id).arg(start));
        data.off = block.end; return;
    }
    data.skipLabel();
    Field f; f.name=name; f.parents=parents;
    lengths.push_back(start+4);f.binaryLengthOffsets=lengths;
    f.location=(parents+QStringList{name}).join('/')+"@"+QString::number(start);
    static const QSet<QString> worldObjects = {"static", "trackobj", "forest", "collideobject",
        "signal", "platform", "siding", "levelcr", "speedpost", "hazard", "soundsource",
        "soundregion", "carspawner", "pickup", "transfer", "gantry", "dyntrack", "telepole"};
    bool children = false;
    bool counted = false;
    int count = -1;
    if (family == "s") {
        children = name == "shape" || name == "images";
        if (name == "images") { counted = true; count = data.getInt(); }
        if (name == "image" && !parents.isEmpty() && parents.last() == "images") stringValue(data, f);
        if (depth == 0 && name != "shape") throw FileBuffer::ParseError("Expected binary shape root");
    } else if (family == "sd") {
        if (depth == 0) {
            if (name != "shape") throw FileBuffer::ParseError("Expected binary shape descriptor root");
            stringValue(data, f);
            children = true;
        } else {
            // These SD fields contain metadata, not resource names. Keep
            // unknown extensions uncertified instead of silently skipping them.
            static const QSet<QString> metadata = {"esd_detail_level", "esd_alternative_texture",
                "esd_bounding_box", "esd_complex_box", "esd_complex", "esd_snapable",
                "esd_no_visual_obstruction"};
            children = name == "esd_complex";
            if (!metadata.contains(name))
                discoveryFailure(doc, "Unclassified binary SD field: " + name);
        }
    } else if (family == "w" || family == "ws") {
        if (depth == 0 && name != (family == "w" ? "tr_worldfile" : "tr_worldsoundfile"))
            throw FileBuffer::ParseError("Unexpected binary world root");
        children = depth == 0 || (depth == 1 && worldObjects.contains(name));
        if (depth == 1 && !worldObjects.contains(name) && name != "viewdbsphere" &&
            name != "vdbidcount" && name != "tr_watermark")
            discoveryFailure(doc,"Unclassified binary world object: " + name);
        if (depth == 2 && (name == "filename" || name == "treetexture" || name == "speed_digit_tex" || name == "sound" ||
                           name == "ortssoundfilename" || name == "ortscranesound")) stringValue(data, f);
        if (name == "filenames") discoveryFailure(doc,"Binary FileNames array is not yet classified");
    } else if (family == "t") {
        if (depth == 0 && name != "terrain") throw FileBuffer::ParseError("Expected binary terrain root");
        children = name == "terrain" || name == "terrain_samples" || name == "terrain_shaders" ||
                   name == "terrain_shader" || name == "terrain_texslots" || name == "terrain_patches" ||
                   name == "terrain_patchsets" || name == "terrain_patchset" ||
                   name == "terrain_transfers" || name == "terrain_shapes";
        if (name == "terrain_shaders" || name == "terrain_texslots" || name == "terrain_patchsets" ||
            name == "terrain_transfers" || name == "terrain_shapes") { counted = true; count = data.getInt(); }
        if (name == "terrain_transfer") {
            parents.push_back(name);
            binaryBlock(data,doc,parents,depth+1,family,lengths); // Shader, then four bounds floats.
            parents.removeLast();
            for(int i=0;i<4;++i)data.getFloat();
        }
        if (name == "terrain_shader") data.readString(); // shader label, not a resource
        if (name == "terrain_texslot" || name == "terrain_sample_ybuffer" ||
            name == "terrain_sample_ebuffer" || name == "terrain_sample_nbuffer" ||
            name == "terrain_sample_fbuffer" || name == "terrain_sample_cbuffer" ||
            name == "terrain_sample_dbuffer" || name == "terrain_patchset_fbuffer" ||
            name == "terrain_shape" || name == "tsreterrainmaterialbuffer") stringValue(data, f);
        if (name == "tsreterrainmaterialmap") {
            const quint32 entries=quint32(data.getInt());
            if(entries>256 || quint64(entries)*8!=quint64(block.end-data.off))
                throw FileBuffer::ParseError("Invalid terrain material UID map bounds");
            // Numeric slot/UID pairs, not paths.
            data.off=block.end;
        } else if(name == "tsreterrainbakedmaterials") {
            if(data.getInt()!=2)throw FileBuffer::ParseError("Unsupported terrain bake metadata version");
            data.getInt();data.getInt(); // 64-bit content revision.
            children=true;
        } else if(name == "tsreterrainbakedmaterial") {
            // TFileBakeMetadata v2: variant, revision, resolution, three hashes.
            // Legacy standalone form is also metadata, not a filename.
            data.readString();
            if(parents.contains("tsreterrainbakedmaterials")) {
                data.getInt();data.getInt();data.getInt();
                for(int i=0;i<3;++i)data.readString();
            }
            if(data.off!=block.end)throw FileBuffer::ParseError("Unexpected terrain bake metadata payload");
        } else if (name.startsWith("tsre") && name != "tsreterrainmaterialbuffer")
            discoveryFailure(doc,"Terrain extension requires additional reference coverage: " + name);
    }
    if (!f.values.isEmpty()) retain(doc, std::move(f));
    if (children) {
        if ((counted && count < 0) || count > (block.end-data.off)/9)
            throw FileBuffer::ParseError("Invalid binary child count");
        parents.push_back(name);
        int seen = 0;
        while (data.off < block.end) { binaryBlock(data,doc,parents,depth+1,family,lengths); ++seen; }
        parents.removeLast();
        if (count >= 0 && seen != count) throw FileBuffer::ParseError("Binary child count mismatch");
        if(family=="s" && name=="images")doc.shapeImagesComplete=true;
    }
    data.off=block.end;
}
QByteArray utf16(const QString &text) {
    QByteArray bytes(text.size()*2, Qt::Uninitialized);
    for (qsizetype i=0;i<text.size();++i) qToLittleEndian<quint16>(text[i].unicode(),bytes.data()+2*i);
    return bytes;
}
bool unwrap(QByteArray &bytes, Document &doc) {
    const bool wide = bytes.startsWith(QByteArray("\xff\xfe",2)) && bytes.mid(2,16)==utf16("SIMISA@F");
    if (!wide && !bytes.startsWith("SIMISA@F")) return true;
    doc.compressed=true;
    const int header=wide ? 34 : 16;
    if (bytes.size()<header) return false;
    const quint32 expected=qFromLittleEndian<quint32>(bytes.constData()+(wide?18:8));
    if (!expected || expected>MaxDocument) return false;
    QByteArray payload(expected,Qt::Uninitialized);
    mz_ulong length=expected;
    const int status=mz_uncompress(reinterpret_cast<unsigned char*>(payload.data()),&length,
        reinterpret_cast<const unsigned char*>(bytes.constData()+header),bytes.size()-header);
    if (status!=MZ_OK || length!=expected) return false;
    bytes=(wide ? QByteArray("\xff\xfe",2)+utf16("SIMISA@@@@@@@@@@") : QByteArray("SIMISA@@@@@@@@@@"))+payload;
    return true;
}
}
bool looksLikeResource(const QString &value) {
    const QString v=value.toLower();
    static const QStringList extensions={".s",".sd",".ace",".dds",".eng",".wag",".con",".srv",
        ".trf",".pat",".sms",".wav",".cvf",".env",".inc",".raw",".ref",".haz",".png",
        ".jpg",".bmp",".tga",".gltf",".glb",".pmap"};
    for (const auto &ext:extensions) if (v.endsWith(ext)) return true;
    return false;
}
Document inspectDocument(const QByteArray &input, const QString &family) {
    Document doc;
    if (input.size()>MaxDocument) { doc.diagnostics << "Document exceeds 256 MiB scan limit"; return doc; }
    QByteArray bytes=input;
    if (!unwrap(bytes,doc)) { doc.diagnostics << "Invalid or oversized compressed SIMIS envelope"; return doc; }
    doc.binary=bytes.size()>=32 && bytes.mid(16,4)=="JINX" && bytes[23]=='b';
    if (doc.binary) {
        doc.decoded=true;
        doc.encoding="SIMIS binary"; doc.offsets="decompressed-envelope-byte";
        if (family!="s" && family!="sd" && family!="w" && family!="ws" && family!="t") {
            doc.diagnostics << "Binary schema not implemented for " + family; return doc;
        }
        if (family == "sd" && bytes[21] != 't') {
            discoveryFailure(doc, "Expected an SD descriptor (JINX0t); this .sd file contains a different binary document kind (JINX0" + QString(QChar(bytes[21])) + ")");
            return doc;
        }
        auto storage=new unsigned char[bytes.size()];
        std::copy(bytes.cbegin(),bytes.cend(),storage);
        FileBuffer data(storage,int(bytes.size())); data.off=32;
        QStringList parents;
        try {
            binaryBlock(data,doc,parents,0,family);
            if (data.off!=bytes.size()) discoveryFailure(doc,"Trailing binary content not classified");
            doc.valid=!doc.syntaxWarning;doc.referenceScanComplete=doc.discoveryFailures.isEmpty();
        } catch (const FileBuffer::ParseError &e) { discoveryFailure(doc,QString::fromLatin1(e.what())); }
        return doc;
    }
    doc.encoding=bytes.startsWith(QByteArray("\xff\xfe",2)) ? "UTF-16LE" :
                 bytes.startsWith(QByteArray("\xfe\xff",2)) ? "UTF-16BE" : "UTF-8-or-legacy-byte-text";
    doc.offsets="decoded-UTF16-code-unit";
    QString text,error;
    if (!Reader::decode(bytes,text,error)) { doc.diagnostics<<error; return doc; }
    if (text.contains(QChar(0))) { doc.diagnostics<<"NUL in text; unsupported encoding or binary format"; return doc; }
    doc.decoded=true;
    Reader reader(text); quint64 ordinal=0; QStringList parents;
    bool any=false,unexpectedClose=false;int catalogCount=-1,topBlocks=0;
    for (;;) {
        const auto t=reader.next();
        if (t.kind==Kind::End) {
            doc.valid=(any || catalogCount==0) && !doc.syntaxWarning;
            if(catalogCount>=0 && topBlocks!=catalogCount)doc.diagnostics<<"Counted catalog entry count differs from declaration";
            break;
        }
        // Native forests/carspawn/ttype catalogs have a bare count and some
        // distributions retain a final ')' without a matching opening root.
        if(family=="catalog" && t.kind==Kind::Close && catalogCount>=0 && reader.peekKind()==Kind::End)continue;
        if(t.kind==Kind::Close) {
            warning(doc,QString("Unmatched top-level ')' at UTF-16 offset %1; continuing reference scan").arg(t.begin));
            unexpectedClose=true;continue;
        }
        if (t.kind==Kind::Error || t.kind==Kind::Open) {
            discoveryFailure(doc,QString("%1 at top-level UTF-16 offset %2; remaining references unread")
                .arg(t.kind==Kind::Error?t.text:"Unexpected '('").arg(t.begin));break;
        }
        if (reader.peekKind()==Kind::Open) {
            if(unexpectedClose)discoveryFailure(doc,"A document block follows an unmatched closing delimiter; reference context requires review");
            if(!ignored(t.text.toLower()))++topBlocks;
            reader.next(); any=true;
            if (!textBlock(reader,copy(t.text).toLower(),parents,doc,ordinal,0,family)) break;
        } else if (!t.text.startsWith("SIMISA",Qt::CaseInsensitive) && t.text!="EOF") {
            qint32 count=0;
            if(family=="catalog" && !any && catalogCount<0 && Reader::integer(t,count) && count>=0)catalogCount=count;
            else if(doc.diagnostics.size()<50)doc.diagnostics << QString("Unclassified top-level text at %1: %2").arg(t.begin).arg(t.text.left(80));
        }
    }
    if(!any && catalogCount!=0)discoveryFailure(doc,"No reference-bearing document blocks found");
    if(family=="s" && !doc.shapeImagesComplete)discoveryFailure(doc,"Shape image table was not completely read");
    doc.referenceScanComplete=(any || catalogCount==0) && doc.discoveryFailures.isEmpty();
    if(doc.syntaxWarning)doc.valid=false;
    return doc;
}
bool patchDocument(const QByteArray &input, const QString &family, const QVector<ReferenceEdit> &edits,
                   QByteArray &output, QString &error) {
    error.clear();output.clear();
    const auto source=inspectDocument(input,family);
    auto fail=[&](const QString &s){error=s;return false;};
    if(!source.valid || !source.referenceScanComplete)return fail("Source is not eligible for reference edits");
    QByteArray bytes=input;Document envelope;
    if(!unwrap(bytes,envelope))return fail("Cannot decompress source");
    struct Patch { int begin,end;QByteArray bytes; };
    QVector<Patch> patches;QMap<int,qint64> lengthChanges;
    QMap<QPair<int,int>,QString> changed;
    QString text;
    const bool le=bytes.startsWith(QByteArray("\xff\xfe",2)),be=bytes.startsWith(QByteArray("\xfe\xff",2));
    const bool utf8Bom=bytes.startsWith(QByteArray("\xef\xbb\xbf",3));
    bool latin=false;
    if(!source.binary) {
        if(!Reader::decode(bytes,text,error))return false;
        if(!le && !be){QStringDecoder decoder(QStringDecoder::Utf8);const QString decoded=decoder(bytes);Q_UNUSED(decoded);latin=decoder.hasError();}
    }
    auto encode=[&](const QString &s)->QByteArray {
        if(le)return utf16(s);
        if(be){auto b=utf16(s);for(int i=0;i<b.size();i+=2)std::swap(b[i],b[i+1]);return b;}
        return latin?s.toLatin1():s.toUtf8();
    };
    for(const auto &edit:edits) {
        if(edit.fieldIndex<0 || edit.fieldIndex>=source.fields.size())return fail("Reference field no longer exists");
        const auto &field=source.fields[edit.fieldIndex];
        if(edit.scalarIndex<0 || edit.scalarIndex>=field.values.size())return fail("Reference scalar no longer exists");
        const auto &value=field.values[edit.scalarIndex];
        if(value.text!=edit.expected)return fail("Reference text differs from plan");
        const auto key=qMakePair(edit.fieldIndex,edit.scalarIndex);
        if(changed.contains(key)) {
            if(changed.value(key)!=edit.replacement)return fail("Conflicting edits for one reference scalar");
            continue;
        }
        changed[key]=edit.replacement;
        if(value.text==edit.replacement)continue;
        if(source.binary) {
            if(edit.replacement.size()>65535 || field.binaryLengthOffsets.isEmpty())return fail("Unsupported binary string edit");
            auto replacement=utf16(edit.replacement);QByteArray count(2,Qt::Uninitialized);
            qToLittleEndian<quint16>(edit.replacement.size(),count.data());replacement.prepend(count);
            patches.push_back({int(value.begin),int(value.end),replacement});
            for(int at:field.binaryLengthOffsets)lengthChanges[at]+=replacement.size()-(value.end-value.begin);
        } else {
            if(value.begin<0 || value.end>text.size())return fail("Invalid text reference span");
            QString replacement=edit.replacement;
            if(latin && QString::fromLatin1(replacement.toLatin1())!=replacement)return fail("Filename is not representable in source byte encoding");
            // Retain the original separator convention where it is unambiguous.
            if(value.text.contains('\\') && !value.text.contains('/'))replacement.replace('/','\\');
            const auto lexeme=text.mid(value.begin,value.end-value.begin);
            bool quoted=lexeme.startsWith('"');
            for(auto c:replacement)quoted|=c.isSpace() || c=='(' || c==')' || c=='"';
            if(quoted)replacement='"'+replacement.replace('\\',"\\\\").replace('"',"\\\"").replace('\n',"\\n").replace('\t',"\\t")+'"';
            const int prefix=le||be?2:utf8Bom?3:0;
            const int begin=prefix+encode(text.left(value.begin)).size();
            const int end=prefix+encode(text.left(value.end)).size();
            patches.push_back({begin,end,encode(replacement)});
        }
    }
    for(auto it=lengthChanges.cbegin();it!=lengthChanges.cend();++it) {
        const qint64 length=qFromLittleEndian<quint32>(bytes.constData()+it.key())+it.value();
        if(length<1 || length>MaxDocument)return fail("Binary block length would be invalid");
        QByteArray word(4,Qt::Uninitialized);qToLittleEndian<quint32>(length,word.data());
        patches.push_back({it.key(),it.key()+4,word});
    }
    std::sort(patches.begin(),patches.end(),[](const Patch &a,const Patch &b){return a.begin>b.begin;});
    int bound=bytes.size();
    for(const auto &p:patches) {
        if(p.begin<0 || p.end<p.begin || p.end>bound)return fail("Overlapping or invalid reference patches");
        bytes.replace(p.begin,p.end-p.begin,p.bytes);bound=p.begin;
    }
    output=bytes;
    if(envelope.compressed) {
        const bool wide=input.startsWith(QByteArray("\xff\xfe",2));
        const int header=wide?34:16;
        const auto payload=bytes.mid(header);
        output=input.left(header);
        qToLittleEndian<quint32>(payload.size(),output.data()+(wide?18:8));
        output+=qCompress(payload).mid(4);
    }
    const auto result=inspectDocument(output,family);
    if(!result.valid || !result.referenceScanComplete || result.fields.size()!=source.fields.size())return fail("Patched document failed reference verification");
    for(int f=0;f<source.fields.size();++f) {
        const auto &before=source.fields[f],&after=result.fields[f];
        if(before.name!=after.name || before.parents!=after.parents || before.values.size()!=after.values.size())return fail("Patched document structure changed: "+before.name+" ["+before.parents.join('/')+"] -> "+after.name+" ["+after.parents.join('/')+"]");
        for(int v=0;v<before.values.size();++v) {
            QString expected=changed.value(qMakePair(f,v),before.values[v].text),actual=after.values[v].text;
            if(changed.contains(qMakePair(f,v))){expected.replace('\\','/');actual.replace('\\','/');}
            if(actual!=expected)return fail("Patched document changed an unexpected scalar");
        }
    }
    return true;
}

}
