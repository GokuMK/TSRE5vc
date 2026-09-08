/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include "TerrainTools.h"
#include "TerrainMaterialDialog.h"
#include <tsre/world/TerrainMaterialLibrary.h>
#include <tsre/world/TerrainMaterialSource.h>
#include <tsre/texture/TexLib.h>
#include <tsre/texture/Brush.h>
#include <tsre/texture/Texture.h>
#include <tsre/gui/GuiFunct.h>
#include <tsre/world/objects/TransferObj.h>
#include <tsre/gui/ClickableLabel.h>
#include <tsre/Game.h>

TerrainTools::TerrainTools(QString name)
    : QWidget(){
    setFixedWidth(250);
    int row = 0;
    
    texPreview = new QPixmap(192,192);
    defaultTexPreview = new QPixmap(64,64);
    defaultTexPreview->fill(Qt::transparent);
    texPreview->fill(Qt::gray);
    texPreviewLabel = new ClickableLabel("");
    texPreviewLabel->setContentsMargins(0,0,0,0);
    texPreviewLabel->setPixmap(*texPreview);
    for(int i = 0; i < 7; i++){
        texPreviewLabels.push_back(new ClickableLabel(""));
        texPreviewLabels.back()->setObjectName(QString("recentTerrainMaterial%1").arg(i));
        texPreviewLabels.back()->setContentsMargins(0,0,0,0);
        texPreviewLabels.back()->setPixmap(*defaultTexPreview);
        texPreviewSignals.setMapping(texPreviewLabels.back(), i);
        connect(texPreviewLabels.back(), SIGNAL(clicked()), &texPreviewSignals, SLOT(map()));
    }
    texPreviewSignals.setMapping(texPreviewLabel, 7);
    connect(texPreviewLabel, SIGNAL(clicked()), &texPreviewSignals, SLOT(map()));
    connect(&texPreviewSignals, SIGNAL(mappedInt(int)), this, SLOT(texPreviewEnabled(int)));

    paintBrush = new Brush();

    QDir dir(QString("appdata/")+Game::AppDataVersion+"/brush/");
    dir.setFilter(QDir::Files);
    dir.setNameFilters(QStringList()<<"*.png");
    foreach(QString bfile, dir.entryList())
        brushShapes.push_back(QImage(QString("appdata/")+Game::AppDataVersion+"/brush/"+bfile).convertToFormat(QImage::Format_Grayscale8));
    nextBrushShape();
    
    buttonTools["heightTool"] = new QPushButton("HeightMap +", this);
    buttonTools["pickTerrainTexTool"] = new QPushButton("Pick", this);
    buttonTools["putTerrainTexTool"] = new QPushButton("Put", this);
    buttonTools["waterTerrTool"] = new QPushButton("Water +", this);
    //buttonTools["drawTerrTool"] = new QPushButton("Show/H Tile", this);
    buttonTools["gapsTerrainTool"] = new QPushButton("Gaps +", this);
    //buttonTools["waterHeightTileTool"] = new QPushButton("Water level", this);
    //buttonTools["fixedTileTool"] = new QPushButton("Fixed Height", this);
    //buttonTools["waTileTool"] = new QPushButton("Fixed Height", this);
    if(Game::serverClient == NULL){
        buttonTools["paintToolColor"] = new QPushButton("Color", this);
        buttonTools["paintToolTexture"] = new QPushButton("Texture", this);
        buttonTools["lockTexTool"] = new QPushButton("Lock", this);
        buttonTools["proceduralPaintTextureTool"] = new QPushButton("Texture", this);
        buttonTools["proceduralFillPatchTool"] = new QPushButton("Fill Patch", this);
        buttonTools["proceduralFillTool"] = new QPushButton("Fill", this);
        buttonTools["proceduralPickTool"] = new QPushButton("Pick", this);
        buttonTools["proceduralLockTool"] = new QPushButton("Lock", this);
        buttonTools["proceduralPickTool"]->setToolTip("Pick the source material from terrain. Same picking tool as in the static section.");
        buttonTools["proceduralLockTool"]->setToolTip("Toggle patch texture lock. Same shared lock as in the static section.");
        buttonTools["paintToolColor"]->setToolTip("Paint static textures only; procedural tiles are ignored.");
        buttonTools["paintToolTexture"]->setToolTip("Paint static textures only; procedural tiles are ignored.");
        buttonTools["lockTexTool"]->setToolTip("Toggle patch texture lock. Applies to both static and procedural painting and fills.");
        buttonTools["proceduralPaintTextureTool"]->setToolTip("Paint shader IDs on procedural tiles using the selected texture and brush mask. Undo groups long strokes into two-second actions.");
        buttonTools["proceduralFillPatchTool"]->setToolTip("Replace every shader ID in the clicked unlocked procedural patch. Ignores brush size/mask. Supports Undo.");
        buttonTools["proceduralFillTool"]->setToolTip("Fill the four-connected region matching the clicked shader ID, within this tile. Locked patches are barriers. Ignores brush size/mask. Supports Undo.");
    }
    buttonTools["putTerrainTexTool"]->setToolTip("Set the material of a static patch; procedural tiles are ignored.");
    QMapIterator<QString, QPushButton*> i(buttonTools);
    while (i.hasNext()) {
        i.next();
        i.value()->setCheckable(true);
        i.value()->setObjectName(i.key());
    }
    
    QPushButton *loadTerrainTexTool = new QPushButton("Load", this);
    loadTerrainTexTool->setObjectName("loadTerrainTexture");
    loadTerrainTexTool->setToolTip("Load a static texture from an image file.");
    
    QGridLayout *vlist3 = new QGridLayout;
    vlist3->setSpacing(2);
    vlist3->setContentsMargins(3,0,1,0);    
    row = 0;
    vlist3->addWidget(buttonTools["heightTool"],row,0);
    vlist3->addWidget(buttonTools["waterTerrTool"],row,1);
    vlist3->addWidget(buttonTools["gapsTerrainTool"],row++,2);
    //vlist3->addWidget(buttonTools["waterHeightTileTool"],row,2);
    //vlist3->addWidget(mapTileShowTool,row,0);
    //vlist3->addWidget(mapTileLoadTool,row,1);
    //vlist3->addWidget(heightTileLoadTool,row++,2);
    
    /*QGridLayout *vlist4 = new QGridLayout;
    vlist4->setSpacing(2);
    vlist4->setContentsMargins(3,0,1,0);    
    row = 0;
    vlist4->addWidget(buttonTools["waterTerrTool"],row,0);
    vlist4->addWidget(buttonTools["drawTerrTool"],row,1);
    vlist4->addWidget(buttonTools["gapsTerrainTool"],row++,2);*/
    
    QGridLayout *vlist0 = new QGridLayout;
    vlist0->setSpacing(2);
    vlist0->setContentsMargins(3,0,1,0);    
    row = 0;
    if (Game::serverClient == nullptr) {
        vlist0->addWidget(buttonTools["paintToolColor"],row,0);
        vlist0->addWidget(buttonTools["paintToolTexture"],row,1);
        vlist0->addWidget(buttonTools["lockTexTool"],row,2);
    }
    
    QGridLayout *vlist1 = new QGridLayout;
    vlist1->setSpacing(2);
    vlist1->setContentsMargins(3,0,1,0);    
    row = 0;
    vlist1->addWidget(buttonTools["pickTerrainTexTool"],row,0);
    vlist1->addWidget(buttonTools["putTerrainTexTool"],row,1);
    vlist1->addWidget(loadTerrainTexTool,row,2);
    
    colorw = new QPushButton("#000000", this);
    colorw->setStyleSheet("background-color:black;");

    QLabel *label0;
    QVBoxLayout *vbox = new QVBoxLayout;
    vbox->setSpacing(2);
    vbox->setContentsMargins(0,1,1,1);
    
    label0 = new QLabel("Edit Terrain Layers:");
    label0->setContentsMargins(3,0,0,0);
    label0->setStyleSheet(QString("QLabel { color : ")+Game::StyleMainLabel+"; }");
    vbox->addWidget(label0);
    vbox->addItem(vlist3);
    /*label0 = new QLabel("Terrain Patch:");
    label0->setContentsMargins(3,0,0,0);
    label0->setStyleSheet(QString("QLabel { color : ")+Game::StyleMainLabel+"; }");
    vbox->addWidget(label0);
    vbox->addItem(vlist4);*/
    {
        label0 = new QLabel("Static textures:");
        label0->setContentsMargins(3,0,0,0);
        label0->setStyleSheet(QString("QLabel { color : ")+Game::StyleMainLabel+"; }");
        vbox->addWidget(label0);
        vbox->addItem(vlist0);
        vbox->addItem(vlist1);
    }

    if (Game::serverClient == nullptr) {
        auto *heading = new QLabel("Procedural Materials (experimental):");
        heading->setStyleSheet(QString("QLabel { color : ")+Game::StyleMainLabel+"; }");
        vbox->addWidget(heading);
        const QStringList names {"proceduralTileEnableTool", "proceduralTileDisableTool"};
        const QStringList captions {"Enable on tile", "Disable"};
        auto *conversion=new QHBoxLayout;
        conversion->setSpacing(2);
        conversion->setContentsMargins(3,0,1,0);
        for (int i=0; i<names.size(); ++i) {
            auto *button = new QPushButton(captions[i],this);
            button->setCheckable(true);
            button->setObjectName(names[i]);
            button->setToolTip(i==0
                    ? "Choose the initial material, then click a tile to enable procedural painting. Supports Undo."
                    : "Click a procedural tile to switch it to static textures. Save first; its baked texture is kept. Supports Undo.");
            buttonTools[names[i]] = button;
            connect(button,&QPushButton::clicked,this,[this,tool=names[i]](bool checked) {
                if (checked && tool=="proceduralTileEnableTool"
                        && (!TerrainMaterialLibrary::current()->find(paintBrush->terrainMaterialUid)
                            || paintBrush->terrainMaterialRoute!=TerrainMaterialLibrary::current()->path())) {
                    if (!chooseProceduralMaterial("To enable procedural materials on a tile, first choose its initial material. "
                            "The tile will be filled with this material. Choose an existing entry or add one with From image, "
                            "then click the tile you want to convert.")) {
                        emit enableTool(QString()); return;
                    }
                }
                emit enableTool(checked ? tool : QString());
            });
            conversion->addWidget(button,i==0 ? 2 : 1);
        }
        vbox->addLayout(conversion);
        auto *painting = new QHBoxLayout;
        painting->setSpacing(2);
        painting->setContentsMargins(3,0,1,0);
        for (const QString &tool : {QStringLiteral("proceduralPaintTextureTool"),
                                   QStringLiteral("proceduralFillPatchTool"),
                                   QStringLiteral("proceduralFillTool")}) {
            QPushButton *button=buttonTools[tool];
            painting->addWidget(button);
            connect(button,&QPushButton::clicked,this,[this,tool](bool checked) {
                if (checked) {
                    paintBrush->useTexture=true;
                    emit setPaintBrush(paintBrush);
                }
                emit enableTool(checked ? tool : QString());
            });
        }
        vbox->addLayout(painting);
        auto *selection=new QHBoxLayout;
        selection->setSpacing(2);
        selection->setContentsMargins(3,0,1,0);
        selection->addWidget(buttonTools["proceduralPickTool"]);
        selection->addWidget(buttonTools["proceduralLockTool"]);
        auto *choose=new QPushButton("Choose",this);
        choose->setObjectName("chooseProceduralMaterial");
        choose->setToolTip("Choose a procedural material from the route library, or add one from an image.");
        selection->addWidget(choose);
        connect(choose,&QPushButton::clicked,this,[this] { chooseProceduralMaterial(); });
        connect(buttonTools["proceduralPickTool"],&QPushButton::clicked,this,&TerrainTools::pickTexToolEnabled);
        connect(buttonTools["proceduralLockTool"],&QPushButton::clicked,this,&TerrainTools::lockTexToolEnabled);
        vbox->addLayout(selection);
    }

    vlist1 = new QGridLayout;
    vlist1->setSpacing(0);
    vlist1->setContentsMargins(0,0,0,0);    
    vlist1->addWidget(texPreviewLabel, 0, 0, 3, 3);
    vlist1->addWidget(texPreviewLabels[0], 0, 3);
    vlist1->addWidget(texPreviewLabels[1], 1, 3);
    vlist1->addWidget(texPreviewLabels[2], 2, 3);
    vlist1->addWidget(texPreviewLabels[3], 3, 2);
    vlist1->addWidget(texPreviewLabels[4], 3, 1);
    vlist1->addWidget(texPreviewLabels[5], 3, 0);
    vlist1->addWidget(texPreviewLabels[6], 3, 3);
    vbox->addItem(vlist1);
    vbox->setAlignment(vlist1, Qt::AlignHCenter);
    //vbox->addWidget(texPreviewLabel);
    //vbox->setAlignment(texPreviewLabel, Qt::AlignHCenter);
    QLabel *label2 = new QLabel("Brush settings:");
    label2->setContentsMargins(3,0,0,0);
    label2->setStyleSheet(QString("QLabel { color : ")+Game::StyleMainLabel+"; }");
    vbox->addWidget(label2);
    

    int labelWidth = 70;
    
    // brush
    sSize = new QSlider(Qt::Horizontal);
    sSize->setMinimum(1);
    sSize->setMaximum(100);
    sSize->setValue(paintBrush->size);
    sIntensity = new QSlider(Qt::Horizontal);
    sIntensity->setMinimum(1);
    sIntensity->setMaximum(100);
    sIntensity->setValue(paintBrush->alpha*100);
    hType = new QComboBox;
    hType->setStyleSheet("combobox-popup: 0;");
    hType->addItem("Add - simple");
    hType->addItem("Add - if inside 'Size' radius");
    hType->addItem("Fixed Height");
    hType->addItem("Flatten");
    hType->setCurrentIndex(paintBrush->hType);
    fheight = new QLineEdit();
    QDoubleValidator* doubleValidator = new QDoubleValidator(-5000, 5000, 2, this); 
    doubleValidator->setNotation(QDoubleValidator::StandardNotation);
    fheight->setValidator(doubleValidator);
    
    QGridLayout *vlist = new QGridLayout;
    vlist->setSpacing(2);
    vlist->setContentsMargins(3,0,1,0);    
    leSize = GuiFunct::newQLineEdit(25,3);
    leSize->setValidator(new QIntValidator(1, 100, this));
    leIntensity = GuiFunct::newQLineEdit(25,3);
    leIntensity->setValidator(new QIntValidator(1, 100, this));
    row = 0;
    vlist->addWidget(GuiFunct::newQLabel("Color:", labelWidth),row,0);
    vlist->addWidget(colorw,row++,1,1,2);
    vlist->addWidget(GuiFunct::newQLabel("Size:", labelWidth),row,0);
    vlist->addWidget(leSize,row,1);
    vlist->addWidget(sSize,row++,2);
    vlist->addWidget(GuiFunct::newQLabel("Intensity:", labelWidth),row,0);
    vlist->addWidget(leIntensity,row,1);
    vlist->addWidget(sIntensity,row++,2);
    vlist->addWidget(GuiFunct::newQLabel("Fixed Height:", labelWidth),row,0);
    vlist->addWidget(fheight,row++,1,1,2);
    vlist->addWidget(GuiFunct::newQLabel("Height type:", labelWidth),row,0);
    vlist->addWidget(hType,row++,1,1,2);
    vbox->addItem(vlist);
    
    
    // enbankment
    sEsize = new QSlider(Qt::Horizontal);
    sEsize->setMinimum(1);
    sEsize->setMaximum(24);
    sEsize->setValue(paintBrush->eSize);
    sEemb = new QSlider(Qt::Horizontal);
    sEemb->setMinimum(10);
    sEemb->setMaximum(80);
    sEemb->setValue(paintBrush->eEmb);
    sEcut = new QSlider(Qt::Horizontal);
    sEcut->setMinimum(10);
    sEcut->setMaximum(80);
    sEcut->setValue(paintBrush->eCut);
    sEradius = new QSlider(Qt::Horizontal);
    sEradius->setMinimum(1);
    sEradius->setMaximum(800);
    sEradius->setValue(paintBrush->eRadius);
    leEsize = GuiFunct::newQLineEdit(25,3);
    leEsize->setValidator(new QIntValidator(1, 24, this));
    leEemb = GuiFunct::newQLineEdit(25,3);
    leEemb->setValidator(new QIntValidator(10, 80, this));
    leEcut = GuiFunct::newQLineEdit(25,3);
    leEcut->setValidator(new QIntValidator(10, 80, this));
    leEradius = GuiFunct::newQLineEdit(25,3);
    leEradius->setValidator(new QIntValidator(1, 800, this));
    QLabel *label3 = new QLabel("Embankment settings:");
    label3->setStyleSheet(QString("QLabel { color : ")+Game::StyleMainLabel+"; }");
    label3->setContentsMargins(3,0,0,0);
    vbox->addWidget(label3);
    
    QGridLayout *vlist2 = new QGridLayout;
    vlist2->setSpacing(2);
    vlist2->setContentsMargins(3,0,1,0);
    row = 0;
    vlist2->addWidget(GuiFunct::newQLabel("Size [m]:", labelWidth),row,0);
    vlist2->addWidget(leEsize,row,1);
    vlist2->addWidget(sEsize,row++,2);
    vlist2->addWidget(GuiFunct::newQLabel("Embank. [°]:", labelWidth),row,0);
    vlist2->addWidget(leEemb,row,1);
    vlist2->addWidget(sEemb,row++,2);
    vlist2->addWidget(GuiFunct::newQLabel("Cutting [°]:", labelWidth),row,0);
    vlist2->addWidget(leEcut,row,1);
    vlist2->addWidget(sEcut,row++,2);
    vlist2->addWidget(GuiFunct::newQLabel("Radius [m]:", labelWidth),row,0);
    vlist2->addWidget(leEradius,row,1);
    vlist2->addWidget(sEradius,row++,2);
    vbox->addItem(vlist2);
    
    vbox->addStretch(1);
    this->setLayout(vbox);
    
    
    // signals
    QObject::connect(buttonTools["heightTool"], SIGNAL(toggled(bool)),
                      this, SLOT(heightToolEnabled(bool)));
    if(Game::serverClient == NULL){
        QObject::connect(buttonTools["paintToolColor"], SIGNAL(toggled(bool)),
                          this, SLOT(paintColorToolEnabled(bool)));

        QObject::connect(buttonTools["paintToolTexture"], SIGNAL(toggled(bool)),
                          this, SLOT(paintTexToolEnabled(bool)));

        QObject::connect(buttonTools["lockTexTool"], SIGNAL(toggled(bool)),
                          this, SLOT(lockTexToolEnabled(bool)));
    }
    QObject::connect(buttonTools["pickTerrainTexTool"], SIGNAL(toggled(bool)),
                      this, SLOT(pickTexToolEnabled(bool)));
    
    QObject::connect(buttonTools["waterTerrTool"], SIGNAL(toggled(bool)),
                      this, SLOT(waterTerrToolEnabled(bool)));
    
    //QObject::connect(buttonTools["waterHeightTileTool"], SIGNAL(toggled(bool)),
    //                  this, SLOT(waterHeightTileToolEnabled(bool)));
    
    //QObject::connect(buttonTools["fixedTileTool"], SIGNAL(toggled(bool)),
    //                  this, SLOT(fixedTileToolEnabled(bool)));
    
    QObject::connect(buttonTools["gapsTerrainTool"], SIGNAL(toggled(bool)),
                      this, SLOT(gapsTerrToolEnabled(bool)));
    
    //QObject::connect(buttonTools["drawTerrTool"], SIGNAL(toggled(bool)),
    //                  this, SLOT(drawTerrToolEnabled(bool)));
    
    QObject::connect(buttonTools["putTerrainTexTool"], SIGNAL(toggled(bool)),
                      this, SLOT(putTexToolEnabled(bool)));
    
    QObject::connect(loadTerrainTexTool, SIGNAL(released()),
                      this, SLOT(setTexToolEnabled()));
    
    QObject::connect(colorw, SIGNAL(released()),
                      this, SLOT(chooseColorEnabled()));
    
    // brush
    QObject::connect(sSize, SIGNAL(valueChanged(int)),
                      this, SLOT(setBrushSize(int)));
    
    QObject::connect(sIntensity, SIGNAL(valueChanged(int)),
                      this, SLOT(setBrushAlpha(int)));

    QObject::connect(leSize, SIGNAL(textEdited(QString)),
                      this, SLOT(setBrushSize(QString)));
    
    QObject::connect(leIntensity, SIGNAL(textEdited(QString)),
                      this, SLOT(setBrushAlpha(QString)));
    
    // embarkment
    QObject::connect(sEsize, SIGNAL(valueChanged(int)),
                      this, SLOT(setEsize(int)));
    
    QObject::connect(leEsize, SIGNAL(textEdited(QString)),
                      this, SLOT(setEsize(QString)));    
    
    QObject::connect(sEemb, SIGNAL(valueChanged(int)),
                      this, SLOT(setEemb(int)));

    QObject::connect(leEemb, SIGNAL(textEdited(QString)),
                      this, SLOT(setEemb(QString)));

    QObject::connect(sEcut, SIGNAL(valueChanged(int)),
                      this, SLOT(setEcut(int)));

    QObject::connect(leEcut, SIGNAL(textEdited(QString)),
                      this, SLOT(setEcut(QString)));
    
    QObject::connect(sEradius, SIGNAL(valueChanged(int)),
                      this, SLOT(setEradius(int)));

    QObject::connect(leEradius, SIGNAL(textEdited(QString)),
                      this, SLOT(setEradius(QString)));
    
    QObject::connect(fheight, SIGNAL(textEdited(QString)),
                      this, SLOT(setFheight(QString)));
    
    QObject::connect(hType, SIGNAL(currentIndexChanged(int)),
                      this, SLOT(setHtype(int)));
    
    this->setBrushSize(this->sSize->value());
    this->setBrushAlpha(this->sIntensity->value());
    this->setEsize(this->sEsize->value());
    this->setEemb(this->sEemb->value());
    this->setEcut(this->sEcut->value());
    this->setEradius(this->sEradius->value());
    this->fheight->setText("0");
}


TerrainTools::~TerrainTools() {
    for (const auto &entry : texLastItems) TexLib::delRef(entry.textureId);
    if (selectedTextureRef>=0) TexLib::delRef(selectedTextureRef);
}

void TerrainTools::nextBrushShape(){
    currentBrushShape++;
    if(currentBrushShape > brushShapes.size()-1)
        currentBrushShape = 0;
    if(brushShapes.size() > 0){
        paintBrush->brushshape = &brushShapes[currentBrushShape];
        texPreviewLabels[6]->setPixmap(QPixmap::fromImage(*paintBrush->brushshape));
    }
}

void TerrainTools::heightToolEnabled(bool val){
    if(val){
        emit setPaintBrush(this->paintBrush);
        emit enableTool("heightTool");
    } else {
        emit enableTool("");
    }
}

void TerrainTools::paintColorToolEnabled(bool val){
     if(val){
        this->paintBrush->useTexture = false;
        emit setPaintBrush(this->paintBrush);
        emit enableTool("paintToolColor");
    } else {
        emit enableTool("");
    }
}

void TerrainTools::gapsTerrToolEnabled(bool val){
    if(val){
        emit enableTool("gapsTerrainTool");
    } else {
        emit enableTool("");
    }
}

void TerrainTools::paintTexToolEnabled(bool val){
    if(val){
        this->paintBrush->useTexture = true;
        emit setPaintBrush(this->paintBrush);
        emit enableTool("paintToolTexture");
    } else {
        emit enableTool("");
    }
}

void TerrainTools::chooseColorEnabled(){
    QColor aColor(paintBrush->color[0], paintBrush->color[1], paintBrush->color[2]);
    QColor color = QColorDialog::getColor(aColor, this, "Text Color",  QColorDialog::DontUseNativeDialog);
    paintBrush->color[0] = color.red();
    paintBrush->color[1] = color.green();
    paintBrush->color[2] = color.blue();
    colorw->setStyleSheet("background-color:"+color.name()+";");
    colorw->setText(color.name());
}

void TerrainTools::pickTexToolEnabled(bool val){
    if(val){
        emit enableTool("pickTerrainTexTool");
    } else {
        emit enableTool("");
    }
}

void TerrainTools::lockTexToolEnabled(bool val){
    if(val){
        emit enableTool("lockTexTool");
    } else {
        emit enableTool("");
    }
}

void TerrainTools::waterTerrToolEnabled(bool val){
    if(val){
        emit enableTool("waterTerrTool");
    } else {
        emit enableTool("");
    }
}

void TerrainTools::drawTerrToolEnabled(bool val){
    if(val){
        emit enableTool("drawTerrTool");
    } else {
        emit enableTool("");
    }
}

void TerrainTools::waterHeightTileToolEnabled(bool val){
    if(val){
        emit enableTool("waterHeightTileTool");
    } else {
        emit enableTool("");
    }
}

void TerrainTools::putTexToolEnabled(bool val){
    if(val){
        emit setPaintBrush(this->paintBrush);
        emit enableTool("putTerrainTexTool");
    } else {
        emit enableTool("");
    }
}

void TerrainTools::fixedTileToolEnabled(bool val){
    if(val){
        emit setPaintBrush(this->paintBrush);
        emit enableTool("fixedTileTool");
    } else {
        emit enableTool("");
    }
}

bool TerrainTools::chooseProceduralMaterial(const QString &message) {
        TerrainMaterialDialog dialog(this,paintBrush->terrainMaterialUid,message);
        if (dialog.exec()!=QDialog::Accepted) return false;
        const auto library=TerrainMaterialLibrary::current();
        const auto material=library->find(dialog.selectedUid());
        if (!material) return false;
        const int textureId=TexLib::addTex(library->textureDirectory(),material->texture);
        setBrushTextureId(textureId);
        TexLib::delRef(textureId);
        paintBrush->terrainMaterialUid=material->uid;
        paintBrush->terrainMaterialRoute=library->path();
        paintBrush->useTexture=true;
        rememberCurrentMaterial();
        QTimer::singleShot(300,this,&TerrainTools::updateTexPrev);
        emit setPaintBrush(paintBrush);
        return true;
}
void TerrainTools::setTexToolEnabled(){
    QFileDialog fd;
    QString path = Game::root+"/routes/"+Game::route+"/terrtex";
    path.replace("//", "/");
    fd.setDirectory(path);
    fd.setFileMode(QFileDialog::ExistingFiles);
    //QTreeView *tree = fd->findChild <QTreeView*>();
    //tree->setRootIsDecorated(true);
    //tree->setItemsExpandable(true);
    //fd->setFileMode(QFileDialog::F);
    //fd->setOption(QFileDialog::ShowDirsOnly);
    //fd->setViewMode(QFileDialog::Detail);
    int result = fd.exec();
    QString filename;
    if (!result) return;
    
    for(int i = 0; i < fd.selectedFiles().length(); i++){
        filename = fd.selectedFiles()[i];
        qDebug()<<"texture file "<<filename;

        int tid = TexLib::addTex(filename);
        setBrushTextureId(tid);
        TexLib::delRef(tid);
        rememberCurrentMaterial();
    //emit enableTool("setTexTool");
    }
    emit setPaintBrush(this->paintBrush);
    
    //QTimer::singleShot(200, this, SLOT(updateTexPrev()));
}

// brush

void TerrainTools::setBrushSize(QString val){
    emit setPaintBrush(this->paintBrush);
    //qDebug() << "a";
    int ival = val.toInt(0, 10);
    this->sSize->setValue(ival);
    this->paintBrush->size = ival;
}

void TerrainTools::setBrushAlpha(QString val){
    emit setPaintBrush(this->paintBrush);
    //qDebug() << "a";
    int ival = val.toInt(0, 10);
    this->sIntensity->setValue(ival);
    this->paintBrush->alpha = (float)ival/100;
}

void TerrainTools::setBrushSize(int val){
    emit setPaintBrush(this->paintBrush);
    //qDebug() << "a";
    this->leSize->setText(QString::number(val,10));
    this->paintBrush->size = val;
}

void TerrainTools::setBrushAlpha(int val){
    emit setPaintBrush(this->paintBrush);
    //qDebug() << "a";
    this->leIntensity->setText(QString::number(val,10));
    this->paintBrush->alpha = (float)val/100;
}

void TerrainTools::setFheight(QString val){
    emit setPaintBrush(this->paintBrush);
    //qDebug() << "a";
    float ival = val.toFloat(0);
    this->paintBrush->hFixed = ival;
}

void TerrainTools::setHtype(int val){
    emit setPaintBrush(this->paintBrush);
    if(val < 0) return;
    this->paintBrush->hType = val;
}

// embarkment

void TerrainTools::setEsize(int val){
    emit setPaintBrush(this->paintBrush);
    //qDebug() << "a";
    this->leEsize->setText(QString::number(val,10));
    this->paintBrush->eSize = val;
}
void TerrainTools::setEsize(QString val){
    emit setPaintBrush(this->paintBrush);
    //qDebug() << "a";
    int ival = val.toInt(0, 10);
    this->sEsize->setValue(ival);
    this->paintBrush->eSize = ival;
}
void TerrainTools::setEemb(int val){
    emit setPaintBrush(this->paintBrush);
    //qDebug() << "a";
    this->leEemb->setText(QString::number(val,10));
    this->paintBrush->eEmb = val;
}
void TerrainTools::setEemb(QString val){
    emit setPaintBrush(this->paintBrush);
    //qDebug() << "a";
    int ival = val.toInt(0, 10);
    this->sEemb->setValue(ival);
    this->paintBrush->eEmb = ival;
}
void TerrainTools::setEcut(int val){
    emit setPaintBrush(this->paintBrush);
    //qDebug() << "a";
    this->leEcut->setText(QString::number(val,10));
    this->paintBrush->eCut = val;
}
void TerrainTools::setEcut(QString val){
    emit setPaintBrush(this->paintBrush);
    //qDebug() << "a";
    int ival = val.toInt(0, 10);
    this->sEcut->setValue(ival);
    this->paintBrush->eCut = ival;
}
void TerrainTools::setEradius(int val){
    emit setPaintBrush(this->paintBrush);
    //qDebug() << "a";
    this->leEradius->setText(QString::number(val,10));
    this->paintBrush->eRadius = val;
}
void TerrainTools::setEradius(QString val){
    emit setPaintBrush(this->paintBrush);
    //qDebug() << "a";
    int ival = val.toInt(0, 10);
    this->sEradius->setValue(ival);
    this->paintBrush->eRadius = ival;
}

//

void TerrainTools::setBrushTextureId(int val){
    const auto texture=TexLib::mtex.find(val);
    if (val<0 || texture==TexLib::mtex.end() || !texture->second) return;
    TexLib::addRef(val);
    if (selectedTextureRef>=0) TexLib::delRef(selectedTextureRef);
    selectedTextureRef=val;
    paintBrush->texId=val;
    paintBrush->tex=texture->second;
    paintBrush->terrainMaterialUid=0;
    paintBrush->terrainMaterialRoute.clear();
    paintBrush->terrainShaderKey.clear();
    paintBrush->terrainShaderSource.reset();
    paintBrush->terrainShaderIsBake=false;
    paintBrush->terrainShaderTextureId=-1;
    paintBrush->terrainPickedShaderId=-1;
    paintBrush->terrainShaderTile.clear();
    emit setPaintBrush(this->paintBrush);
}

void TerrainTools::rememberCurrentMaterial() {
    if (!paintBrush->tex || paintBrush->texId<0) return;
    RecentMaterial entry;
    entry.textureId=paintBrush->texId; entry.texturePath=paintBrush->tex->pathid;
    entry.uid=paintBrush->terrainMaterialUid; entry.route=paintBrush->terrainMaterialRoute;
    entry.shaderKey=paintBrush->terrainShaderKey; entry.shaderSource=paintBrush->terrainShaderSource;
    entry.isBake=paintBrush->terrainShaderIsBake; entry.pickedShaderId=paintBrush->terrainPickedShaderId;
    entry.shaderTile=paintBrush->terrainShaderTile;
    TexLib::addRef(entry.textureId);
    for (int i=texLastItems.size()-1;i>=0;--i) {
        const auto &old=texLastItems[i];
        const bool same=entry.uid ? old.uid==entry.uid && old.route==entry.route
            : !old.uid && old.texturePath==entry.texturePath && old.shaderKey==entry.shaderKey
              && (old.shaderSource ? old.shaderSource->key() : QString())
                   ==(entry.shaderSource ? entry.shaderSource->key() : QString());
        if (same) { TexLib::delRef(old.textureId); texLastItems.removeAt(i); }
    }
    texLastItems.push_back(entry);
    while (texLastItems.size()>6) { TexLib::delRef(texLastItems.first().textureId); texLastItems.removeFirst(); }
    updateTexPrev();
}

void TerrainTools::updateTexPrev(){
    bool pending=false;
    auto draw=[&](ClickableLabel *label,int textureId,quint32 uid,const QString &route,int size) {
        QPixmap preview(size,size); preview.fill(Qt::gray);
        const auto found=TexLib::mtex.find(textureId);
        Texture *texture=found==TexLib::mtex.end()?nullptr:found->second;
        if (texture && texture->loaded) {
            std::unique_ptr<unsigned char[]> pixels(texture->getImageData(size,size));
            if (pixels && (texture->bytesPerPixel==3 || texture->bytesPerPixel==4))
                preview=QPixmap::fromImage(QImage(pixels.get(),size,size,
                    texture->bytesPerPixel==3?QImage::Format_RGB888:QImage::Format_RGBA8888));
        } else if (texture && !texture->missing && !texture->error) pending=true;
        QString description=texture ? QFileInfo(texture->pathid).fileName() : "Unavailable texture";
        if (uid) {
            const auto library=TerrainMaterialLibrary::current();
            const auto material=route==library->path()?library->find(uid):nullptr;
            description=QString("Procedural: %1 (UiD %2)").arg(material?material->displayName:"unavailable material").arg(uid);
        } else description="Static / local shader: "+description;
        QPainter painter(&preview);
        painter.fillRect(0,0,18,18,uid?QColor(25,85,155):QColor(55,55,55));
        painter.setPen(Qt::white); painter.drawText(QRect(0,0,18,18),Qt::AlignCenter,uid?"P":"S");
        painter.end(); label->setPixmap(preview); label->setToolTip(description);
    };
    if (paintBrush->tex) draw(texPreviewLabel,paintBrush->texId,paintBrush->terrainMaterialUid,paintBrush->terrainMaterialRoute,192);
    for (int i=0;i<6;++i) {
        const int index=texLastItems.size()-i-1;
        texPreviewLabels[i]->setEnabled(index>=0);
        if (index<0) { texPreviewLabels[i]->setPixmap(*defaultTexPreview); texPreviewLabels[i]->setToolTip("No recent material"); continue; }
        const auto &entry=texLastItems[index];
        draw(texPreviewLabels[i],entry.textureId,entry.uid,entry.route,64);
    }
    // Slot 6 is exclusively the brush shape; never paint a seventh history image over it.
    if (pending && !previewRetryScheduled) {
        previewRetryScheduled=true;
        QTimer::singleShot(200,this,[this] { previewRetryScheduled=false; updateTexPrev(); });
    }
}

void TerrainTools::texPreviewEnabled(int val){
    if (val==6) { nextBrushShape(); return; }
    if (val<0 || val>=6 || val>=texLastItems.size()) return;
    const auto entry=texLastItems[texLastItems.size()-val-1];
    if (entry.uid) {
        const auto library=TerrainMaterialLibrary::current(); library->poll();
        const auto material=entry.route==library->path()?library->find(entry.uid):nullptr;
        if (!material) { QMessageBox::warning(this,"Recent terrain material","This procedural material is not available in the current route. Use Choose to select another material."); return; }
        const int textureId=TexLib::addTex(library->textureDirectory(),material->texture);
        setBrushTextureId(textureId); TexLib::delRef(textureId);
        paintBrush->terrainMaterialUid=entry.uid; paintBrush->terrainMaterialRoute=entry.route;
    } else {
        setBrushTextureId(entry.textureId);
        paintBrush->terrainShaderKey=entry.shaderKey; paintBrush->terrainShaderSource=entry.shaderSource;
        paintBrush->terrainShaderIsBake=entry.isBake; paintBrush->terrainPickedShaderId=entry.pickedShaderId;
        paintBrush->terrainShaderTextureId=entry.textureId; paintBrush->terrainShaderTile=entry.shaderTile;
    }
    paintBrush->useTexture=true;
    rememberCurrentMaterial();
    emit setPaintBrush(paintBrush);
}

void TerrainTools::msg(QString text, QString val){
    if(text == "toolEnabled"){
        QMapIterator<QString, QPushButton*> i(buttonTools);
        while (i.hasNext()) {
            i.next();
            if(i.value() == NULL)
                continue;
            i.value()->blockSignals(true);
            i.value()->setChecked(false);
        }
        if(buttonTools[val] != NULL)
            buttonTools[val]->setChecked(true);
        if (val=="pickTerrainTexTool" && buttonTools.value("proceduralPickTool"))
            buttonTools["proceduralPickTool"]->setChecked(true);
        if (val=="lockTexTool" && buttonTools.value("proceduralLockTool"))
            buttonTools["proceduralLockTool"]->setChecked(true);
        i.toFront();
        while (i.hasNext()) {
            i.next();
            if(i.value() == NULL)
                continue;
            i.value()->blockSignals(false);
        }
    } else if(text == "brushDirection"){
        QString t = buttonTools["heightTool"]->text().left(buttonTools["heightTool"]->text().length() - 1);
        buttonTools["heightTool"]->setText(t+val);
        t = buttonTools["waterTerrTool"]->text().left(buttonTools["waterTerrTool"]->text().length() - 1);
        buttonTools["waterTerrTool"]->setText(t+val);
        t = buttonTools["gapsTerrainTool"]->text().left(buttonTools["gapsTerrainTool"]->text().length() - 1);
        buttonTools["gapsTerrainTool"]->setText(t+val);
    }
}
