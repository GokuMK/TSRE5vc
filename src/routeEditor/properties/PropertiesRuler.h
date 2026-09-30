/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef PROPERTIESRULER_H
#define	PROPERTIESRULER_H

#include <routeEditor/properties/PropertiesAbstract.h>

class Ref;

class PropertiesRuler: public PropertiesAbstract{
    Q_OBJECT
public:
    PropertiesRuler();
    virtual ~PropertiesRuler();
    bool support(GameObj* obj);
    void showObj(GameObj* obj);
    void updateObj(GameObj* obj);
        
public slots:
    void checkboxTwoPointEdited(int val);
    void checkboxDrawPointsEdited(int val);
    void createRoadPathsEdited();
    void removeRoadPathsEdited();
    void removeNodeShapeEdited();
    void elevTypeEdited(QString val);
    void eTemplateEdited(QString val);
    void eTemplateSubtypeEdited(QString val);
    void eNodeShapeActivated(int index);
    void eNodeShapeEditingFinished();
    void hideElevBoxes();
    void showElevBox(QString val);
    
signals:
    
private:
    void refreshTemplateList();
    void updateTemplateValue();
    void refreshNodeShapeList();
    void updateNodeShapeValue();
    QString selectedNodeShapeValue() const;
    void applyNodeShapeValue();
    void selectNodeShapeValue(const QString &value);
    QLineEdit lengthM;
    QLineEdit lengthGM;
    QCheckBox checkboxTwoPoint;
    QCheckBox checkboxDrawPoints;
    QComboBox eNodeShape;
    QPushButton *removeNodeShapeButton = nullptr;
    const Ref *nodeShapeListRef = nullptr;
    QString nodeShapeListRoute;
    int nodeShapeCatalogCount = 0;
    
    QComboBox elevType;
    QLineEdit elevProm;
    QLineEdit elevProg;
    QLineEdit elevProp;
    QLineEdit elev1inXm;
    QLabel elevPromLabel;
    QLabel elevProgLabel;
    QLabel elevPropLabel;
    QLabel elev1inXmLabel;
};

#endif	/* PROPERTIESRULER_H */

