#include "TerrainSeason.h"
#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <QDebug>

QStringList TerrainSeason::variants() {
    return {"Base","Spring","Autumn","Winter","SpringRain","SummerRain",
            "AutumnRain","WinterRain","SpringSnow","SummerSnow","AutumnSnow","Snow"};
}
QString TerrainSeason::canonical(const QString &s) {
    if (s.isEmpty() || s.compare("Default",Qt::CaseInsensitive)==0
            || s.compare("Summer",Qt::CaseInsensitive)==0) return "Base";
    if (s.compare("WinterSnow",Qt::CaseInsensitive)==0) return "Snow";
    for (const auto &v:variants()) if (v.compare(s,Qt::CaseInsensitive)==0) return v;
    return {};
}
QString TerrainSeason::directory(const QString &root, const QString &variant) {
    if (variant=="Base") return QDir::cleanPath(root);
    const auto dirs=QDir(root).entryList(QDir::Dirs|QDir::NoDotAndDotDot);
    for (const auto &d:dirs) if (d.compare(variant,Qt::CaseInsensitive)==0) return QDir(root).filePath(d);
    return QDir(root).filePath(variant.toLower());
}
QStringList TerrainSeason::available(const QString &root) {
    QStringList result{"Base","Snow"};
    for (const auto &v:variants()) if (!result.contains(v) && QDir(directory(root,v)).exists()) result<<v;
    return result;
}
QString TerrainSeason::resolve(const QString &root, const QString &variant, const QString &file,
                              bool warn, bool ignoreSeasons) {
    QStringList search;
    const QString v=ignoreSeasons ? "Base" : canonical(variant);
    if (v.isEmpty()) return {};
    search<<v;
    if (v.endsWith("Rain") && v!="SummerRain") search<<v.left(v.size()-4);
    if (v.endsWith("Snow") && v!="Snow") search<<"Snow";
    if (!search.contains("Base")) search<<"Base";
    for (int index=0;index<search.size();++index) {
        const QDir dir(directory(root,search[index]));
        QStringList candidates{file};
        if (file.endsWith(".ace",Qt::CaseInsensitive)) candidates.prepend(file.left(file.size()-3)+"dds");
        for (const auto &candidate:candidates) {
            QString path=dir.filePath(candidate);
            if (!QFileInfo(path).isFile()) {
                // Catalogue filenames may contain subdirectories. Resolve each
                // component's case, not just the leaf, on case-sensitive hosts.
                QString resolved=dir.absolutePath();
                for (const auto &part:candidate.split('/')) {
                    const QDir parent(resolved);
                    QString spelling=part;
                    for (const auto &entry:parent.entryList(QDir::AllEntries|QDir::NoDotAndDotDot))
                        if (entry.compare(part,Qt::CaseInsensitive)==0) {spelling=entry;break;}
                    resolved=parent.filePath(spelling);
                }
                path=resolved;
            }
            if (!QFileInfo(path).isFile()) continue;
            if (warn && index>0) {
                static QSet<QString> warned;
                const QString key=QDir(root).absolutePath()+"/"+v+"/"+file;
                if (!warned.contains(key)) { warned.insert(key); qWarning()<<"Seasonal material fallback"<<key<<"->"<<path; }
            }
            return path;
        }
    }
    return {};
}
