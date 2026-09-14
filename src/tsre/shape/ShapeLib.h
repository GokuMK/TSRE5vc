#ifndef SHAPELIB_H
#define	SHAPELIB_H

#include <unordered_map>
#include <QString>

class ComplexShape;

class ShapeLib {
public:
    int jestshape = 0;
    std::unordered_map<int, ComplexShape*> shape;
    ShapeLib();
    ShapeLib(const ShapeLib& orig);
    virtual ~ShapeLib();
    void reset();
    void delRef(int texx);
    void addRef(int texx);
    int addShape(QString path);
    int addShape(QString path, QString texPath);
    void invalidateRendererCaches(bool invalidateMatrixCache = true);
private:
    std::unordered_map<int, QString> contexts;
    std::unordered_map<int, QString> textureRoots;
    std::unordered_map<int, QString> pathKeys;
    QString mstsBackend; // Fixed per library; cache cannot mix implementations.
    bool firstMstsLodOnly = false;

};

#endif	/* SHAPELIB_H */
