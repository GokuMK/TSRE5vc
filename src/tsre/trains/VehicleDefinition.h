#pragma once
#include <QString>
#include <QStringList>
#include <QVector>
#include <memory>
#include <tsre/fileFunctions/SimisDocument.h>

class EngLightsBlock;

// One ENG or WAG file as read (task driving 01): made once, never changed
// after, and shared by const pointer between the editor's cars (Eng) and the
// simulation thread. Its document holds every key of the file and its
// includes (the key catalogue, VehicleKeys, gives their units); the fields
// below are those C++ reads, in SI, read as Open Rails reads them: the last
// value given wins, couplings add. No Game, renderer or editor parts.
struct VehicleDefinition {
    struct Coupling {
        QString type;
        // Spring ( r0 ( ... ) ), metres; its velocity as written.
        float r0[2] = {0.0f, 0.0f};
        float velocity = 0.0f;
    };
    // A freight animation: MSTS's FreightAnim (its shape) or Open Rails'
    // FreightAnimStatic (a shape and its offset, metres).
    struct FreightAnim {
        QString shape;
        // The shape's file (files()).
        QString path;
        float offset[3] = {0.0f, 0.0f, 0.0f};
    };
    // Its Open Rails 3D cab (ORTS3DCab, ORTSAlternate3DCabViewPoints in the
    // Wagon section; the user's 2026-10-23): the shape files named, in
    // order, and the viewpoints: the head's place in the car's shape
    // (metres), where it looks at the start (degrees: down, round from
    // ahead) and how far it may turn from there (degrees; 0 free).
    struct Cab3D {
        struct Viewpoint {
            float position[3] = {0.0f, 0.0f, 0.0f};
            float start[3] = {0.0f, 0.0f, 0.0f};
            float limit[3] = {0.0f, 0.0f, 0.0f};
        };
        QStringList shapes;
        QVector<Viewpoint> viewpoints;
    };
    // Where its resistance came from: Open Rails' Davis coefficients,
    // MSTS's Friction converted, Open Rails' default from the mass; none
    // without a mass.
    enum class Resistance { None, Davis, Friction, Default };

    // Reads `name` in `folder` (the vehicle's folder, MSTS's), its copy in
    // folder/OPENRAILS when `openRails` and there is one. `game` is the game
    // folder: the roots and the folder check (SimisDocument).
    static std::shared_ptr<const VehicleDefinition> read(const QString &folder, const QString &name,
                                                         const QString &game, bool openRails);

    // An empty definition: a car's before its file is read.
    static const std::shared_ptr<const VehicleDefinition> &empty();

    // The file read and the vehicle's folder ([[Vehicle]]).
    QString path;
    QString folder;
    // Every key, through its includes.
    std::shared_ptr<const SimisDocument> document;
    bool ok() const { return document && document->ok(); }
    // The file read and every file its includes read.
    QStringList files() const { return document ? document->files() : QStringList(); }

    // Names: the first Wagon block's, the one shown (the last Name given,
    // in any section), TSRE's search keywords.
    QString engName;
    QString displayName;
    QString searchKeywords;
    // Wagon ( Type ), Engine ( Type ), both as "Type-EngineType"; the
    // editors' kind: 1 carriage, 2 freight, 3 tender, 4 electric, 5 diesel,
    // 6 steam, 0 other.
    QString type;
    QString engType;
    QString typeHash;
    int wagonTypeId = 0;
    QString brakeSystemType;
    // Its shape and freight animations, as written, and the shape's file.
    QString shape;
    QString shapePath;
    QVector<FreightAnim> freightAnims;
    // Its length with its coupler (the first's r0), as the editor lines
    // cars up and the simulation measures trains.
    float length() const { return (couplings.isEmpty() ? 0.0f : couplings[0].r0[0]) + size[2]; }
    // Metres: width, height, length; kg; the wheels' radii (the Engine
    // section's are the driving wheels).
    float size[3] = {0.0f, 0.0f, 0.0f};
    double massKg = 0.0;
    float wheelRadius = 0.0f;
    float driverWheelRadius = 0.0f;
    QVector<Coupling> couplings;
    // An engine's force (N), power (W), top speed (m/s), current (A).
    double forceN = 0.0;
    double powerW = 0.0;
    double maxSpeedMps = 0.0;
    double maxCurrent = 0.0;
    // As written: Open Rails' Davis A, B, C (N, N/(m/s), N/(m/s)²) where
    // given, MSTS's Friction (C1, E1, V2, C2, E2).
    double davis[3] = {0.0, 0.0, 0.0};
    bool hasDavis = false;
    double friction[5] = {0.0, 0.0, -1.0, 0.0, 0.0};
    bool hasFriction = false;
    // Its resistance worked out as Davis coefficients (the third for any
    // vehicle but the leading one, and for the leading one), and from what.
    double resistance[3] = {0.0, 0.0, 0.0};
    double resistanceLeading = 0.0;
    Resistance resistanceFrom = Resistance::None;
    // It carries passengers (PassengerCapacity, an EMU's power car too).
    bool passengerCapacity = false;
    // The outside .sms (Wagon) and the cab's (Engine), as written, and their
    // files (empty where none is there).
    QString soundFile;
    QString cabSoundFile;
    QString soundPath;
    QString cabSoundPath;
    // Engine ( CabView ): the 2D cab's .cvf as written, and its file.
    QString cabViewFile;
    QString cabViewPath;
    // Null without one; the shape drawn for it: the last named that is
    // there (Open Rails keeps one shape for its cabs), empty for none.
    std::shared_ptr<const Cab3D> cab3D;
    QString cab3DShapePath;
    // Its Lights block (task renderer 34) as its text, read on first use
    // (on the main thread); null without one.
    std::shared_ptr<EngLightsBlock> lights;
    // Its effects' emitters (task driving 05), as Open Rails reads them:
    // Wagon ( Effects ( SpecialEffects ( … ) ) ) for any vehicle, Engine
    // ( Effects ( DieselSpecialEffects ( … ) ) ) for a diesel and
    // SteamSpecialEffects for a steam engine, each `name ( x y z  dx dy dz
    // diameter )`, every block's added in file order. The name as written
    // and its kind (exhaustKind), its place in the car's shape and its
    // direction (a unit vector; up where none is given), MSTS's axes as
    // written, metres.
    struct Emitter {
        QString name;
        QString kind;
        float position[3] = {0.0f, 0.0f, 0.0f};
        float direction[3] = {0.0f, 1.0f, 0.0f};
        float diameter = 0.0f;
    };
    QVector<Emitter> emitters;
    // Its emitters' kinds, each once, in the order first met (CarExhaust's).
    QStringList exhaustKinds;
    // A number of its file at a path ("wagon(maxbrakeforce"), the value at
    // `index` of the last such block, in SI by the key catalogue (its
    // quantity and default unit); `otherwise` where the file has none.
    double number(QAnyStringView path, int index = 0, double otherwise = 0.0) const;
    // A text of its file at a path ("wagon(brakeequipmenttype"), the value
    // at `index` of the last such block; empty where it has none.
    QString text(QAnyStringView path, int index = 0) const;
    // Problems found in its fields (the document has the file's own).
    QVector<SimisDocument::Problem> problems;

    // A file named in it (`written` in `holder`, a file of its document),
    // as all of TSRE finds it (the user's, 2026-10-28): with a root at its
    // start, where the root says (SimisDocument::resolve), told among the
    // problems when it cannot be; without one, the first of `folders`
    // holding it (Open Rails' places for such a file), else in the first.
    QString file(const QString &written, const QString &holder, const QStringList &folders,
                 const QString &game);
};
