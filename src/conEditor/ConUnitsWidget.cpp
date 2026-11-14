/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <conEditor/ConUnitsWidget.h>
#include <tsre/trains/Consist.h>
#include <tsre/trains/Eng.h>
#include <tsre/trains/EngLib.h>
#include <tsre/Game.h>
#include <tsre/sim/AirBrake.h>
#include <tsre/sim/TrainLines.h>
#include <tsre/trains/VehicleDefinition.h>

namespace {
// The lines the coupling menu offers, with their texts.
struct LineChoice {
    int bit;
    const char *id;
};
}

ConUnitsWidget::ConUnitsWidget() {
    QPushButton *bUp = new QPushButton(
        //% "Up"
        qtTrId("con.editor.con.units.widget.button.b.up"));
    QPushButton *bDown = new QPushButton(
        //% "Down"
        qtTrId("con.editor.con.units.widget.button.b.down"));
    QPushButton *bDel = new QPushButton(
        //% "Delete"
        qtTrId("con.editor.con.units.widget.button.b.del"));
    QPushButton *bFlip = new QPushButton(
        //% "Flip"
        qtTrId("con.editor.con.units.widget.button.b.flip"));
    QHBoxLayout *bbox = new QHBoxLayout;
    bbox->addWidget(bUp);
    bbox->addWidget(bDown);
    bbox->addWidget(bDel);
    bbox->addWidget(bFlip);
    
    QVBoxLayout *vbox = new QVBoxLayout;
    vbox->setSpacing(2);
    vbox->setContentsMargins(0,1,1,1);
    QFormLayout *vlist = new QFormLayout;
    vlist->setSpacing(2);
    vlist->setContentsMargins(3,0,3,0);
    vlist->addRow(
        //% "Units:"
        qtTrId("con.editor.con.units.widget.label.units"),&count);
    // A car's number (task scripting 03): its UiD, as a coach's number on
    // its displays; another car's number again marked in red.
    number.setObjectName("conUnitNumber");
    number.setRange(0, 65535);
    number.setEnabled(false);
    number.setToolTip(
        //% "The chosen car's number: its UiD in the consist file. Displays show it as the coach's number (a timetable's #tsre.cars may give others); activities' work orders tell cars by it, so each car should have its own (another car's number is shown in red)."
        qtTrId("con.editor.con.units.widget.number.tooltip"));
    vlist->addRow(
        //% "Number:"
        qtTrId("con.editor.con.units.widget.label.number"),&number);
    // Its train's lines (task driving 10): the chosen car cut out of its
    // train's control, and the lines of its coupling to the next car.
    controlOff.setObjectName("conUnitControlOff");
    controlOff.setText(
        //% "Control off"
        qtTrId("con.editor.con.units.widget.control.off"));
    controlOff.setEnabled(false);
    controlOff.setToolTip(
        //% "The chosen loco cut out: no unit of its train works it and it starts completely powered off (TSREControl ( Off ) in the consist file; Open Rails skips it)."
        qtTrId("con.editor.con.units.widget.control.off.tooltip"));
    vlist->addRow(QString(), &controlOff);
    coupling.setObjectName("conUnitCoupling");
    coupling.setPopupMode(QToolButton::InstantPopup);
    coupling.setEnabled(false);
    coupling.setToolTip(
        //% "The lines of the chosen car's coupling to the next car: by default what both couplers allow (MaSzyna's AllowedFlag; for ENG and WAG cars the brake hose and control, the main hose between twin pipes); set here, written as TSRECoupling in the consist file (Open Rails skips it)."
        qtTrId("con.editor.con.units.widget.coupling.tooltip"));
    QAction *byDefault = couplingMenu.addAction(
        //% "Default (its couplers)"
        qtTrId("con.editor.con.units.widget.coupling.default"));
    byDefault->setData(0);
    byDefault->setCheckable(true);
    couplingMenu.addSeparator();
    static const LineChoice choices[] = {
        //% "Brake hose"
        {Sim::Lines::BrakeHose, QT_TRID_NOOP("con.editor.con.units.widget.coupling.brakehose")},
        //% "Main reservoir hose"
        {Sim::Lines::MainHose, QT_TRID_NOOP("con.editor.con.units.widget.coupling.mainhose")},
        //% "Control (multiple units)"
        {Sim::Lines::Control, QT_TRID_NOOP("con.editor.con.units.widget.coupling.control")},
        //% "Heating"
        {Sim::Lines::Heating, QT_TRID_NOOP("con.editor.con.units.widget.coupling.heating")},
        //% "High voltage"
        {Sim::Lines::HighVoltage, QT_TRID_NOOP("con.editor.con.units.widget.coupling.highvoltage")},
    };
    for (const LineChoice &choice : choices) {
        QAction *line = couplingMenu.addAction(qtTrId(choice.id));
        line->setData(choice.bit);
        line->setCheckable(true);
    }
    coupling.setMenu(&couplingMenu);
    vlist->addRow(
        //% "Coupling to next:"
        qtTrId("con.editor.con.units.widget.label.coupling"), &coupling);
    vbox->addItem(bbox);
    vbox->addItem(vlist);
    vbox->addWidget(&items);
    //vbox->addStretch(1);
    this->setLayout(vbox);
    this->setMinimumWidth(250);
    QObject::connect(&items, SIGNAL(itemClicked(QListWidgetItem*)),
                      this, SLOT(itemsSelected(QListWidgetItem*)));
    
    QObject::connect(bDel, SIGNAL(released()),
                      this, SLOT(bDelReleased()));
    QObject::connect(bUp, SIGNAL(released()),
                      this, SLOT(bUpReleased()));
    QObject::connect(bDown, SIGNAL(released()),
                      this, SLOT(bDownReleased()));
    QObject::connect(bFlip, SIGNAL(released()),
                      this, SLOT(bFlipReleased()));
    QObject::connect(&number, &QSpinBox::editingFinished, this, &ConUnitsWidget::numberEdited);
    QObject::connect(&controlOff, &QCheckBox::toggled, this, &ConUnitsWidget::controlOffToggled);
    QObject::connect(&couplingMenu, &QMenu::triggered, this, &ConUnitsWidget::couplingChosen);
    items.setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
}

ConUnitsWidget::~ConUnitsWidget() {
}

void ConUnitsWidget::setCon(Consist* c){
    con = c;
    if(con == NULL) return;
    items.clear();
    count.setText(QString::number(con->engItems.size()));
    Eng * e;
    QString prefix;
    for (int i = 0; i < con->engItems.size(); i++){
        e = Game::currentEngLib->eng[con->engItems[i].eng];
        if(e == NULL) continue;
        // Its number first (task scripting 03).
        prefix = QString::number(con->engItems[i].uid) + " \u00b7 ";
        QListWidgetItem *item;
        if(e->loaded !=1) {
            prefix += "[BROKEN]";
            if(con->engItems[i].flip) prefix+="[F]";
            item = new QListWidgetItem ( prefix+" "+con->engItems[i].ename, &items, i);
        } else {
            if(con->engItems[i].flip) prefix+="[F]";
            if(con->engItems[i].controlOff) prefix+="[OFF]";
            item = new QListWidgetItem ( prefix+" "+e->definition->engName, &items, i);
        }
        if(con->numberRepeated(i)) {
            item->setForeground(Qt::red);
            //% "Another car of the consist has number %1."
            item->setToolTip(qtTrId("con.editor.con.units.widget.number.repeated").arg(con->engItems[i].uid));
        }
    }
    selectUnit(con->selectedIdx);
}

void ConUnitsWidget::selectUnit(int idx){
    const bool chosen = con != NULL && idx >= 0 && idx < con->engItems.size();
    QSignalBlocker blocker(number);
    number.setEnabled(chosen);
    number.setValue(chosen ? con->engItems[idx].uid : 0);
    QSignalBlocker offBlocker(controlOff);
    controlOff.setEnabled(chosen);
    controlOff.setChecked(chosen && con->engItems[idx].controlOff);
    showCoupling(chosen ? idx : -1);
    for (int row = 0; row < items.count(); row++)
        if (items.item(row)->type() == idx)
            items.setCurrentRow(row);
}

void ConUnitsWidget::numberEdited(){
    if(con == NULL || con->selectedIdx < 0 || con->selectedIdx >= con->engItems.size()
            || con->engItems[con->selectedIdx].uid == number.value())
        return;
    con->setNumber(con->selectedIdx, number.value());
    emit refreshItem();
}

int ConUnitsWidget::couplingLines(int idx) const {
    if (con == NULL || idx < 0 || idx + 1 >= con->engItems.size())
        return -1;
    const Consist::EngItem &front = con->engItems[idx], &rear = con->engItems[idx + 1];
    if (!front.coupling.isEmpty())
        return Sim::ParseLines(front.coupling);
    const Eng *a = Game::currentEngLib->eng[front.eng], *b = Game::currentEngLib->eng[rear.eng];
    if (a == NULL || b == NULL || a->definition == nullptr || b->definition == nullptr)
        return -1;
    return Sim::DefaultLines(*a->definition, front.flip ? 0 : 1, *b->definition, rear.flip ? 1 : 0,
                             Sim::TwinPipe(*a->definition) && Sim::TwinPipe(*b->definition));
}

void ConUnitsWidget::showCoupling(int idx) {
    const int lines = couplingLines(idx);
    const bool own = con != NULL && idx >= 0 && idx < con->engItems.size() && con->engItems[idx].coupling.isEmpty();
    coupling.setEnabled(lines >= 0);
    for (QAction *action : couplingMenu.actions()) {
        const int bit = action->data().toInt();
        if (action->isSeparator())
            continue;
        action->setChecked(bit == 0 ? own : lines >= 0 && (lines & bit) != 0);
    }
    if (lines < 0) {
        coupling.setText(QStringLiteral("-"));
        return;
    }
    const QString text = Sim::LinesText(lines & ~Sim::Lines::Coupler);
    coupling.setText(own ? QString("%1 (%2)").arg(text.isEmpty() ? QStringLiteral("-") : text,
                                                  //% "default"
                                                  qtTrId("con.editor.con.units.widget.coupling.default.short"))
                         : (text.isEmpty() ? QStringLiteral("-") : text));
}

void ConUnitsWidget::controlOffToggled(bool off){
    if(con == NULL || con->selectedIdx < 0 || con->selectedIdx >= con->engItems.size()
            || con->engItems[con->selectedIdx].controlOff == off)
        return;
    con->setControlOff(con->selectedIdx, off);
    emit refreshItem();
}

void ConUnitsWidget::couplingChosen(QAction *action){
    const int idx = con != NULL ? con->selectedIdx : -1;
    const int lines = couplingLines(idx);
    if(lines < 0)
        return;
    const int bit = action->data().toInt();
    QString chosen;
    if (bit != 0) {
        chosen = Sim::LinesText((lines ^ bit) & ~Sim::Lines::Coupler);
        // A coupling set to nothing still writes its block (no lines).
        if (chosen.isEmpty())
            chosen = QStringLiteral("none");
    }
    con->setCoupling(idx, chosen);
    showCoupling(idx);
}

void ConUnitsWidget::itemsSelected(QListWidgetItem * item){
    emit selected(item->type());
    selectUnit(item->type());
}

void ConUnitsWidget::bDelReleased(){
    //Game::currentEngLib = englib;
    con->deteleSelected();
    emit refreshItem();
}

void ConUnitsWidget::bUpReleased(){
    //Game::currentEngLib = englib;
    con->moveLeftSelected();
    emit refreshItem();
}

void ConUnitsWidget::bDownReleased(){
    //Game::currentEngLib = englib;
    con->moveRightSelected();
    emit refreshItem();
}

void ConUnitsWidget::bFlipReleased(){
    //Game::currentEngLib = englib;
    con->flipSelected();
    emit refreshItem();
}