/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */


#ifndef ERRORMESSAGESWINDOW_H
#define ERRORMESSAGESWINDOW_H

#include <QtWidgets>
#include <QMap>

class ErrorMessageProperties;
class PreciseTileCoordinate;
class GameObj;
class ErrorMessage;

class ErrorMessagesWindow : public QWidget {
    Q_OBJECT
public:
    ErrorMessagesWindow(QWidget* parent);
    virtual ~ErrorMessagesWindow();
    void refreshErrorList();
    bool showMessage(ErrorMessage *message);
    
public slots:
    void show();
    void hideEvent(QHideEvent *e);
    void errorListSelected(QTreeWidgetItem* item, int column);
    void jumpRequestReceived(PreciseTileCoordinate *c);
    void selectRequestReceived(GameObj *o);
    
signals:
    void windowClosed();
    void windowShown();
    void jumpTo(PreciseTileCoordinate *c);
    void selectObject(GameObj *o);
    
private:
    void showEvent(QShowEvent *event) override;
    bool selectMessage(ErrorMessage *message);
    QHash<int, QBrush> brushes;
    QTreeWidget errorList;
    ErrorMessageProperties *properties;
    
};

#endif /* ERRORMESSAGESWINDOW_H */

