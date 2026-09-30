/* This file is part of TSRE5. Licensed under GPL-3.0-or-later; see LICENSE.md. */
#ifndef PROPERTIESTELEPOLE_H
#define PROPERTIESTELEPOLE_H

#include <routeEditor/properties/PropertiesAbstract.h>

class PropertiesTelepole : public PropertiesAbstract {
    Q_OBJECT
public:
    PropertiesTelepole();
    bool support(GameObj *object) override;
    void showObj(GameObj *object) override;
    void updateObj(GameObj *object) override;

private slots:
    void configChanged(int index);

private:
    void refreshConfigList();
    QComboBox configList;
    QLineEdit length;
    QLineEdit population;
    QLineEdit spacing;
};

#endif
