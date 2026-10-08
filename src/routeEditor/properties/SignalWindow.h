/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef SIGNALWINDOW_H
#define	SIGNALWINDOW_H

#include <QtWidgets>

class SignalObj;

#include <routeEditor/tools/EditorTool.h>

class SignalWindow : public QWidget { //QDialog {
    Q_OBJECT

public:
    SignalWindow(QWidget *parent);
    virtual ~SignalWindow();
    void showObj(SignalObj* obj);
    void updateObj(SignalObj* obj);
    // Set Link starts the signal link tool: off in modes it does not
    // support.
    void setViewMode(ViewMode mode);
    
public slots:
    void exitNow();
    void setLink();
    void chSubEnabled(int i);
    void bLinkEnabled(int i);
    
signals:
    void sendMsg(QString name, QString val);
    
private:
    static const int maxSubObj = 32;
    int currentSubObjLinkInfo = 0;
    QLineEdit name;
    QLineEdit description;
    // Parents must outlive member widgets/layouts during reverse destruction.
    QWidget wSub[maxSubObj];
    QGridLayout vSub[maxSubObj];
    QCheckBox chSub[maxSubObj];
    QPushButton bSub[maxSubObj];
    QLineEdit dSub[maxSubObj];
    QSignalMapper signalsChSect;
    QSignalMapper signalsLinkButton;
    SignalObj* sobj = nullptr;
    QPushButton* setLinkButton;
    QLineEdit eLink1;
    QLineEdit eLink2;
    QLineEdit eLink3;
    
    void setLinkInfo(int i);
};

#endif	/* SIGNALWINDOW_H */

