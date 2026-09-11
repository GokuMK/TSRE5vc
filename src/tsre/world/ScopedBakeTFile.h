#pragma once
#include "TFile.h"
#include <QSet>

// Batch reads must not accumulate the legacy TFile destructor's allocations.
// Only use for independently parsed files: editor descriptors can share strings.
class ScopedBakeTFile : public TFile {
public:
    ~ScopedBakeTFile() override {
        QSet<QString*> strings{sampleFbuffer,sampleYbuffer,sampleEbuffer,sampleNbuffer};
        for (auto *table : {&materials,&amaterials}) for (auto &entry : *table) {
            strings.insert(entry.second.name);
            strings.insert(entry.second.tex[0]);strings.insert(entry.second.tex[1]);
        }
        for (auto *s:strings) delete s;
        delete[] tdata;delete[] errorBias;delete[] flags;
        delete nsamples;delete errthresholdScale;delete alwaysselectMaxdist;
        delete sampleRotation;delete sampleSize;
    }
};
