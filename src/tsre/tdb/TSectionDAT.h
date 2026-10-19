/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef TSECTIONDAT_H
#define	TSECTIONDAT_H
#include <unordered_map>
#include <QHash>
#include <QPair>
#include <QStringList>
#include <QVector>
#include <tsre/tdb/TSection.h>
#include <tsre/tdb/TrackShape.h>

class FileBuffer;
class QTextStream;

class TSectionDAT {
public:
    std::unordered_map<int, TSection*> sekcja;
    std::unordered_map<int, TrackShape*> shape;
    QHash<unsigned int, unsigned int> autoFixedSectionIds;
    QHash<unsigned int, unsigned int> autoFixedShapeIds;
    
    bool loaded;
    bool dataOutOfSync = false;
    bool updateSectionDataRequired = false;
    
    int tsectionMaxIdx = 0;// = 40000;
    int tsectionShapes = 0;// = 40000;
    int routeMaxIdx;// = 40000;
    int routeShapes;// = 40000;
    //std::unordered_map<int, TrackShape::SectionIdx*> routeShape;
    
    TSectionDAT(bool autoFix = false, bool loadRouteNow = true);
    TSectionDAT(const TSectionDAT& orig);
    virtual ~TSectionDAT();
    bool isRoadShape(int id);
    void getShapeData(int id);
    bool saveRoute();
    bool loadRoute(bool autoFix = false);
    void loadRouteUtf16Data(FileBuffer *data, bool autoFix = false);
    void saveRouteToStream(QTextStream &out);
    void mergeTSection(TSectionDAT *second, QHash<unsigned int,unsigned int>& fixedSectionIds, QHash<unsigned int,unsigned int>& fixedShapeIds);
    // A distance between the parallel paths of the shapes with several
    // straight ones (double track, road lanes; not junctions, crossovers
    // or dyntrack), in metres: a separation new track lines up with. The
    // shape families that have it are their file names' leading letters
    // ("A" for A2t10mStrt.s, "SR" for SR_2tStr_c_025m.s), the most used
    // first.
    struct Separation {
        float metres = 0.0f;
        int shapes = 0;
        QStringList families;
        QString example;
    };
    // The most used first.
    QVector<Separation> parallelSeparations(bool road) const;
    bool rollbackRouteAdditions(int routeMaxIdxBefore, int routeShapesBefore,
            int expectedRouteMaxIdx, int expectedRouteShapes);
private:
    bool loadGlobal();
    
};

#endif	/* TSECTIONDAT_H */

