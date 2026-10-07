/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef TRNODE_H
#define	TRNODE_H

#include <tsre/math3d/Vector2i.h>
#include <QString>
#include <tsre/tdb/TrackNodeData.h>

class QTextStream;
class FileBuffer;

class TRnode {
public:
    int typ;
    JunctionData junction;
    std::uint32_t endNodeValue = 0;
    TrackNodeUid uid;
    int iTrv = 0;
    TrackVectorSection *trVectorSection = NULL;
    int iTri = 0;
    int *trItemRef = NULL;
    int inputPinCount = 0;
    int outputPinCount = 0;
    std::array<TrackPin, 3> pins{};
    
    TRnode();
    TRnode(const TRnode& orig);
    virtual ~TRnode();
    Vector2i* getTile();
    void loadUtf16Data(FileBuffer *data);
    void saveToStream(QTextStream &out, int nid);
    bool isEnd();
    bool equals(TRnode* r);
    bool equalsIgnoreType(TRnode* r);
    int podmienTrPin(int stare, int nowe);
    bool isLikedTo(int id);
    int setTrPinK(int id, int nowe);
    float getVectorSectionXRot(int id);
    void addPositionOffset(float offsetXYZ[3]);
    void addTrackNodeItemOffset(unsigned int trackNodeOffset, unsigned int trackItemOffset);
private:

};

#endif	/* TRNODE_H */

