/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <conEditor/VehicleFilesDialog.h>
#include <tsre/Game.h>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLocale>
#include <QMessageBox>
#include <QProgressDialog>
#include <QUrl>
#include <QVBoxLayout>

namespace {

const int PathRole = Qt::UserRole;
const int OpensRole = Qt::UserRole + 1;

QString sizeText(qint64 bytes) {
    return QLocale().formattedDataSize(bytes, 1, QLocale::DataSizeSIFormat);
}

}

VehicleFilesDialog::VehicleFilesDialog(const QString &title, const QString &archiveName,
                                       const QVector<VehicleFiles::File> &files, QWidget *parent)
        : QDialog(parent), files(files), archiveName(archiveName) {
    setWindowTitle(title);
    resize(760, 560);
    tree.setColumnCount(3);
    tree.setHeaderLabels({
        //% "File"
        qtTrId("con.editor.vehicle.files.column.file"),
        //% "Size"
        qtTrId("con.editor.vehicle.files.column.size"),
        //% "Note"
        qtTrId("con.editor.vehicle.files.column.note")});

    const QString root = QDir::cleanPath(Game::root);
    for (const VehicleFiles::Folder &folder : VehicleFiles::byFolder(files, root)) {
        //% "Files: %1"
        QString note = qtTrId("con.editor.vehicle.files.folder.count").arg(folder.files.size());
        if (folder.missing > 0)
            //% "Files: %1, missing: %2"
            note = qtTrId("con.editor.vehicle.files.folder.count.missing").arg(folder.files.size()).arg(folder.missing);
        auto *top = new QTreeWidgetItem(&tree, {folder.name, sizeText(folder.bytes), note});
        QFont bold = top->font(0);
        bold.setBold(true);
        top->setFont(0, bold);
        const QDir base(VehicleFiles::fromRoot(folder.files[0].path, root).isEmpty() ? folder.name
                                                                                     : root + QLatin1Char('/') + folder.name);
        for (const VehicleFiles::File &file : folder.files) {
            QString fileNote;
            if (file.missing)
                //% "missing"
                fileNote = qtTrId("con.editor.vehicle.files.file.missing");
            else if (file.notRead)
                //% "not read: its OPENRAILS copy is"
                fileNote = qtTrId("con.editor.vehicle.files.file.not.read");
            auto *item = new QTreeWidgetItem(top, {base.relativeFilePath(file.path), file.missing ? QString() : sizeText(file.bytes), fileNote});
            item->setData(0, PathRole, file.path);
            item->setData(0, OpensRole, file.text() && !file.missing);
            if (file.missing)
                for (int column = 0; column < 3; ++column)
                    item->setForeground(column, Qt::red);
        }
    }
    tree.expandAll();
    tree.header()->setSectionResizeMode(0, QHeaderView::Stretch);
    tree.header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    tree.header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);

    //% "Open in external editor"
    openButton.setText(qtTrId("con.editor.vehicle.files.open"));
    //% "Archive..."
    archiveButton.setText(qtTrId("con.editor.vehicle.files.archive"));
    //% "Close"
    closeButton.setText(qtTrId("con.editor.vehicle.files.close"));
    openButton.setEnabled(false);

    auto *buttons = new QHBoxLayout();
    buttons->addWidget(&openButton);
    buttons->addWidget(&archiveButton);
    buttons->addStretch(1);
    buttons->addWidget(&closeButton);
    auto *layout = new QVBoxLayout(this);
    layout->addWidget(&tree);
    layout->addLayout(buttons);

    connect(&tree, &QTreeWidget::itemSelectionChanged, this, &VehicleFilesDialog::selectionChanged);
    connect(&tree, &QTreeWidget::itemDoubleClicked, this, &VehicleFilesDialog::openSelected);
    connect(&openButton, &QPushButton::clicked, this, &VehicleFilesDialog::openSelected);
    connect(&archiveButton, &QPushButton::clicked, this, &VehicleFilesDialog::archiveSelected);
    connect(&closeButton, &QPushButton::clicked, this, &QDialog::accept);
}

bool VehicleFilesDialog::canOpen(const QTreeWidgetItem *item) const {
    return item != nullptr && item->data(0, OpensRole).toBool();
}

void VehicleFilesDialog::selectionChanged() {
    openButton.setEnabled(canOpen(tree.currentItem()));
}

void VehicleFilesDialog::openSelected() {
    const QTreeWidgetItem *item = tree.currentItem();
    if (canOpen(item))
        QDesktopServices::openUrl(QUrl::fromLocalFile(item->data(0, PathRole).toString()));
}

void VehicleFilesDialog::archiveSelected() {
    const QString path = QFileDialog::getSaveFileName(this,
            //% "Archive with its files"
            qtTrId("con.editor.vehicle.files.archive.title"), archiveName + QLatin1String(".zip"),
            //% "Zip archives (*.zip)"
            qtTrId("con.editor.vehicle.files.archive.filter"));
    if (path.isEmpty())
        return;
    QProgressDialog progress(qtTrId("con.editor.vehicle.files.archive.title"), QString(), 0, files.size(), this);
    progress.setWindowModality(Qt::WindowModal);
    const VehicleFiles::Archived done = VehicleFiles::archive(files, Game::root, path, [&progress](int value, int) {
        progress.setValue(value);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        return true;
    });
    progress.close();
    if (done.ok)
        QMessageBox::information(this, windowTitle(),
                //% "Written: %1 files, with their paths from the game root: extract it in another game root. Left out, missing or outside the game root: %2."
                qtTrId("con.editor.vehicle.files.archive.done").arg(done.written).arg(done.left));
    else
        QMessageBox::warning(this, windowTitle(),
                //% "The archive could not be written: %1"
                qtTrId("con.editor.vehicle.files.archive.failed").arg(done.error));
}
