/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef SKYDOME_H
#define	SKYDOME_H

#include <tsre/GameObj.h>

class RenderQueue;

class GLUU;
class ComplexShape;

class Skydome : public GameObj {
public:
    Skydome();
    Skydome(const Skydome& orig);
    virtual ~Skydome();
    
    // Submits the sky shape with the renderer's current transform.
    void pushRenderItems(RenderQueue &queue);

private:
    bool loaded = false;
    ComplexShape* shapePointer = NULL;
};

#endif	/* SKYDOME_H */

