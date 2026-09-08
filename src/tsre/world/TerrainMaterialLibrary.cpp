#include "TerrainMaterialLibrary.h"
#include <tsre/Game.h>
#include <tsre/texture/AceLib.h>
#include <tsre/texture/DdsLib.h>
#include <tsre/texture/Texture.h>
#include <QImage>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QSaveFile>
#include <QTextStream>
#include <QStringConverter>
#include <QDateTime>
#include <QRegularExpression>
#include <QSet>
#include <limits>

namespace {
QString quote(QString text) {
    return '"'+text.replace('\\',"\\\\").replace('"',"\\\"")+ '"';
}
// Small strict MSTS-style block reader. No dependence on permissive binary parsers.
struct Tokens {
    QStringList values;
    int at=0;
    bool ok=true;
    QString take() { if (at>=values.size()) { ok=false; return {}; } return values[at++]; }
    bool expect(const QString &value) { if (take().compare(value,Qt::CaseInsensitive)) ok=false; return ok; }
    quint64 number() { bool valid=false; auto value=take().toULongLong(&valid); ok &= valid; return value; }
};
Tokens tokenize(const QString &text) {
    Tokens out;
    for (int i=0;i<text.size();) {
        if (text[i].isSpace() || text[i]==QChar(0xfeff)) { ++i; continue; }
        if (text[i]=='(' || text[i]==')') { out.values.push_back(text.mid(i++,1)); continue; }
        if (text[i]=='"') {
            QString value; bool closed=false; ++i;
            while (i<text.size()) {
                QChar c=text[i++];
                if (c=='"') { closed=true; break; }
                if (c=='\\' && i<text.size() && (text[i]=='\\' || text[i]=='"')) c=text[i++];
                value+=c;
            }
            out.ok &= closed; out.values.push_back(value);
        } else {
            const int start=i;
            while (i<text.size() && !text[i].isSpace() && text[i]!='(' && text[i]!=')') ++i;
            out.values.push_back(text.mid(start,i-start));
        }
    }
    return out;
}
}
TerrainMaterialLibrary::TerrainMaterialLibrary(QString routeDirectory) : directory(QDir::cleanPath(routeDirectory)) { reload(); }
std::shared_ptr<TerrainMaterialLibrary> TerrainMaterialLibrary::current() {
    static std::shared_ptr<TerrainMaterialLibrary> library;
    const QString directory=QDir::cleanPath(Game::root+"/routes/"+Game::route);
    if (!library || library->directory!=directory) library=std::make_shared<TerrainMaterialLibrary>(directory);
    return library;
}
QString TerrainMaterialLibrary::path() const { return QDir(directory).filePath(FileName); }
QString TerrainMaterialLibrary::textureDirectory() const { return QDir(directory).filePath("terrtex"); }
bool TerrainMaterialLibrary::validTextureName(const QString &name) {
    if (name.isEmpty() || QDir::isAbsolutePath(name) || name.contains(':') || name.contains('\\')
            || QDir::cleanPath(name)!=name || name.split('/').contains("..")) return false;
    for (const auto &part : name.split('/')) if (part.isEmpty() || part.trimmed()!=part || part.endsWith('.')) return false;
    return true;
}
const TerrainMaterialDefinition *TerrainMaterialLibrary::find(quint32 uid) const {
    if (!loadError.isEmpty()) return nullptr;
    auto found=definitions.constFind(uid);
    return found==definitions.constEnd()?nullptr:&found.value();
}
void TerrainMaterialLibrary::poll() {
    const auto now=QDateTime::currentMSecsSinceEpoch();
    if (now<nextPoll) return;
    nextPoll=now+1000;
    QFileInfo info(path());
    if ((info.exists()?info.lastModified().toMSecsSinceEpoch():-1)!=stamp
            || (info.exists()?info.size():-1)!=fileSize) reload();
}
bool TerrainMaterialLibrary::reload() {
    QFileInfo info(path());
    stamp=info.exists()?info.lastModified().toMSecsSinceEpoch():-1;
    fileSize=info.exists()?info.size():-1;
    loadError.clear(); ++generation;
    if (!info.exists()) {
        if (nextUid>1) { loaded=false; loadError="Previously loaded material library is missing; restore it before adding materials"; return false; }
        definitions.clear(); loaded=true; return true;
    }
    QFile file(path());
    auto fail=[&](const QString &why) { loadError=path()+": "+why; loaded=false; return false; };
    if (!file.open(QIODevice::ReadOnly) || file.size()>16*1024*1024) return fail("Cannot read material library");
    const QByteArray bytes=file.readAll();
    if (bytes.size()<2 || bytes.size()%2 || (!bytes.startsWith("\xff\xfe") && !bytes.startsWith("\xfe\xff")))
        return fail("Expected UTF-16 text with BOM");
    QStringDecoder decode(QStringDecoder::Utf16);
    QString text=decode(bytes);
    if (decode.hasError()) return fail("Invalid UTF-16 text");
    if (text.startsWith("SIMISA")) text=text.mid(text.indexOf('\n')+1);
    Tokens t=tokenize(text);
    t.expect("TSRE_Terrain_Materials"); t.expect("(");
    QMap<quint32,TerrainMaterialDefinition> parsed;
    quint64 next=0; bool versionSeen=false, nextSeen=false;
    while (t.ok && t.at<t.values.size() && t.values[t.at]!=")") {
        const QString block=t.take().toLower(); t.expect("(");
        if (block=="version") { if (versionSeen || t.number()!=1) t.ok=false; versionSeen=true; }
        else if (block=="nextuid") { if (nextSeen) t.ok=false; next=t.number(); nextSeen=true; }
        else if (block=="material") {
            TerrainMaterialDefinition definition; QSet<QString> fields;
            while (t.ok && t.at<t.values.size() && t.values[t.at]!=")") {
                const QString field=t.take().toLower(); t.expect("(");
                if (fields.contains(field)) t.ok=false;
                fields.insert(field);
                if (field=="uid") { auto uid=t.number(); if (!uid || uid>UINT32_MAX) t.ok=false; definition.uid=quint32(uid); }
                else if (field=="name") definition.displayName=t.take();
                else if (field=="texture") definition.texture=t.take();
                else t.ok=false;
                t.expect(")");
            }
            if (!definition.uid || definition.displayName.isEmpty() || !validTextureName(definition.texture)
                    || parsed.contains(definition.uid)) t.ok=false;
            parsed.insert(definition.uid,definition);
        } else t.ok=false;
        t.expect(")");
    }
    t.expect(")");
    if (!t.ok || t.at!=t.values.size() || !versionSeen || !nextSeen || !next || next>quint64(UINT32_MAX)+1
            || (!parsed.isEmpty() && next<=parsed.lastKey())) return fail("Invalid blocks, duplicate UiD or invalid NextUiD");
    definitions=std::move(parsed); nextUid=next; loaded=true; return true;
}
bool TerrainMaterialLibrary::save(QString &error) {
    if (!Game::writeEnabled || Game::serverClient) { error="Route is not writable"; return false; }
    if (!loaded || !loadError.isEmpty()) { error=loadError; return false; }
    QFileInfo current(path());
    if ((current.exists()?current.lastModified().toMSecsSinceEpoch():-1)!=stamp
            || (current.exists()?current.size():-1)!=fileSize) { error="Material library changed on disk; reopen Choose before editing"; return false; }
    QSaveFile file(path());
    if (!file.open(QIODevice::WriteOnly)) { error=file.errorString(); return false; }
    QTextStream out(&file); out.setEncoding(QStringConverter::Utf16LE); out.setGenerateByteOrderMark(true);
    out << "SIMISA@@@@@@@@@@JINX0t1t______\n\nTSRE_Terrain_Materials (\n    Version ( 1 )\n    NextUiD ( " << nextUid << " )\n";
    for (const auto &m : definitions)
        out << "    Material (\n        UiD ( " << m.uid << " )\n        Name ( " << quote(m.displayName)
            << " )\n        Texture ( " << quote(m.texture) << " )\n    )\n";
    out << ")\n"; out.flush();
    if (out.status()!=QTextStream::Ok || !file.commit()) { error=file.errorString(); return false; }
    stamp=QFileInfo(path()).lastModified().toMSecsSinceEpoch(); fileSize=QFileInfo(path()).size(); ++generation;
    return true;
}
bool TerrainMaterialLibrary::rename(quint32 uid, const QString &name, QString &error) {
    error.clear();
    if (!Game::writeEnabled || Game::serverClient) { error="Route is not writable"; return false; }
    if (!loaded || !loadError.isEmpty()) { error=loadError; return false; }
    auto material=definitions.find(uid);
    if (material==definitions.end()) { error="Material UiD no longer exists"; return false; }
    const QString trimmed=name.trimmed();
    if (trimmed.isEmpty()) { error="Material name cannot be empty"; return false; }
    if (material->displayName==trimmed) return true;
    const QString previous=material->displayName;
    material->displayName=trimmed;
    if (save(error)) return true;
    material->displayName=previous;
    return false;
}
quint32 TerrainMaterialLibrary::addImage(const QString &source, QString &error) {
    if (!Game::writeEnabled || Game::serverClient) { error="Route is not writable"; return 0; }
    if (!loaded || !loadError.isEmpty()) { error=loadError; return 0; }
    if (nextUid>UINT32_MAX) { error="Material UiD range exhausted"; return 0; }
    QFileInfo image(source);
    if (!image.isFile() || !validTextureName(image.fileName())) { error="Invalid source image path"; return 0; }
    bool valid=false;
    if (source.endsWith(".ace",Qt::CaseInsensitive) || source.endsWith(".dds",Qt::CaseInsensitive)) {
        Texture texture(source);
        if (source.endsWith(".ace",Qt::CaseInsensitive)) {
            AceLoadOptions options; options.cpuPixels=true; options.stageMipmaps=false;
            valid=AceLib::load(source,texture,options,error);
        } else { DdsLib reader; reader.texture=&texture; reader.run(); valid=texture.loaded; }
        delete[] texture.imageData; texture.imageData=nullptr;
    } else valid=!QImage(source).isNull();
    if (!valid) { error="Cannot decode material source: "+source+"\n"+error; return 0; }
    if (!QDir().mkpath(textureDirectory())) { error="Cannot create route TERRTEX directory"; return 0; }
    QString relative=image.fileName(), target=QDir(textureDirectory()).filePath(relative);
    bool copied=false;
    if (QFileInfo(target).canonicalFilePath()!=image.canonicalFilePath()) {
        int suffix=1;
        while (QFileInfo::exists(target)) {
            relative=image.completeBaseName()+"_"+QString::number(suffix++)+"."+image.suffix();
            target=QDir(textureDirectory()).filePath(relative);
        }
        if (!QFile::copy(source,target)) { error="Cannot copy source image into route TERRTEX"; return 0; }
        copied=true;
    }
    TerrainMaterialDefinition definition{quint32(nextUid),image.completeBaseName(),relative};
    definitions.insert(definition.uid,definition); ++nextUid;
    if (!save(error)) {
        definitions.remove(definition.uid); --nextUid;
        if (copied && !QFile::remove(target)) error+="\nUnused copied source remains: "+target;
        return 0;
    }
    return definition.uid;
}
