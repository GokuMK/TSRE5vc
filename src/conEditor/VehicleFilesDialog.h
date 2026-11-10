/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef VEHICLEFILESDIALOG_H
#define VEHICLEFILESDIALOG_H

#include <QDialog>
#include <QPushButton>
#include <QTreeWidget>
#include <QVector>
#include <tsre/trains/VehicleFiles.h>

// A vehicle's or a train's files by main folder: which are needed, what is
// missing here, its text data opened in an external editor, all of it packed
// into an archive for another game root.
class VehicleFilesDialog : public QDialog {
    Q_OBJECT
public:
    VehicleFilesDialog(const QString &title, const QString &archiveName, const QVector<VehicleFiles::File> &files,
                       QWidget *parent = nullptr);
    QTreeWidget tree;
    QPushButton openButton, archiveButton, closeButton;
    // A text file that is there.
    bool canOpen(const QTreeWidgetItem *item) const;

private slots:
    void selectionChanged();
    void openSelected();
    void archiveSelected();

private:
    QVector<VehicleFiles::File> files;
    QString archiveName;
};

#endif // VEHICLEFILESDIALOG_H
