#pragma once

#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QString>
#include <filesystem>

// Filesystem spelling and logical asset identity are deliberately separate.
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
inline bool sameLocation(const QString &a,const QString &b) {
    std::error_code error;
#ifdef Q_OS_WIN
    const bool same=std::filesystem::equivalent(
            std::filesystem::path(normalize(a).toStdWString()),
            std::filesystem::path(normalize(b).toStdWString()),error);
#else
    const bool same=std::filesystem::equivalent(
            std::filesystem::path(normalize(a).toStdString()),
            std::filesystem::path(normalize(b).toStdString()),error);
#endif
    return !error && same;
}
inline bool canReuse(const QString &requested,const QString &stored) {
    if(key(requested)!=key(stored))return false;
    if(synthetic(requested))return requested==stored;
    const QFileInfo request(normalize(requested)),cached(normalize(stored));
    // A folded cache hit must not repair a missing spelling or hide a competing file.
    if(!request.isFile() || !request.isReadable() || !cached.isFile())return false;
    if(sameLocation(request.filePath(),cached.filePath()))return true;
    qWarning()<<"Conflicting content paths for one logical asset"<<requested<<stored;
    return false;
}
inline QString withExtension(const QString &path,const QString &extension) {
    QString result=normalize(path);
    const int dot=result.lastIndexOf('.');
    if(dot>result.lastIndexOf('/'))result.truncate(dot);
    return result+"."+extension;
}
inline QString textureSource(const QString &path) {
    const QString source=normalize(path);
    if(source.endsWith(".ace",Qt::CaseInsensitive) && !QFileInfo(source).isFile())
        return withExtension(source,"dds");
    return source;
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
