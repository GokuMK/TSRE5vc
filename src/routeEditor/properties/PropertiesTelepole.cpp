/* This file is part of TSRE5. Licensed under GPL-3.0-or-later; see LICENSE.md. */
#include <routeEditor/properties/PropertiesTelepole.h>

#include <tsre/Game.h>
#include <tsre/Undo.h>
#include <tsre/world/TelepoleData.h>
#include <tsre/world/objects/TelepoleObj.h>
#include <QSignalBlocker>

PropertiesTelepole::PropertiesTelepole() {
    QVBoxLayout *vbox = new QVBoxLayout;
    vbox->setSpacing(2);
    vbox->setContentsMargins(0, 1, 1, 1);
    infoLabel = new QLabel(
        //% "Telepole:"
        qtTrId("route.editor.properties.telepole.label.title"));
    infoLabel->setStyleSheet(QString("QLabel { color : ")
            + Game::StyleMainLabel + "; }");
    infoLabel->setContentsMargins(3, 0, 0, 0);
    vbox->addWidget(infoLabel);

    QFormLayout *vlist = new QFormLayout;
    vlist->setSpacing(2);
    vlist->setContentsMargins(3, 0, 3, 0);
    uid.setDisabled(true);
    tX.setDisabled(true);
    tY.setDisabled(true);
    vlist->addRow(
        //% "UiD:"
        qtTrId("route.editor.properties.telepole.label.uid"), &uid);
    vlist->addRow(
        //% "Tile X:"
        qtTrId("route.editor.properties.telepole.label.tile.x"), &tX);
    vlist->addRow(
        //% "Tile Z:"
        qtTrId("route.editor.properties.telepole.label.tile.z"), &tY);
    vbox->addItem(vlist);

    QLabel *label = new QLabel(
        //% "Configuration:"
        qtTrId("route.editor.properties.telepole.label.configuration"));
    label->setStyleSheet(QString("QLabel { color : ")
            + Game::StyleMainLabel + "; }");
    label->setContentsMargins(3, 0, 0, 0);
    vbox->addWidget(label);
    configList.setStyleSheet("combobox-popup: 0;");
    vbox->addWidget(&configList);
    connect(&configList, SIGNAL(activated(int)),
            this, SLOT(configChanged(int)));

    label = new QLabel(
        //% "Span:"
        qtTrId("route.editor.properties.telepole.label.span"));
    label->setStyleSheet(QString("QLabel { color : ")
            + Game::StyleMainLabel + "; }");
    label->setContentsMargins(3, 0, 0, 0);
    vbox->addWidget(label);

    vlist = new QFormLayout;
    vlist->setSpacing(2);
    vlist->setContentsMargins(3, 0, 3, 0);
    length.setDisabled(true);
    population.setDisabled(true);
    spacing.setDisabled(true);
    vlist->addRow(
        //% "Length:"
        qtTrId("route.editor.properties.telepole.label.length"), &length);
    vlist->addRow(
        //% "Pole count:"
        qtTrId("route.editor.properties.telepole.label.population"),
        &population);
    vlist->addRow(
        //% "Separation:"
        qtTrId("route.editor.properties.telepole.label.separation"), &spacing);
    vbox->addItem(vlist);
    vbox->addStretch(1);
    setLayout(vbox);
}

bool PropertiesTelepole::support(GameObj *object) {
    if(object == nullptr || object->typeObj != GameObj::worldobj)
        return false;
    return static_cast<WorldObj*>(object)->typeID == WorldObj::telepole;
}

void PropertiesTelepole::refreshConfigList() {
    const QSignalBlocker blocker(&configList);
    configList.clear();
    const QString routePath = Game::root + "/ROUTES/" + Game::route;
    const TelepoleData &catalog = TelepoleData::routeData(routePath);
    for(int index = 0; index < catalog.configCount(); index++){
        const TelepoleData::Config *entry = catalog.config(index);
        if(entry == nullptr)
            continue;
        configList.addItem(QString("%1: %2 (%3 m)")
                .arg(index).arg(entry->fileName)
                .arg(entry->separation, 0, 'g', 6), index);
    }
    if(configList.count() == 0){
        configList.addItem(
            //% "No telepole.dat configuration"
            qtTrId("route.editor.obj.tools.item.no.telepole.configuration"),
            -1);
        configList.setDisabled(true);
    } else {
        configList.setEnabled(true);
    }
}

void PropertiesTelepole::showObj(GameObj *object) {
    if(!support(object))
        return;
    worldObj = static_cast<WorldObj*>(object);
    refreshConfigList();
    TelepoleObj *telepole = static_cast<TelepoleObj*>(object);
    //% "Object: %1"
    infoLabel->setText(qtTrId("route.properties.telepole.object.type")
            .arg(telepole->type));
    uid.setText(QString::number(telepole->UiD));
    tX.setText(QString::number(telepole->x));
    tY.setText(QString::number(-telepole->y));
    length.setText(QString::number(telepole->spanLength(), 'f', 3) + " m");
    population.setText(QString::number(telepole->populationValue()));
    spacing.setText(QString::number(telepole->separation(), 'f', 3) + " m");
    const int index = configList.findData(telepole->configIndex());
    configList.setCurrentIndex(index >= 0 ? index : 0);
}

void PropertiesTelepole::updateObj(GameObj *object) {
    if(!support(object))
        return;
    TelepoleObj *telepole = static_cast<TelepoleObj*>(object);
    if(!length.hasFocus())
        length.setText(QString::number(telepole->spanLength(), 'f', 3) + " m");
    if(!population.hasFocus())
        population.setText(QString::number(telepole->populationValue()));
    if(!spacing.hasFocus())
        spacing.setText(QString::number(telepole->separation(), 'f', 3) + " m");
    const int index = configList.findData(telepole->configIndex());
    if(index >= 0 && configList.currentIndex() != index){
        const QSignalBlocker blocker(&configList);
        configList.setCurrentIndex(index);
    }
}

void PropertiesTelepole::configChanged(int index) {
    if(worldObj == nullptr || index < 0)
        return;
    TelepoleObj *telepole = static_cast<TelepoleObj*>(worldObj);
    const int config = configList.itemData(index).toInt();
    if(config == telepole->configIndex())
        return;
    Undo::SinglePushWorldObjData(worldObj);
    telepole->setConfigIndex(config);
    Undo::StateEnd();
    updateObj(worldObj);
}
