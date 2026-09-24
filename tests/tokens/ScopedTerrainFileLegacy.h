#pragma once
#include "TerrainFileLegacy.h"
#include <QSet>

// Benchmark-only ownership wrapper for the frozen pre-migration reader.
class ScopedTerrainFileLegacy : public TerrainFileLegacy {
public:
    ~ScopedTerrainFileLegacy() override {
        QSet<QString*> strings{sampleFbuffer,sampleYbuffer,sampleEbuffer,sampleNbuffer};
        for(auto *table:{&materials,&amaterials})for(auto &entry:*table) {
            strings.insert(entry.second.name);
            strings.insert(entry.second.tex[0]);strings.insert(entry.second.tex[1]);
        }
        for(auto *s:strings)delete s;
        delete[] tdata;delete[] errorBias;delete[] flags;
        delete nsamples;delete errthresholdScale;delete alwaysselectMaxdist;
        delete sampleRotation;delete sampleSize;
    }
};
