/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef SHAPEMATERIALSWINDOW_H
#define SHAPEMATERIALSWINDOW_H

#include <QWidget>
#include <QtWidgets>
#include <deque>
#include <memory>
#include <tsre/shape/SFileComplex.h>

class ComplexShape;
class ShapeViewerGLWidget;

// The Shape Viewer's materials editor (task shapes 13): a shape's materials
// (its primitive states), each edited with a description beside every
// parameter (shader, textures, MSTS lighting, alpha test, z-bias, PBR
// factors), added, removed and given to the parts picked in the view.
// Edits draw at once and undo within the session; File > Save writes the
// shape and its .sd.
class ShapeMaterialsWindow : public QWidget {
    Q_OBJECT
public:
    ShapeMaterialsWindow(QWidget *parent, ShapeViewerGLWidget *view);
    // The shown shape; materials are edited for complete MSTS shapes.
    void setShape(ComplexShape *shape, const QString &textureDirectory);

public slots:
    void primitivePicked(int primitive, bool add);

signals:
    // The shape changed (its materials, textures or .sd).
    void edited();

protected:
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    ShapeViewerGLWidget *view;
    SFileComplex *shape = nullptr;
    QString textureDirectory;
    // The picked primitives of the drawn level.
    QVector<int> picked;
    // Undo: the shape's documents before each edit, newest last.
    std::deque<std::shared_ptr<const SFileComplex::Snapshot>> undoStack;

    QLabel *note;
    QTreeWidget *list;
    QPushButton *addButton, *removeButton, *undoButton;
    QCheckBox *pickParts, *allLevels;
    QLabel *pickedLabel;
    QPushButton *selectPartsButton, *assignButton, *clearPickButton;
    QWidget *form;
    QLineEdit *nameEdit;
    QComboBox *shaderCombo;
    QLabel *shaderHelp;
    QLineEdit *textureEdits[3];
    QLabel *textureLabels[3];
    QLabel *textureHelps[3];
    QComboBox *lightCombo;
    QLabel *lightHelp;
    QCheckBox *alphaTestCheck;
    QDoubleSpinBox *zBiasSpin;
    QGroupBox *pbrGroup;
    QPushButton *tintButton;
    QColor tint;
    QDoubleSpinBox *tintAlpha, *metalnessSpin, *roughnessSpin, *normalSpin, *occlusionSpin,
            *clearcoatSpin, *clearcoatRoughnessSpin, *transmissionSpin;
    QPushButton *applyButton, *revertButton;

    int currentMaterial() const;
    void refreshList(int select = -1);
    void showMaterial(int material);
    void updateShaderHelp();
    void updateLightHelp();
    void updatePicked();
    void setTint(const QColor &colour);
    SFileComplex::MaterialInfo formInfo() const;
    // Before an edit: the shape's documents, for undo.
    void remember();
    void editDone(bool ok, const QString &error, int select = -1);
    void chooseTexture(int slot);
    void packOrm();
    void apply();
    void add();
    void remove();
    void undo();
    void selectParts();
    void assign();
};

#endif // SHAPEMATERIALSWINDOW_H
