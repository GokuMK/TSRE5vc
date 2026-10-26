#ifndef SHAPELIB_H
#define	SHAPELIB_H

#include <unordered_map>
#include <QHash>
#include <QString>
#include <QVariantMap>

class ComplexShape;

class ShapeLib {
public:
    enum class MstsBackend {
        ConfiguredDefault,
        Legacy,
        Complex,
        ComplexCompact
    };

    int jestshape = 0;
    std::unordered_map<int, ComplexShape*> shape;
    explicit ShapeLib(MstsBackend backend = MstsBackend::ConfiguredDefault);
    ShapeLib(const ShapeLib& orig);
    virtual ~ShapeLib();
    void reset();
    void delRef(int texx);
    void addRef(int texx);
    int addShape(QString path);
    int addShape(QString path, QString texPath);
    // With load options of its own (ShapeLoadOption: a 3D cab's
    // DoubleSided), a shape apart from the file loaded without them.
    int addShape(QString path, QString texPath, const QVariantMap &options);
private:
    std::unordered_map<int, QString> contexts;
    std::unordered_map<int, QString> pathKeys;
    // The shapes by path key and context: addShape looks them up here
    // instead of comparing every shape (most of a world tile's object
    // making on a large route).
    QHash<QString, int> byKey;
    // ContentPath::key of the paths asked for (an absolute path made from
    // the working directory each time: 15 us a call, a world tile's objects
    // ask thousands of times for the same few).
    QHash<QString, QString> keys;
    const QString &cachedKey(const QString &path);
    QString mstsBackend; // Fixed per library; cache cannot mix implementations.
    bool firstMstsLodOnly = false;

};

#endif	/* SHAPELIB_H */
