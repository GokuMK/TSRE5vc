#pragma once
#include <tsre/fileFunctions/FileBuffer.h>
#include <QByteArray>
#include <QStringList>
#include <QMap>
#include <optional>
#include <vector>

// Descriptor storage only: no GL, terrain samples, texture loading or scalar DOM.
// Numeric patches are contiguous. Labels/unknown blocks live in cold sidecars.
namespace TerrainFile {
bool readDescriptorBytes(const QString &path,QByteArray &bytes,QString &error);
struct OrderEntry { TS::TokenId token; int index; };
struct Extras {
    QByteArray label; // Exact UTF-16LE label bytes, excluding length byte.
    QByteArray tail;
    std::vector<OrderEntry> order;
    std::vector<QByteArray> unknown; // Whole framed blocks, in source order.
    // Metadata for known children, allocated only if nonempty.
    QMap<quint64, Extras> children;
    bool empty() const;
};
struct TextureSlot { QString filename; qint32 arg0=0, arg1=0; };
struct UvCalc { qint32 arg0=0, arg1=0, arg2=0; float scale=0; };
struct Shader {
    QString name;
    bool texturesPresent=true, uvCalcsPresent=true;
    std::vector<TextureSlot> textures;
    std::vector<UvCalc> uvCalcs;
    Extras extras;
};
struct PatchUv {
    float x=0,y=0,w=0,b=0,c=0,h=0;
    float &component(int i) {
        switch(i){case 0:return x;case 1:return y;case 2:return w;case 3:return b;case 4:return c;default:return h;}
    }
};
struct Patch {
    quint32 flags=0;
    float centerX=0,averageY=0,centerZ=0,sphereRadius=0,rangeY=0,radiusM=0;
    quint32 shaderIndex=0;
    PatchUv uv;
    float errorBias=1;
};
struct PatchSet {
    std::optional<float> distance;
    std::optional<quint32> patchesPerSide;
    std::optional<QString> flagsBuffer;
    std::vector<Patch> patches;
    Extras extras;
};
struct Transfer { Shader shader; float x0=0,z0=0,x1=0,z1=0; Extras extras; };
struct Shape {
    QString filename;
    qint32 bounds[4]{};
    float rotations[3]{};
    Extras extras;
};
struct Samples {
    std::optional<quint32> count;
    std::optional<float> rotation, floor, scale, spacing;
    std::optional<QString> y,f,e,n,c,d;
    std::optional<QByteArray> alwaysSelect, unknownSelect;
};
struct Water { float sw=0,se=0,ne=0,nw=0; bool single=false; };
struct Data {
    // Upper bounds protect descriptor parsing, not engine-supported layouts.
    static constexpr int MaximumBytes=64*1024*1024;
    std::optional<float> errorThresholdScale,alwaysSelectMaxDistance;
    Samples samples;
    std::optional<Water> water;
    std::vector<Shader> shaders;
    std::vector<PatchSet> patchSets;
    std::vector<Transfer> transfers;
    std::vector<Shape> shapes;
    Extras extras;
    QStringList diagnostics;
    bool paired=false;
    bool ambiguous=false;
    // Transactional: failure leaves this object unchanged; error receives details.
    bool read(FileBuffer &input, QString &error);
    bool readFile(const QString &path, QString &error);
    QByteArray encode(QString &error) const;
    bool save(const QString &path, QString &error) const;
    void detectShaderLayout();
    int repairAuxiliaryReferences(); // Explicit repair, separate from pure codec.
    // No resource access or allocations during hot access.
    PatchSet *activeSet() { return patchSets.empty()?nullptr:&patchSets.back(); }
    const PatchSet *activeSet() const { return patchSets.empty()?nullptr:&patchSets.back(); }
};
}
