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

class FileBuffer;
class TerrainInfo;
class QTextStream;

class QuadTree {
public:

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

