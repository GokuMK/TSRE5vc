/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef PROPERTIESCONSIST_H
#define	PROPERTIESCONSIST_H

#include <routeEditor/properties/PropertiesAbstract.h>
 
class Consist;

class PropertiesConsist : public PropertiesAbstract {
    Q_OBJECT
public:
    PropertiesConsist();
    virtual ~PropertiesConsist();
    bool support(GameObj* obj);
    void showObj(GameObj* obj);
    void updateObj(GameObj* obj);
    
public slots:
    void bCamEnabled();
    void eSpeedEnabled(QString val);
    
signals:
    void cameraObject(GameObj* obj);

private:
    Consist *conObj = NULL;
    QLineEdit eSpeed;
    // A simulated train's state ("running 72 km/h"), shown for one alone.
    QLabel state;
    void showSimulated();
    //QLineEdit eName;

};

#endif	/* PROPERTIESCONSIST_H */

