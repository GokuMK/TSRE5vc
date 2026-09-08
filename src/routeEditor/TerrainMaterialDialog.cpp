#include "TerrainMaterialDialog.h"
#include <tsre/world/TerrainMaterialLibrary.h>
#include <tsre/Game.h>
#include <QListWidget>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QMessageBox>

TerrainMaterialDialog::TerrainMaterialDialog(QWidget *parent, quint32 selected, const QString &message)
    : QDialog(parent), library(TerrainMaterialLibrary::current()) {
    setWindowTitle("Choose terrain material"); resize(600,440);
    auto *layout=new QVBoxLayout(this);
    auto *title=new QLabel("Route procedural materials",this);
    title->setStyleSheet("color: "+Game::StyleMainLabel+";"); layout->addWidget(title);
    if (!message.isEmpty()) {
        auto *context=new QLabel(message,this);
        context->setObjectName("materialChooserMessage");
        context->setTextFormat(Qt::PlainText);
        context->setWordWrap(true);
        layout->addWidget(context);
    }
    list=new QListWidget(this); layout->addWidget(list);
    status=new QLabel(this); status->setWordWrap(true); layout->addWidget(status);
    auto *buttons=new QDialogButtonBox(this);
    fromImage=buttons->addButton("From image...",QDialogButtonBox::ActionRole);
    choose=buttons->addButton("Choose",QDialogButtonBox::AcceptRole);
    buttons->addButton(QDialogButtonBox::Cancel); layout->addWidget(buttons);
    connect(buttons,&QDialogButtonBox::accepted,this,&QDialog::accept);
    connect(buttons,&QDialogButtonBox::rejected,this,&QDialog::reject);
    connect(list,&QListWidget::itemSelectionChanged,this,[this] { choose->setEnabled(selectedUid()!=0); });
    connect(list,&QListWidget::itemDoubleClicked,this,[this] { if (selectedUid()) accept(); });
    connect(fromImage,&QPushButton::clicked,this,[this] {
        const QString file=QFileDialog::getOpenFileName(this,"Terrain material source",library->textureDirectory(),
                "Images (*.ace *.dds *.png *.jpg *.jpeg *.bmp *.tga)");
        if (file.isEmpty()) return;
        QString error; const quint32 uid=library->addImage(file,error);
        if (!uid) QMessageBox::warning(this,"Terrain materials",error);
        refresh(uid);
    });
    library->reload(); refresh(selected);
}
quint32 TerrainMaterialDialog::selectedUid() const {
    return list->currentItem() && library->error().isEmpty() ? list->currentItem()->data(Qt::UserRole).toUInt() : 0;
}
void TerrainMaterialDialog::refresh(quint32 selected) {
    list->clear();
    if (library->error().isEmpty()) for (const auto &m : library->materials()) {
        auto *item=new QListWidgetItem(QString("%1 — %2").arg(m.uid).arg(m.displayName),list);
        item->setData(Qt::UserRole,m.uid); item->setToolTip(m.texture);
        if (m.uid==selected) list->setCurrentItem(item);
    }
    status->setText(library->error().isEmpty() ? "Stable UiDs are stored in terrainmaterials.dat. From image copies external sources into route TERRTEX." : library->error());
    choose->setEnabled(selectedUid()!=0);
    fromImage->setEnabled(Game::writeEnabled && !Game::serverClient && library->error().isEmpty());
}
