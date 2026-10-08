/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef QUADTREE_H
#define	QUADTREE_H

#include <QString>
#include <QHash>
#include <QVector>
#include <QStringList>
#include <functional>

class FileBuffer;
class TerrainInfo;
class QTextStream;

class QuadTree {
public:
    // What visit() reports: every node's square (corner and side in world
    // tiles, tree coordinates), and every populated quadrant as the terrain
    // tile it names (TerrainInfo name, corner cx, cy and size level).
    struct Visitor {
        std::function<void(int x, int y, int size)> node;
        std::function<void(const TerrainInfo &info)> tile;
    };
    // A quadrant of the tree (map mode's quadtree tool): the square of
    // `level` world tiles at x, y (tree coordinates, aligned to level), the
    // name of its terrain tile, and whether it is populated.
    struct Quad {
        int x = 0;
        int y = 0;
        int level = 0;
        bool populated = false;
        QString name;
    };

    /*struct TreePos {
        int level;
        int sum;
        int x;
        int y;
        QVector<std::pair<int, int>> tile;
        
        TreePos(int l){
            level = l;
            sum = 0;
        }
    };*/
    struct QuadTile {
        static QChar PrefixString[2];
        int level;
        int prefix;
        int sum;
        int x;
        int y;
        unsigned int nameId = 0;
        QuadTile* tile[2][2];
        bool populated[2][2];
        
        QuadTile(int l, int p, int xx, int yy);
        ~QuadTile();
        void save(QVector<unsigned char> &data);
        void load(FileBuffer* data);
        void addTile(int tileX, int tileY, int dLevel);
        QString getMyName(int tileX, int tileY);
        unsigned int getMyNameId(int tileX, int tileY);
        bool fillTerrainInfo(int tileX, int tileY, TerrainInfo* info);
        // The terrain tile of quadrant px, py: name, corner and size.
        void quadrantInfo(int px, int py, TerrainInfo *info) const;
        void visit(int minX, int maxX, int minY, int maxY, const Visitor &visitor,
                   bool low) const;
        // The quadrant of `level` at x, y below this node, creating the nodes
        // on the way when create is set: the node owning it and its index.
        QuadTile *quadrantOwner(int qx, int qy, int qLevel, bool create, int &px, int &py);
        void quadAt(int tileX, int tileY, Quad &quad) const;
        int listNames();
    };
    struct TdFile {
        int x;
        int y;
        QuadTile* qt = NULL;
        bool modified = false;
        //unsigned char data[512][512];
    };
    QHash<int, TdFile*> td;
    QuadTree(bool l = false);
    virtual ~QuadTree();
    QuadTree(const QuadTree&) = delete;
    QuadTree& operator=(const QuadTree&) = delete;
    enum class SavePolicy { Immediate, Deferred };
    enum class LoadStatus { Loaded, Missing, Invalid };
    LoadStatus loadChecked(const QString &tdDirectory, QString &error);
    bool saveChecked(const QString &tdDirectory, QString &error);
    bool reconstruct(const QString &tileDirectory, QStringList &issues, int &count);
    static bool decodeTileName(const QString &name, int &x, int &y, int &level);
    bool insertTile(int x, int y, int level, SavePolicy policy = SavePolicy::Deferred);
    bool isModified() const;
    void makeTemporary() { temporary = true; recovery = true; }
    void adoptRecovery() { temporary = false; recovery = true; modified = true; backupDirectory.clear(); }
    bool isTemporary() const { return temporary; }
    bool isRecovery() const { return recovery; }
    bool immediateSaveAllowed() const { return !temporary && !recovery; }
    void load();
    void load(FileBuffer *data, bool loadtd = true);
    void save();
    void save(QTextStream &out);
    void createNew(int tileX, int tileY, SavePolicy policy = SavePolicy::Immediate);
    void addTile(int tileX, int tileY, SavePolicy policy = SavePolicy::Immediate);
    void fillTerrainInfo(int tileX, int tileY, TerrainInfo* info);
    // The nodes and populated quadrants overlapping world tiles minX..maxX,
    // minY..maxY (tree coordinates, inclusive), for drawing the tree (map
    // mode, task editor 04).
    void visit(int minX, int maxX, int minY, int maxY, const Visitor &visitor) const;
    // The smallest quadrant containing a world tile: nodes are followed
    // where a quadrant is split. Outside every TD block, the block's
    // quadrant of 256 tiles, not populated.
    Quad quadAt(int tileX, int tileY) const;
    // Splits a quadrant into four: the node dividing it is created (level of
    // 2 or more). False for an invalid quadrant or one already split.
    bool splitQuad(int x, int y, int level, SavePolicy policy = SavePolicy::Immediate);
    // Sets whether a quadrant is populated, without touching tile files.
    bool setPopulated(int x, int y, int level, bool populated,
                      SavePolicy policy = SavePolicy::Immediate);
    QString getMyName(int tileX, int tileY);
    unsigned int getMyNameId(int tileX, int tileY);
    void listNames();
    bool isLow();
    void loadTD(int x, int y);
    void loadTD(int x, int y, FileBuffer *data);
    void saveTD(int x, int y);
    void saveTD(int x, int y, QDataStream *out);
private:
    bool temporary = false;
    bool recovery = false;
    bool modified = false;
    QString backupDirectory;
    int terrainDescSize = 67108864;
    int depth = 6;
    bool low = false;
    QString getNameXY(int e);
};

#endif	/* QUADTREE_H */

