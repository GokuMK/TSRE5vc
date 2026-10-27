/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <shapeViewer/ShapeMaterialsWindow.h>
#include <shapeViewer/ShapeViewerGLWidget.h>
#include <tsre/shape/MstsMaterials.h>
#include <cmath>

namespace {

// MSTS shader names (Open Rails' meaning) and TSRE's PBR ones.
const QStringList &shaderNames() {
    static const QStringList names = {"TexDiff", "BlendATexDiff", "AddATexDiff", "Tex", "BlendATex", "AddATex",
                                      "PbrTexDiff", "BlendAPbrTexDiff"};
    return names;
}

QString shaderDescription(const QString &name) {
    const QString n = name.toLower();
    if (n == "texdiff")
        //% "The texture, lit by the sun and the sky; opaque (its alpha cuts holes only with Alpha test)."
        return qtTrId("shape.materials.shader.texdiff");
    if (n == "blendatexdiff")
        //% "The texture blended by its alpha, lit: glass and other see-through parts."
        return qtTrId("shape.materials.shader.blendatexdiff");
    if (n == "addatexdiff")
        //% "The texture added to what lies behind, lit: glows."
        return qtTrId("shape.materials.shader.addatexdiff");
    if (n == "tex")
        //% "The texture, not lit (as bright as it is painted)."
        return qtTrId("shape.materials.shader.tex");
    if (n == "blendatex")
        //% "The texture blended by its alpha, not lit."
        return qtTrId("shape.materials.shader.blendatex");
    if (n == "addatex")
        //% "The texture added to what lies behind, not lit."
        return qtTrId("shape.materials.shader.addatex");
    if (n == "pbrtexdiff")
        //% "TSRE PBR: base colour, ORM(C) and normal map textures, reflections of the sky and surroundings, the factors below; opaque (its alpha cuts holes with Alpha test). MSTS and Open Rails draw the first texture only."
        return qtTrId("shape.materials.shader.pbrtexdiff");
    if (n == "blendapbrtexdiff")
        //% "TSRE PBR glass: blended by the base colour's alpha, its reflection whole on top (clear head-on, a mirror at grazing angles)."
        return qtTrId("shape.materials.shader.blendapbrtexdiff");
    //% "A shader name TSRE does not know: drawn as TexDiff, or blended when it is not TexDiff."
    return qtTrId("shape.materials.shader.other");
}

// MSTS light materials, -5 to -12.
QString lightName(int light) {
    switch (light) {
    //% "-5 Normal"
    case -5: return qtTrId("shape.materials.light.normal");
    //% "-6 Specular 25"
    case -6: return qtTrId("shape.materials.light.specular25");
    //% "-7 Specular 750"
    case -7: return qtTrId("shape.materials.light.specular750");
    //% "-8 Full bright"
    case -8: return qtTrId("shape.materials.light.fullbright");
    //% "-9 Cruciform"
    case -9: return qtTrId("shape.materials.light.cruciform");
    //% "-10 Cruciform (long)"
    case -10: return qtTrId("shape.materials.light.cruciform.long");
    //% "-11 Half bright"
    case -11: return qtTrId("shape.materials.light.halfbright");
    //% "-12 Dark shade"
    case -12: return qtTrId("shape.materials.light.darkshade");
    default: return QString::number(light);
    }
}

QString lightDescription(int light) {
    switch (light) {
    //% "Lit by the sun and the sky."
    case -5: return qtTrId("shape.materials.light.normal.help");
    //% "Lit, with a broad sheen of the sun: painted bodies, rail heads."
    case -6: return qtTrId("shape.materials.light.specular25.help");
    //% "Lit, with a tight glint of the sun: chrome, polished metal, glass."
    case -7: return qtTrId("shape.materials.light.specular750.help");
    //% "Not lit: as bright as the texture (lamps, signs)."
    case -8: return qtTrId("shape.materials.light.fullbright.help");
    //% "Vegetation, lit alike from every side (trees, bushes)."
    case -9: return qtTrId("shape.materials.light.cruciform.help");
    //% "Vegetation as -9 (hedges)."
    case -10: return qtTrId("shape.materials.light.cruciform.long.help");
    //% "Half lit, half as bright as the texture."
    case -11: return qtTrId("shape.materials.light.halfbright.help");
    //% "Lit at half: shaded parts, interiors."
    case -12: return qtTrId("shape.materials.light.darkshade.help");
    //% "A light material of the shape's own (a light_material): drawn as -5."
    default: return qtTrId("shape.materials.light.other.help");
    }
}

QLabel *help(const QString &text) {
    auto *label = new QLabel(text);
    label->setWordWrap(true);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    QPalette palette = label->palette();
    palette.setColor(QPalette::WindowText, palette.color(QPalette::Disabled, QPalette::WindowText));
    label->setPalette(palette);
    return label;
}

QDoubleSpinBox *spin(double low, double high, double step) {
    auto *box = new QDoubleSpinBox;
    box->setRange(low, high);
    box->setSingleStep(step);
    box->setDecimals(3);
    return box;
}

// A material made PBR starts with its baked occlusion at this strength: the
// view's ambient occlusion (GTAO) darkens on top of it, and creases both see
// would darken twice.
constexpr double NewPbrOcclusion = 0.6;

constexpr qint64 UndoBytes = 256ll * 1024 * 1024;
constexpr size_t UndoSteps = 20;

} // namespace

ShapeMaterialsWindow::ShapeMaterialsWindow(QWidget *parent, ShapeViewerGLWidget *view)
    : QWidget(parent), view(view) {
    setWindowFlags(Qt::WindowType::Tool);
    setWindowTitle(
        //% "Shape Materials"
        qtTrId("shape.materials.title"));

    note = new QLabel;
    note->setWordWrap(true);

    // The materials.
    list = new QTreeWidget;
    list->setObjectName("materialsList");
    list->setRootIsDecorated(false);
    list->setHeaderLabels({"#",
                           //% "Name"
                           qtTrId("shape.materials.column.name"),
                           //% "Shader"
                           qtTrId("shape.materials.column.shader"),
                           //% "Texture"
                           qtTrId("shape.materials.column.texture"),
                           //% "Lighting"
                           qtTrId("shape.materials.column.light"),
                           //% "Matrix"
                           qtTrId("shape.materials.column.matrix"),
                           //% "Parts"
                           qtTrId("shape.materials.column.parts")});
    list->header()->resizeSection(0, 36);
    list->header()->resizeSection(1, 120);
    list->header()->resizeSection(2, 110);
    list->header()->resizeSection(3, 170);
    connect(list, &QTreeWidget::currentItemChanged, this, [this]() { showMaterial(currentMaterial()); });

    //% "Add a copy"
    addButton = new QPushButton(qtTrId("shape.materials.add"));
    //% "Remove"
    removeButton = new QPushButton(qtTrId("shape.materials.remove"));
    //% "Undo"
    undoButton = new QPushButton(qtTrId("shape.materials.undo"));
    undoButton->setObjectName("materialsUndo");
    connect(addButton, &QPushButton::clicked, this, &ShapeMaterialsWindow::add);
    connect(removeButton, &QPushButton::clicked, this, &ShapeMaterialsWindow::remove);
    connect(undoButton, &QPushButton::clicked, this, &ShapeMaterialsWindow::undo);
    auto *listButtons = new QHBoxLayout;
    listButtons->addWidget(addButton);
    listButtons->addWidget(removeButton);
    listButtons->addStretch();
    listButtons->addWidget(undoButton);

    // The parts.
    //% "Pick parts in the 3D view (Ctrl adds)"
    pickParts = new QCheckBox(qtTrId("shape.materials.pick"));
    connect(pickParts, &QCheckBox::toggled, this, [this](bool on) {
        if (this->view)
            this->view->setPrimitivePicking(on && isVisible());
    });
    pickedLabel = new QLabel;
    pickedLabel->setWordWrap(true);
    //% "Parts of this material"
    selectPartsButton = new QPushButton(qtTrId("shape.materials.select.parts"));
    //% "Give the picked parts this material"
    assignButton = new QPushButton(qtTrId("shape.materials.assign"));
    //% "Clear"
    clearPickButton = new QPushButton(qtTrId("shape.materials.clear.pick"));
    //% "In all distance levels"
    allLevels = new QCheckBox(qtTrId("shape.materials.all.levels"));
    allLevels->setChecked(true);
    allLevels->setToolTip(
        //% "The other distance levels' parts of the same matrix and material get it too."
        qtTrId("shape.materials.all.levels.help"));
    connect(selectPartsButton, &QPushButton::clicked, this, &ShapeMaterialsWindow::selectParts);
    connect(assignButton, &QPushButton::clicked, this, &ShapeMaterialsWindow::assign);
    connect(clearPickButton, &QPushButton::clicked, this, [this]() {
        picked.clear();
        updatePicked();
    });
    auto *partButtons = new QHBoxLayout;
    partButtons->addWidget(selectPartsButton);
    partButtons->addWidget(clearPickButton);
    partButtons->addStretch();
    auto *assignRow = new QHBoxLayout;
    assignRow->addWidget(assignButton);
    assignRow->addWidget(allLevels);
    assignRow->addStretch();

    auto *left = new QVBoxLayout;
    left->addWidget(note);
    left->addWidget(list, 1);
    left->addLayout(listButtons);
    left->addSpacing(6);
    left->addWidget(pickParts);
    left->addWidget(pickedLabel);
    left->addLayout(partButtons);
    left->addLayout(assignRow);

    // The selected material.
    form = new QWidget;
    auto *fields = new QFormLayout(form);
    fields->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    nameEdit = new QLineEdit;
    //% "Name"
    fields->addRow(qtTrId("shape.materials.field.name"), nameEdit);
    fields->addRow(QString(), help(
        //% "The primitive state's name; exporters name them after their matrix and look."
        qtTrId("shape.materials.field.name.help")));
    shaderCombo = new QComboBox;
    shaderCombo->setObjectName("materialsShader");
    shaderCombo->setEditable(true);
    shaderCombo->addItems(shaderNames());
    connect(shaderCombo, &QComboBox::currentTextChanged, this, [this](const QString &name) {
        // Made PBR here: the baked occlusion at its starting strength.
        if (MstsMaterials::pbr(MstsMaterials::shader(name)) && !MstsMaterials::pbr(MstsMaterials::shader(shownShader)))
            occlusionSpin->setValue(NewPbrOcclusion);
        updateShaderHelp();
    });
    //% "Shader"
    fields->addRow(qtTrId("shape.materials.field.shader"), shaderCombo);
    shaderHelp = help(QString());
    fields->addRow(QString(), shaderHelp);
    const QString slotNames[3] = {
        //% "Texture 1"
        qtTrId("shape.materials.field.texture1"),
        //% "Texture 2"
        qtTrId("shape.materials.field.texture2"),
        //% "Texture 3"
        qtTrId("shape.materials.field.texture3")};
    const QString slotHelp[3] = {
        //% "The base colour (its alpha as the shader uses it). MSTS and Open Rails draw this one."
        qtTrId("shape.materials.field.texture1.help"),
        //% "PBR: ORM(C), R occlusion, G roughness (black a mirror, white matte), B metalness, A a clearcoat mask (optional). Pack makes it from separate grayscale maps."
        qtTrId("shape.materials.field.texture2.help"),
        //% "PBR: the normal map, tangent space, OpenGL (green up); flat is 128,128,255."
        qtTrId("shape.materials.field.texture3.help")};
    for (int slot = 0; slot < 3; ++slot) {
        textureEdits[slot] = new QLineEdit;
        auto *browse = new QPushButton(QStringLiteral("..."));
        browse->setFixedWidth(32);
        connect(browse, &QPushButton::clicked, this, [this, slot]() { chooseTexture(slot); });
        auto *row = new QHBoxLayout;
        row->addWidget(textureEdits[slot], 1);
        row->addWidget(browse);
        if (slot == 1) {
            //% "Pack..."
            auto *pack = new QPushButton(qtTrId("shape.materials.pack"));
            connect(pack, &QPushButton::clicked, this, &ShapeMaterialsWindow::packOrm);
            row->addWidget(pack);
        }
        textureLabels[slot] = new QLabel(slotNames[slot]);
        fields->addRow(textureLabels[slot], row);
        textureHelps[slot] = help(slotHelp[slot]);
        fields->addRow(QString(), textureHelps[slot]);
    }
    lightCombo = new QComboBox;
    for (int light = -5; light >= -12; --light)
        lightCombo->addItem(lightName(light), light);
    connect(lightCombo, &QComboBox::currentIndexChanged, this, [this]() { updateLightHelp(); });
    //% "Lighting"
    fields->addRow(qtTrId("shape.materials.field.light"), lightCombo);
    lightHelp = help(QString());
    fields->addRow(QString(), lightHelp);
    //% "Alpha test"
    alphaTestCheck = new QCheckBox(qtTrId("shape.materials.field.alpha.test"));
    fields->addRow(QString(), alphaTestCheck);
    fields->addRow(QString(), help(
        //% "Texels with alpha below half are cut out (leaves, fences); the rest drawn opaque."
        qtTrId("shape.materials.field.alpha.test.help")));
    zBiasSpin = spin(-10.0, 10.0, 0.001);
    //% "Z-bias"
    fields->addRow(qtTrId("shape.materials.field.zbias"), zBiasSpin);
    fields->addRow(QString(), help(
        //% "Draws the part a little nearer, over others at the same depth (decals, markings)."
        qtTrId("shape.materials.field.zbias.help")));

    //% "PBR factors (in the shape's .sd, by texture 1)"
    pbrGroup = new QGroupBox(qtTrId("shape.materials.pbr"));
    auto *pbr = new QFormLayout(pbrGroup);
    pbr->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    tintButton = new QPushButton;
    tintButton->setFixedWidth(60);
    connect(tintButton, &QPushButton::clicked, this, [this]() {
        const QColor colour = QColorDialog::getColor(tint, this);
        if (colour.isValid())
            setTint(colour);
    });
    tintAlpha = spin(0.0, 1.0, 0.05);
    auto *tintRow = new QHBoxLayout;
    tintRow->addWidget(tintButton);
    //% "alpha"
    tintRow->addWidget(new QLabel(qtTrId("shape.materials.pbr.alpha")));
    tintRow->addWidget(tintAlpha);
    tintRow->addStretch();
    //% "Tint"
    pbr->addRow(qtTrId("shape.materials.pbr.tint"), tintRow);
    pbr->addRow(QString(), help(
        //% "Multiplies the base colour and its alpha."
        qtTrId("shape.materials.pbr.tint.help")));
    auto addFactor = [&](QDoubleSpinBox *&box, double high, const QString &name, const QString &text) {
        box = spin(0.0, high, 0.05);
        pbr->addRow(name, box);
        pbr->addRow(QString(), help(text));
    };
    addFactor(metalnessSpin, 1.0,
              //% "Metalness"
              qtTrId("shape.materials.pbr.metalness"),
              //% "Times the ORM's blue (without an ORM: the value). 0 paint, glass, rubber; 1 bare metal."
              qtTrId("shape.materials.pbr.metalness.help"));
    addFactor(roughnessSpin, 1.0,
              //% "Roughness"
              qtTrId("shape.materials.pbr.roughness"),
              //% "Times the ORM's green (without an ORM: the value). 0 a mirror, 1 matte."
              qtTrId("shape.materials.pbr.roughness.help"));
    addFactor(normalSpin, 4.0,
              //% "Normal strength"
              qtTrId("shape.materials.pbr.normal"),
              //% "How deep the normal map's relief looks."
              qtTrId("shape.materials.pbr.normal.help"));
    addFactor(occlusionSpin, 1.0,
              //% "Occlusion"
              qtTrId("shape.materials.pbr.occlusion"),
              //% "How much the ORM's red (baked dirt and creases) darkens the light from the sky and surroundings. The view's ambient occlusion darkens on top of it, so a material made PBR here starts at 0.6."
              qtTrId("shape.materials.pbr.occlusion.help"));
    addFactor(clearcoatSpin, 1.0,
              //% "Clearcoat"
              qtTrId("shape.materials.pbr.clearcoat"),
              //% "A clear varnish over the base (times the ORM's alpha) with its own sharp reflection: painted bodies."
              qtTrId("shape.materials.pbr.clearcoat.help"));
    addFactor(clearcoatRoughnessSpin, 1.0,
              //% "Clearcoat roughness"
              qtTrId("shape.materials.pbr.clearcoat.roughness"),
              //% "The varnish's roughness: 0 a mirror."
              qtTrId("shape.materials.pbr.clearcoat.roughness.help"));
    addFactor(transmissionSpin, 1.0,
              //% "Transmission"
              qtTrId("shape.materials.pbr.transmission"),
              //% "Light passing through: glass that tints and blurs what lies behind it."
              qtTrId("shape.materials.pbr.transmission.help"));
    fields->addRow(pbrGroup);

    //% "Apply"
    applyButton = new QPushButton(qtTrId("shape.materials.apply"));
    applyButton->setObjectName("materialsApply");
    //% "Revert"
    revertButton = new QPushButton(qtTrId("shape.materials.revert"));
    connect(applyButton, &QPushButton::clicked, this, &ShapeMaterialsWindow::apply);
    connect(revertButton, &QPushButton::clicked, this, [this]() { showMaterial(currentMaterial()); });
    auto *formButtons = new QHBoxLayout;
    formButtons->addStretch();
    formButtons->addWidget(revertButton);
    formButtons->addWidget(applyButton);
    fields->addRow(formButtons);

    auto *scroll = new QScrollArea;
    scroll->setWidget(form);
    scroll->setWidgetResizable(true);
    scroll->setMinimumWidth(420);

    auto *leftWidget = new QWidget;
    leftWidget->setLayout(left);
    auto *splitter = new QSplitter;
    splitter->addWidget(leftWidget);
    splitter->addWidget(scroll);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 1);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->addWidget(splitter);
    resize(1100, 640);

    if (view)
        connect(view, &ShapeViewerGLWidget::primitivePicked, this, &ShapeMaterialsWindow::primitivePicked);
    setShape(nullptr, QString());
}

void ShapeMaterialsWindow::showEvent(QShowEvent *event) {
    QWidget::showEvent(event);
    if (view)
        view->setPrimitivePicking(pickParts->isChecked());
    refreshList(currentMaterial());
}

void ShapeMaterialsWindow::hideEvent(QHideEvent *event) {
    QWidget::hideEvent(event);
    if (view)
        view->setPrimitivePicking(false);
}

void ShapeMaterialsWindow::setShape(ComplexShape *shown, const QString &directory) {
    shape = dynamic_cast<SFileComplex *>(shown);
    textureDirectory = directory;
    undoStack.clear();
    picked.clear();
    if (view)
        view->setPickedPrimitives(picked);
    refreshList(0);
}

int ShapeMaterialsWindow::currentMaterial() const {
    QTreeWidgetItem *item = list->currentItem();
    return item ? item->data(0, Qt::UserRole).toInt() : -1;
}

void ShapeMaterialsWindow::refreshList(int select) {
    const bool loaded = shape != nullptr && shape->isLoaded();
    QString error;
    const bool editable = loaded && shape->materialCount() > 0;
    if (shape == nullptr)
        //% "Open an MSTS shape (.s) to edit its materials."
        note->setText(qtTrId("shape.materials.note.no.shape"));
    else if (!loaded) {
        //% "Loading..."
        note->setText(qtTrId("shape.materials.note.loading"));
        // Again when it has loaded.
        QPointer<ShapeMaterialsWindow> self(this);
        QTimer::singleShot(300, this, [self, select]() {
            if (self && self->isVisible())
                self->refreshList(select);
        });
    } else
        note->clear();
    note->setVisible(!note->text().isEmpty());
    const QSignalBlocker blocker(list);
    list->clear();
    for (int m = 0; editable && m < shape->materialCount(); ++m) {
        SFileComplex::MaterialInfo info;
        if (!shape->materialInfo(m, info))
            continue;
        auto *item = new QTreeWidgetItem(list);
        item->setData(0, Qt::UserRole, m);
        item->setText(0, QString::number(m));
        item->setText(1, info.name);
        item->setText(2, info.shader);
        item->setText(3, info.textures.isEmpty() ? QString() : info.textures.join(", "));
        item->setText(4, lightName(info.lightMaterial));
        item->setText(5, info.matrixName);
        item->setText(6, QString::number(info.uses));
    }
    for (QWidget *w : std::initializer_list<QWidget *>{addButton, removeButton, pickParts, selectPartsButton,
                                                       assignButton, clearPickButton, allLevels, form})
        w->setEnabled(editable);
    undoButton->setEnabled(editable && !undoStack.empty());
    if (editable) {
        const int count = list->topLevelItemCount();
        list->setCurrentItem(list->topLevelItem(std::clamp(select, 0, std::max(count - 1, 0))));
    }
    showMaterial(currentMaterial());
    updatePicked();
}

void ShapeMaterialsWindow::showMaterial(int material) {
    SFileComplex::MaterialInfo info;
    if (shape == nullptr || material < 0 || !shape->materialInfo(material, info))
        info = SFileComplex::MaterialInfo();
    nameEdit->setText(info.name);
    const QSignalBlocker blocker(shaderCombo);
    shownShader = info.shader;
    shaderCombo->setCurrentText(info.shader);
    for (int slot = 0; slot < 3; ++slot)
        textureEdits[slot]->setText(slot < info.textures.size() ? info.textures[slot] : QString());
    const int lightIndex = lightCombo->findData(info.lightMaterial);
    if (lightIndex < 0) {
        lightCombo->addItem(lightName(info.lightMaterial), info.lightMaterial);
        lightCombo->setCurrentIndex(lightCombo->count() - 1);
    } else
        lightCombo->setCurrentIndex(lightIndex);
    alphaTestCheck->setChecked(info.alphaTest);
    zBiasSpin->setValue(info.zBias);
    const bool orm = info.textures.size() > 1 && !info.textures[1].isEmpty();
    const MstsMaterials::Factors &f = info.factors;
    setTint(QColor::fromRgbF(f.tint[0], f.tint[1], f.tint[2]));
    tintAlpha->setValue(f.tint[3]);
    metalnessSpin->setValue(f.metalnessFor(orm));
    roughnessSpin->setValue(f.roughnessFor(orm));
    normalSpin->setValue(f.normal);
    occlusionSpin->setValue(f.occlusion);
    clearcoatSpin->setValue(f.clearcoat);
    clearcoatRoughnessSpin->setValue(f.clearcoatRoughness);
    transmissionSpin->setValue(f.transmission);
    updateShaderHelp();
    updateLightHelp();
}

void ShapeMaterialsWindow::updateShaderHelp() {
    const QString name = shaderCombo->currentText().trimmed();
    shaderHelp->setText(shaderDescription(name));
    const bool pbr = MstsMaterials::pbr(MstsMaterials::shader(name));
    pbrGroup->setVisible(pbr);
    for (int slot = 1; slot < 3; ++slot)
        textureHelps[slot]->setVisible(pbr);
}

void ShapeMaterialsWindow::updateLightHelp() {
    lightHelp->setText(lightDescription(lightCombo->currentData().toInt()));
}

void ShapeMaterialsWindow::setTint(const QColor &colour) {
    tint = colour;
    tintButton->setStyleSheet(QStringLiteral("background-color: %1").arg(colour.name()));
}

SFileComplex::MaterialInfo ShapeMaterialsWindow::formInfo() const {
    SFileComplex::MaterialInfo info;
    info.name = nameEdit->text().trimmed();
    info.shader = shaderCombo->currentText().trimmed();
    for (int slot = 0; slot < 3; ++slot)
        info.textures << textureEdits[slot]->text().trimmed();
    info.lightMaterial = lightCombo->currentData().toInt();
    info.alphaTest = alphaTestCheck->isChecked();
    info.zBias = float(zBiasSpin->value());
    MstsMaterials::Factors &f = info.factors;
    f.tint[0] = float(tint.redF());
    f.tint[1] = float(tint.greenF());
    f.tint[2] = float(tint.blueF());
    f.tint[3] = float(tintAlpha->value());
    f.metalness = float(metalnessSpin->value());
    f.roughness = float(roughnessSpin->value());
    f.normal = float(normalSpin->value());
    f.occlusion = float(occlusionSpin->value());
    f.clearcoat = float(clearcoatSpin->value());
    f.clearcoatRoughness = float(clearcoatRoughnessSpin->value());
    f.transmission = float(transmissionSpin->value());
    return info;
}

void ShapeMaterialsWindow::remember() {
    undoStack.push_back(shape->snapshot());
    qint64 bytes = 0;
    for (const auto &s : undoStack)
        bytes += SFileComplex::snapshotBytes(*s);
    while (undoStack.size() > 1 && (undoStack.size() > UndoSteps || bytes > UndoBytes)) {
        bytes -= SFileComplex::snapshotBytes(*undoStack.front());
        undoStack.pop_front();
    }
}

void ShapeMaterialsWindow::editDone(bool ok, const QString &error, int select) {
    if (!ok) {
        if (!undoStack.empty())
            undoStack.pop_back();
        // The comment straight before qtTrId: lupdate misses it inside ?:.
        QString text = error;
        if (text.isEmpty())
            //% "The material could not be changed."
            text = qtTrId("shape.materials.error.edit");
        QMessageBox::warning(this, windowTitle(), text);
    } else {
        emit edited();
    }
    if (view)
        view->update();
    refreshList(select < 0 ? currentMaterial() : select);
}

void ShapeMaterialsWindow::apply() {
    const int material = currentMaterial();
    if (shape == nullptr || material < 0)
        return;
    remember();
    QString error;
    const bool ok = shape->setMaterial(material, formInfo(), &error);
    editDone(ok, error, material);
}

void ShapeMaterialsWindow::add() {
    const int material = currentMaterial();
    if (shape == nullptr || material < 0)
        return;
    remember();
    QString error;
    const int added = shape->addMaterial(material, &error);
    editDone(added >= 0, error, added);
}

void ShapeMaterialsWindow::remove() {
    const int material = currentMaterial();
    if (shape == nullptr || material < 0)
        return;
    remember();
    QString error;
    const bool ok = shape->removeMaterial(material, &error);
    editDone(ok, error, std::max(material - 1, 0));
}

void ShapeMaterialsWindow::undo() {
    if (shape == nullptr || undoStack.empty())
        return;
    const auto last = undoStack.back();
    undoStack.pop_back();
    QString error;
    const bool ok = shape->restore(*last, &error);
    if (!ok)
        QMessageBox::warning(this, windowTitle(), error);
    else
        emit edited();
    if (view)
        view->update();
    refreshList(currentMaterial());
}

void ShapeMaterialsWindow::primitivePicked(int primitive, bool addTo) {
    if (shape == nullptr)
        return;
    if (!addTo)
        picked.clear();
    if (primitive >= 0) {
        if (addTo && picked.contains(primitive))
            picked.removeAll(primitive);
        else
            picked << primitive;
        // Its material in the list.
        const int material = shape->drawnPrimitiveMaterial(0, primitive);
        for (int i = 0; i < list->topLevelItemCount(); ++i)
            if (list->topLevelItem(i)->data(0, Qt::UserRole).toInt() == material && !addTo)
                list->setCurrentItem(list->topLevelItem(i));
    }
    updatePicked();
}

void ShapeMaterialsWindow::updatePicked() {
    if (view)
        view->setPickedPrimitives(picked);
    if (picked.isEmpty() || shape == nullptr) {
        //% "No parts picked."
        pickedLabel->setText(qtTrId("shape.materials.picked.none"));
        assignButton->setEnabled(false);
        return;
    }
    QStringList materials;
    for (int p : std::as_const(picked)) {
        const QString m = QString::number(shape->drawnPrimitiveMaterial(0, p));
        if (!materials.contains(m))
            materials << m;
    }
    //% "Picked: %1 parts, materials %2."
    pickedLabel->setText(qtTrId("shape.materials.picked").arg(picked.size()).arg(materials.join(", ")));
    assignButton->setEnabled(currentMaterial() >= 0);
}

void ShapeMaterialsWindow::selectParts() {
    const int material = currentMaterial();
    if (shape == nullptr || material < 0)
        return;
    picked = shape->primitivesWithMaterial(shape->drawnLevel(0), material);
    updatePicked();
}

void ShapeMaterialsWindow::assign() {
    const int material = currentMaterial();
    if (shape == nullptr || material < 0 || picked.isEmpty())
        return;
    remember();
    QString error;
    const bool ok = shape->assignMaterial(shape->drawnLevel(0), picked, material, allLevels->isChecked(), &error);
    editDone(ok, error, material);
}

void ShapeMaterialsWindow::chooseTexture(int slot) {
    const QString file = QFileDialog::getOpenFileName(
            this,
            //% "Texture"
            qtTrId("shape.materials.texture.dialog"), textureDirectory,
            //% "Textures (*.ace *.dds *.png *.jpg *.jpeg *.bmp *.tga);;All files (*)"
            qtTrId("shape.materials.texture.filter"));
    if (file.isEmpty())
        return;
    const QFileInfo chosen(file);
    // A texture from elsewhere is copied beside the shape's.
    if (!textureDirectory.isEmpty()
            && QDir(chosen.absolutePath()) != QDir(textureDirectory)) {
        const QString target = QDir(textureDirectory).filePath(chosen.fileName());
        if (!QFileInfo::exists(target) && !QFile::copy(file, target)) {
            QMessageBox::warning(this, windowTitle(),
                                 //% "Cannot copy %1 to %2."
                                 qtTrId("shape.materials.error.copy").arg(file, textureDirectory));
            return;
        }
    }
    textureEdits[slot]->setText(chosen.fileName());
    updateShaderHelp();
}

void ShapeMaterialsWindow::packOrm() {
    QDialog dialog(this);
    dialog.setWindowTitle(
        //% "Pack an ORM(C) texture"
        qtTrId("shape.materials.pack.title"));
    auto *layout = new QFormLayout(&dialog);
    layout->addRow(help(
        //% "Grayscale maps, one a channel; a channel without a map takes the value beside it. The maps are scaled to the largest."
        qtTrId("shape.materials.pack.help")));
    const QString names[4] = {
        //% "Occlusion (R)"
        qtTrId("shape.materials.pack.occlusion"),
        //% "Roughness (G)"
        qtTrId("shape.materials.pack.roughness"),
        //% "Metalness (B)"
        qtTrId("shape.materials.pack.metalness"),
        //% "Clearcoat (A)"
        qtTrId("shape.materials.pack.clearcoat")};
    MstsMaterials::OrmSources sources;
    QLineEdit *files[4];
    QDoubleSpinBox *constants[4];
    for (int c = 0; c < 4; ++c) {
        files[c] = new QLineEdit;
        constants[c] = spin(0.0, 1.0, 0.05);
        constants[c]->setValue(sources.constants[c]);
        auto *browse = new QPushButton(QStringLiteral("..."));
        browse->setFixedWidth(32);
        QLineEdit *edit = files[c];
        connect(browse, &QPushButton::clicked, &dialog, [this, edit]() {
            const QString file = QFileDialog::getOpenFileName(this, qtTrId("shape.materials.texture.dialog"),
                                                              textureDirectory,
                                                              qtTrId("shape.materials.texture.filter"));
            if (!file.isEmpty())
                edit->setText(file);
        });
        auto *row = new QHBoxLayout;
        row->addWidget(files[c], 1);
        row->addWidget(browse);
        row->addWidget(constants[c]);
        layout->addRow(names[c], row);
    }
    auto *out = new QLineEdit;
    const QString base = QFileInfo(textureEdits[0]->text()).completeBaseName();
    out->setText((base.isEmpty() ? QStringLiteral("material") : base) + QStringLiteral("_orm.png"));
    //% "Texture file"
    layout->addRow(qtTrId("shape.materials.pack.out"), out);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addRow(buttons);
    dialog.resize(640, dialog.sizeHint().height());
    if (dialog.exec() != QDialog::Accepted)
        return;
    for (int c = 0; c < 4; ++c) {
        sources.files[c] = files[c]->text().trimmed();
        sources.constants[c] = float(constants[c]->value());
    }
    const QString name = QFileInfo(out->text().trimmed()).fileName();
    if (name.isEmpty())
        return;
    const QString target = QDir(textureDirectory).filePath(name);
    QString error;
    if (!MstsMaterials::packOrm(sources, target, &error)) {
        QMessageBox::warning(this, windowTitle(), error);
        return;
    }
    textureEdits[1]->setText(name);
    updateShaderHelp();
}
