#include "TerrainMaterialDialog.h"
#include <tsre/world/TerrainMaterialLibrary.h>
#include <tsre/Game.h>
#include <tsre/texture/AceDocument.h>
#include <tsre/texture/DdsLib.h>
#include <QTableWidget>
#include <QHeaderView>
#include <QImageReader>
#include <QDir>
#include <QSignalBlocker>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QMessageBox>

namespace {
const char *libraryHelp="Double-click a name to edit it. Changes are saved immediately to terrainmaterials.dat.";
QImage materialThumbnail(const QString &path, QString &error) {
    const QSize bound(64,64);
    if (path.endsWith(".ace",Qt::CaseInsensitive)) {
        AceDocument ace;
        if (!AceDocument::read(path,ace,error) || ace.levels.isEmpty()) return {};
        const auto &level=ace.levels.first();
        QSize size(level.width,level.height);
        if (size.width()>bound.width() || size.height()>bound.height()) size.scale(bound,Qt::KeepAspectRatio);
        size=size.expandedTo(QSize(1,1));
        QByteArray bgra;
        if (!ace.decodeThumbnail(0,size.width(),size.height(),bgra,error)) return {};
        return QImage(reinterpret_cast<const uchar*>(bgra.constData()),size.width(),size.height(),
                      QImage::Format_ARGB32_Premultiplied).copy();
    }
    QImage image;
    if (path.endsWith(".dds",Qt::CaseInsensitive)) {
        DdsImageInfo info;
        if (!DdsLib::loadImage(path,image,info,error)) return {};
    } else {
        QImageReader reader(path); reader.setAutoTransform(true);
        const QSize size=reader.size();
        if (size.isValid() && (size.width()>64 || size.height()>64))
            reader.setScaledSize(size.scaled(bound,Qt::KeepAspectRatio).expandedTo(QSize(1,1)));
        image=reader.read();
        if (image.isNull()) error=reader.errorString();
    }
    return image.isNull() ? image : image.scaled(bound,Qt::KeepAspectRatio,Qt::SmoothTransformation);
}
}

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
    table=new QTableWidget(0,3,this); layout->addWidget(table);
    table->setObjectName("terrainMaterialTable");
    table->setHorizontalHeaderLabels({"UiD","Texture","Name"});
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    table->setIconSize(QSize(64,64));
    table->verticalHeader()->hide();
    table->verticalHeader()->setDefaultSectionSize(72);
    table->horizontalHeader()->setSectionResizeMode(0,QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(1,QHeaderView::Fixed);
    table->setColumnWidth(1,84);
    table->horizontalHeader()->setSectionResizeMode(2,QHeaderView::Stretch);
    status=new QLabel(this); status->setTextFormat(Qt::PlainText); status->setWordWrap(true); layout->addWidget(status);
    auto *buttons=new QDialogButtonBox(this);
    fromImage=buttons->addButton("From image...",QDialogButtonBox::ActionRole);
    choose=buttons->addButton("Choose",QDialogButtonBox::AcceptRole);
    buttons->addButton(QDialogButtonBox::Cancel); layout->addWidget(buttons);
    connect(buttons,&QDialogButtonBox::accepted,this,&QDialog::accept);
    connect(buttons,&QDialogButtonBox::rejected,this,&QDialog::reject);
    connect(table,&QTableWidget::itemSelectionChanged,this,[this] { choose->setEnabled(selectedUid()!=0); });
    connect(table,&QTableWidget::itemDoubleClicked,this,[this](QTableWidgetItem *item) {
        if (item->column()!=2 && selectedUid()) accept();
    });
    connect(table,&QTableWidget::itemChanged,this,[this](QTableWidgetItem *item) {
        if (item->column()!=2) return;
        const quint32 uid=item->data(Qt::UserRole).toUInt();
        QString error;
        const bool saved=library->rename(uid,item->text(),error);
        const QSignalBlocker blocked(table);
        const auto *material=library->find(uid);
        item->setText(material ? material->displayName : item->data(Qt::UserRole+1).toString());
        item->setData(Qt::UserRole+1,item->text());
        status->setText(saved ? QString::fromLatin1(libraryHelp) : "Name not saved: "+error);
    });
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
    return table->currentItem() && library->error().isEmpty() ? table->currentItem()->data(Qt::UserRole).toUInt() : 0;
}
void TerrainMaterialDialog::refresh(quint32 selected) {
    const QSignalBlocker blocked(table);
    table->setRowCount(0);
    if (library->error().isEmpty()) for (const auto &m : library->materials()) {
        const int row=table->rowCount(); table->insertRow(row);
        auto *uid=new QTableWidgetItem(QString::number(m.uid));
        auto *thumbnail=new QTableWidgetItem;
        auto *name=new QTableWidgetItem(m.displayName);
        name->setData(Qt::UserRole+1,m.displayName);
        table->setItem(row,0,uid); table->setItem(row,1,thumbnail); table->setItem(row,2,name);
        for (auto *item : {uid,thumbnail,name}) {
            item->setData(Qt::UserRole,m.uid); item->setToolTip(m.texture);
            item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        }
        if (Game::writeEnabled && !Game::serverClient) name->setFlags(name->flags() | Qt::ItemIsEditable);
        QString error;
        const QImage preview=materialThumbnail(QDir(library->textureDirectory()).filePath(m.texture),error);
        if (!preview.isNull()) thumbnail->setIcon(QPixmap::fromImage(preview));
        else { thumbnail->setText("No preview"); thumbnail->setToolTip(m.texture+"\n"+error); }
        if (m.uid==selected) table->setCurrentItem(name);
    }
    status->setText(library->error().isEmpty() ? QString::fromLatin1(libraryHelp) : library->error());
    choose->setEnabled(selectedUid()!=0);
    fromImage->setEnabled(Game::writeEnabled && !Game::serverClient && library->error().isEmpty());
}
