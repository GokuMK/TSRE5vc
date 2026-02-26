/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef COMPLEXSHAPE_H
#define	COMPLEXSHAPE_H

#include <QString>

class ComplexShape {
public:
    virtual ~ComplexShape() = default;

    virtual const QString& getPathId() const = 0;

    virtual void load() = 0;
    virtual void reload() = 0;

    virtual unsigned int newState() = 0;
    virtual void updateSim(float deltaTime, unsigned int stateId = 0) = 0;

    virtual void render() = 0;
    virtual void render(int selectionColor, unsigned int stateId) = 0;
    virtual void pushRenderItem() = 0;
    virtual void pushRenderItem(int selectionColor, unsigned int stateId) = 0;

    virtual void invalidateRenderState(bool invalidateMatrixCache = true) = 0;
};

#endif	/* COMPLEXSHAPE_H */

