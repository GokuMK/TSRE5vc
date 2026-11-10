#pragma once

#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QString>

// Preserve I/O spelling; runtime cache identity is case-insensitive.
// Compute key() once per request/asset and compare stored keys in lookup loops.
namespace ContentPath {
inline bool synthetic(const QString &path) {
    return path.startsWith("gltfimg:") || path.startsWith("terrain-proc:")
            || path.startsWith("base-level-bake:terrain-proc:")
            || path.endsWith(":painttex") || path.endsWith(":maptex");
}
inline QString normalize(QString path) {
    if(synthetic(path))return path;
    path.replace('\\','/');
    // Retain UNC's leading // and authored .. components for ordinary OS lookup.
    QString result;
    for(const auto c:path) {
        if(c=='/' && result.endsWith('/') && result!="/")continue;
        result+=c;
    }
    return result;
}
inline QString join(const QString &base,const QString &name) {
    return normalize(base+"/"+name);
}
inline QString parentDirectory(const QString &directory) {
    return QDir::cleanPath(QDir(normalize(directory)).absoluteFilePath(".."));
}
inline QString key(const QString &path) {
    if(synthetic(path))return path;
    return QDir::cleanPath(QDir::current().absoluteFilePath(normalize(path))).toLower();
}
inline bool readable(const QString &path) {
    const QFileInfo file(normalize(path));return file.isFile() && file.isReadable();
}
inline QString withExtension(const QString &path,const QString &extension) {
    QString result=normalize(path);
    const int dot=result.lastIndexOf('.');
    if(dot>result.lastIndexOf('/'))result.truncate(dot);
    return result+"."+extension;
}
// The file a texture name loads, as Open Rails picks it (SharedTextureManager):
// for an .ace or a .dds name its .dds when there is one, else its .ace, else
// the name as given. An .ace name's .dds is looked for in lowercase; a .dds
// name as spelt, its .ace only when it is not there.
inline QString textureSource(const QString &path) {
    const QString source=normalize(path);
    if(source.endsWith(".ace",Qt::CaseInsensitive)) {
        const QString dds=withExtension(source,"dds");
        return QFileInfo(dds).isFile() ? dds : source;
    }
    if(source.endsWith(".dds",Qt::CaseInsensitive) && !QFileInfo(source).isFile()) {
        const QString ace=withExtension(source,"ace");
        if(QFileInfo(ace).isFile())return ace;
    }
    return source;
}
// The name MSTS files give a texture (a .t's materials, a shape's images):
// an .ace for a .dds (MSTS reads no .dds; Open Rails finds the .dds by the
// .ace name), the name as given otherwise.
inline QString authoredTextureName(const QString &name) {
    return name.endsWith(".dds",Qt::CaseInsensitive) ? name.left(name.size()-4)+".ace" : name;
}
inline bool requiredDirectory(const QString &path) {
    if(QDir(normalize(path)).exists())return true;
    qWarning()<<"Missing required content directory"<<path
              <<"Check content naming with the content-case planner.";
    const QFileInfo wanted(normalize(path));
    for(const auto &entry:wanted.dir().entryList(QDir::Dirs|QDir::NoDotAndDotDot))
        if(entry.compare(wanted.fileName(),Qt::CaseInsensitive)==0)
            qWarning()<<"Directory has a different spelling:"<<wanted.dir().filePath(entry);
    return false;
}
}
