#include "TFile.h"
#include <tsre/fileFunctions/ContentPath.h>
#include <QFile>
#include <QSaveFile>
#include <QDebug>
#include <QFileInfo>
#include <memory>

bool TFile::patchFlagsWritable() const {
    for(size_t i=0;i<patchSets.size();++i)if(patchSets[i].flagsBuffer
        &&(i>=patchFlagsResources.size()||!patchFlagsResources[i].valid))return false;
    return true;
}
bool TFile::loadPatchFlags(const QString &directory,QString &error) {
    error.clear();
    if(patchFlagsResources.size()!=patchSets.size())patchFlagsResources.resize(patchSets.size());
    for(size_t i=0;i<patchSets.size();++i) {
        auto &set=patchSets[i];if(!set.flagsBuffer)continue;
        auto &resource=patchFlagsResources[i];
        if(resource.valid)continue;
        if(resource.inlineFlags.empty())for(const auto &p:set.patches)resource.inlineFlags.push_back(p.flags);
        resource.path=ContentPath::join(directory,*set.flagsBuffer);
        QFile file(resource.path);
        if(!file.open(QIODevice::ReadOnly)||file.size()!=qint64(set.patches.size())) {
            error+="Missing/invalid patch flag sidecar: "+resource.path+"; ";continue;
        }
        const auto bytes=file.readAll();
        if(bytes.size()!=qint64(set.patches.size())||file.error()!=QFile::NoError) {
            error+="Cannot read patch flag sidecar: "+resource.path+"; ";continue;
        }
        resource.original=bytes;resource.valid=true;
        for(size_t p=0;p<set.patches.size();++p)set.patches[p].flags=quint8(bytes[int(p)]);
    }
    return error.isEmpty();
}
bool TFile::save(QString path) {
    TerrainFile::Data prepared;QString error;
    if(!prepare(prepared,error)){qWarning()<<error;return false;}
    const auto bytes=prepared.encode(error);if(bytes.isEmpty()){qWarning()<<error;return false;}
    struct Change {size_t set;QString path;QByteArray before,after;std::unique_ptr<QSaveFile> file;};
    std::vector<Change> changes;
    QMap<QString,QByteArray> sidecarValues;
    for(size_t i=0;i<patchFlagsResources.size();++i) {
        const auto &r=patchFlagsResources[i];if(!r.valid)continue;
        auto after=r.original;
        for(size_t p=0;p<patchSets[i].patches.size();++p) {
            const auto flags=patchSets[i].patches[p].flags;
            if(flags!=quint8(r.original[int(p)]))after[int(p)]=char(flags&0xcb);
        }
        const auto identity=QFileInfo(r.path).absoluteFilePath();
        if(sidecarValues.contains(identity)) {
            if(sidecarValues.value(identity)!=after) {
                qWarning()<<"Conflicting patch sets share a flag sidecar; save refused"<<r.path;return false;
            }
            continue;
        }
        sidecarValues.insert(identity,after);
        if(after==r.original)continue;
        QFile before(r.path);
        if(!before.open(QIODevice::ReadOnly)||before.readAll()!=r.original) {
            qWarning()<<"Patch flag sidecar changed externally; save refused"<<r.path;return false;
        }
        auto file=std::make_unique<QSaveFile>(r.path);
        if(!file->open(QIODevice::WriteOnly)||file->write(after)!=after.size())return false;
        changes.push_back({i,r.path,r.original,after,std::move(file)});
    }
    QSaveFile descriptor(ContentPath::normalize(path));
    if(!descriptor.open(QIODevice::WriteOnly)||descriptor.write(bytes)!=bytes.size())return false;
    size_t committed=0;
    auto rollback=[&] {
        for(size_t i=0;i<committed;++i) {
            QSaveFile file(changes[i].path);
            if(!file.open(QIODevice::WriteOnly)||file.write(changes[i].before)!=changes[i].before.size()||!file.commit())
                qCritical()<<"Could not restore patch flag sidecar after failed terrain save"<<changes[i].path;
        }
    };
    for(auto &change:changes) {
        if(!change.file->commit()){rollback();return false;}
        ++committed;
    }
    if(!descriptor.commit()){rollback();return false;}
    for(auto &resource:patchFlagsResources)if(resource.valid)
        resource.original=sidecarValues.value(QFileInfo(resource.path).absoluteFilePath(),resource.original);
    for(size_t i=0;i<patchFlagsResources.size();++i)if(patchFlagsResources[i].valid) {
        auto &original=patchFlagsResources[i].inlineFlags;
        for(size_t p=0;p<original.size();++p)original[p]=prepared.patchSets[i].patches[p].flags;
    }
    return true;
}
