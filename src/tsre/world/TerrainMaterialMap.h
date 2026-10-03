#pragma once

#include <QByteArray>
#include <QHash>
#include <QImage>
#include <QSet>
#include <QString>
#include <QVector>

// Experimental categorical material plane. Rows follow terrainData's +Z rows.
// No heightmap resolution, OpenGL, TFile ownership or texture-library dependency.
class TerrainMaterialMap {
public:
    static constexpr int MinimumSide = 2048;
    static constexpr int Side = 4096; // Default for newly created maps.
    static constexpr int MaximumSide = 8192;
    enum class BakeSampling { Optimized2x, FullOutput };
    // Startup-cached settings: immutable while generation workers are running.
    static inline bool Enabled = true;
    static inline int OutputSide = 512;
    static inline int BakedSide = 1024;
    // Patch-local virtual scatter grid used only by direct GPU rendering.
    // This changes edge detail without allocating a noise texture or changing
    // the retained CPU/baked output resolution.
    static constexpr int DirectNoiseSide = 2048;
    // Horizontal camera-to-patch-center distance for detailed procedural output.
    // Independent of object/geometry LOD; farther patches use the saved bake.
    static inline float DetailDistanceMeters = 2048.0f;
    // Advanced debug/restore setting, applied on application restart.
    // Full input validation/hashing on load/save is opt-in, never a normal save cost.
    static inline bool ValidateBakeOnLoad = false;
    // Internal rollout switch. False restores the complete CPU-generated
    // patch path; it is deliberately not a route or user preference.
    static inline bool DirectGpuRendering = true;
    // Rebuild/reload to compare: 1 = strongest, 2 = deterministic scattering,
    // 3 = original nearest-neighbour, 4 = strongest support (ID only breaks ties).
    // Only generated output changes, not stored IDs.
    static constexpr int SamplingMode = 2;
    // Startup/debug choice. FullOutput preserves the former 512-square path;
    // Optimized2x only uses a 2x intermediate for bake cache misses.
    static inline BakeSampling BakeSamplingMode = BakeSampling::Optimized2x;
    enum EditOperation { TexturePaint = 0, FillPatch = 1, FloodFill = 2 };
    QByteArray ids;

    bool initialize(quint8 id = 0, int requestedSide = Side);
    bool setIds(const QByteArray &values);
    int side() const { return mapSide; }
    static bool supportedSide(int value);
    bool valid() const;
    bool read(const QString &path, QString &error);
    bool write(const QString &path, QString &error) const;
    static bool decode(const QByteArray &file, QByteArray &ids, QString &error,
                       int *decodedSide = nullptr);
    QByteArray encode() const;
    quint8 at(int x, int z) const;
    QSet<int> usedIds() const;
    // Coordinates/radius are ID-plane pixels; mask is a grayscale brush image.
    // Returns patches needing regeneration, including the filter's one-pixel
    // halo. Locks protect stored IDs; locked neighbours may still need redraw.
    QSet<int> paint(double x, double z, double radius, int id, int patches,
                    const QImage &mask, const QSet<int> &locked = {}, bool dryRun = false,
                    int mode = SamplingMode);
    // Fills ignore the brush mask/size. Flood fill uses four-connected equal IDs;
    // tile borders and locked patches are barriers. dryRun never modifies IDs.
    QSet<int> fill(int x, int z, int id, int patches, bool patchOnly,
                  const QSet<int> &locked = {}, bool dryRun = false, int mode = SamplingMode);
    // ID-plane coordinates: texel (i,j) is centred at (i+.5,j+.5).
    quint8 sampleId(double x, double z, int mode = SamplingMode, quint32 seed = 0) const;
    QByteArray patchKey(int patch, int patches, int mode = SamplingMode) const;
    QImage generate(int patch, int patches, const QHash<int, QImage> &sources,
                    int mode = SamplingMode) const;
    QImage generateAtSize(int patch, int patches, const QHash<int, QImage> &sources,
                          int outputSide, int mode = SamplingMode) const;
    // Caller supplies miniatures from the same source-image revision. Dimensions
    // are checked here too; a changed baked size must never reuse old-size data.
    QImage bake(int patches, const QHash<int, QImage> &sources,
                const QHash<QByteArray,QImage> &miniatures = {},
                const QImage &previous = {}, const QSet<int> &dirtyPatches = {},
                QVector<QByteArray> *recipeKeys = nullptr) const;
    // A correctly sized previous image limits work to dirtyPatches (including
    // sampling halos). Without it, all patches are generated. Optional recipe
    // keys belong to this exact ID-map revision; invalidate affected keys on edits.
    static QString textureKey(const QImage &rgb, bool bc1);
    static QByteArray encodeBC1(const QImage &rgb);

private:
    int mapSide = Side;
};
