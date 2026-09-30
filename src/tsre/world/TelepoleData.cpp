/* This file is part of TSRE5. Licensed under GPL-3.0-or-later; see LICENSE.md. */
#include <tsre/world/TelepoleData.h>

#include <tsre/fileFunctions/ContentPath.h>
#include <tsre/fileFunctions/FileBuffer.h>
#include <tsre/fileFunctions/ParserX.h>
#include <tsre/fileFunctions/ReadFile.h>
#include <QFile>
#include <cmath>
#include <memory>

namespace {

QString loadedRoutePath;
TelepoleData loadedRouteData;

bool finitePositive(float value) {
    return std::isfinite(value) && value > 0.0f;
}

}

bool TelepoleData::load(const QString &sourcePath) {
    configList.clear();
    loadDiagnostics.clear();

    const QString path = ContentPath::normalize(sourcePath);
    QFile file(path);
    if(!file.open(QIODevice::ReadOnly)){
        loadDiagnostics.append("Unable to open " + path);
        return false;
    }
    std::unique_ptr<FileBuffer> data(ReadFile::read(&file));
    file.close();
    if(data == nullptr){
        loadDiagnostics.append("Unable to read " + path);
        return false;
    }

    data->toUtf16();
    ParserX::NextLine(data.get());
    const QString root = ParserX::NextTokenDomIgnore(data.get()).toLower();
    if(root != "tpoleconfigdata"){
        loadDiagnostics.append("Missing TPoleConfigData in " + path);
        return false;
    }

    const int declaredConfigs = std::max(
            0, (int)ParserX::GetNumber(data.get()));
    QString token;
    while(!((token = ParserX::NextTokenInside(data.get()).toLower()).isEmpty())){
        if(token != "tpoleconfig"){
            ParserX::SkipToken(data.get());
            continue;
        }

        Config entry;
        const int declaredWires = std::max(
                0, (int)ParserX::GetNumber(data.get()));
        QString property;
        while(!((property = ParserX::NextTokenInside(
                     data.get()).toLower()).isEmpty())){
            if(property == "filename"){
                entry.fileName = ParserX::GetString(data.get()).trimmed();
            } else if(property == "shadow"){
                entry.shadowName = ParserX::GetString(data.get()).trimmed();
            } else if(property == "separation"){
                entry.separation = ParserX::GetNumber(data.get());
            } else if(property == "wire"){
                entry.wires.append({
                    ParserX::GetNumber(data.get()),
                    ParserX::GetNumber(data.get()),
                    ParserX::GetNumber(data.get())
                });
            }
            ParserX::SkipToken(data.get());
        }
        ParserX::SkipToken(data.get());

        entry.valid = !entry.fileName.isEmpty()
                && finitePositive(entry.separation);
        if(!entry.valid)
            loadDiagnostics.append(QString(
                    "TPoleConfig %1 has no usable Filename/Separation")
                    .arg(configList.size()));
        if(declaredWires != entry.wires.size())
            loadDiagnostics.append(QString(
                    "TPoleConfig %1 declares %2 wires but contains %3")
                    .arg(configList.size()).arg(declaredWires)
                    .arg(entry.wires.size()));
        configList.append(entry);
    }
    ParserX::SkipToken(data.get());

    if(declaredConfigs != configList.size())
        loadDiagnostics.append(QString(
                "TPoleConfigData declares %1 configurations but contains %2")
                .arg(declaredConfigs).arg(configList.size()));
    return !configList.isEmpty();
}

int TelepoleData::configCount() const {
    return configList.size();
}

const TelepoleData::Config *TelepoleData::config(int index) const {
    if(index < 0 || index >= configList.size())
        return nullptr;
    return &configList[index];
}

const QVector<TelepoleData::Config> &TelepoleData::configs() const {
    return configList;
}

const QStringList &TelepoleData::diagnostics() const {
    return loadDiagnostics;
}

const TelepoleData &TelepoleData::routeData(
        const QString &routePath, bool forceReload) {
    const QString normalized = ContentPath::normalize(routePath);
    const QString key = ContentPath::key(normalized);
    if(forceReload || key != loadedRoutePath){
        loadedRoutePath = key;
        loadedRouteData.load(ContentPath::join(normalized, "telepole.dat"));
    }
    return loadedRouteData;
}
