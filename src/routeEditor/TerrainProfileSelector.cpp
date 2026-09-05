/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <routeEditor/TerrainProfileSelector.h>

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QSignalBlocker>
#include <QStandardItemModel>
#include <QVBoxLayout>
#include <tsre/Game.h>

namespace {

QLabel *compatibilityLabel(QWidget *parent) {
    QLabel *label = new QLabel(parent);
    label->setWordWrap(true);
    return label;
}

}

TerrainProfileSelector::TerrainProfileSelector(QWidget *parent,
                                               bool includeExtremeProfile)
    : QWidget(parent),
      profiles(new QComboBox(this)),
      patches(new QComboBox(this)),
      profileDescription(new QLabel(this)),
      tsreName(new QLabel("TSRE current:", this)),
      msts18Name(new QLabel("MSTS Bin 1.8:", this)),
      msts19Name(new QLabel("MSTS Bin 1.9:", this)),
      ortsMasterName(new QLabel("ORTS master:", this)),
      ortsUnstableName(new QLabel("ORTS unstable:", this)),
      tsreStatus(compatibilityLabel(this)),
      msts18Status(compatibilityLabel(this)),
      msts19Status(compatibilityLabel(this)),
      ortsMasterStatus(compatibilityLabel(this)),
      ortsUnstableStatus(compatibilityLabel(this)) {
    msts19Name->setObjectName("msts19Name");
    msts19Status->setObjectName("msts19Status");
    profiles->setStyleSheet("combobox-popup: 0;");
    profiles->addItem(TerrainGridLayout::heightProfileName(
                              TerrainHeightProfile::Standard256x8),
                      static_cast<int>(TerrainHeightProfile::Standard256x8));
    profiles->addItem(TerrainGridLayout::heightProfileName(
                              TerrainHeightProfile::Low128x16),
                      static_cast<int>(TerrainHeightProfile::Low128x16));
    profiles->addItem(TerrainGridLayout::heightProfileName(
                              TerrainHeightProfile::High512x4),
                      static_cast<int>(TerrainHeightProfile::High512x4));
    profiles->addItem(TerrainGridLayout::heightProfileName(
                              TerrainHeightProfile::Ultra1024x2),
                      static_cast<int>(TerrainHeightProfile::Ultra1024x2));
    if (includeExtremeProfile) {
        profiles->addItem(TerrainGridLayout::heightProfileName(
                                  TerrainHeightProfile::Extreme2048x1),
                          static_cast<int>(TerrainHeightProfile::Extreme2048x1));
    }

    patches->setStyleSheet("combobox-popup: 0;");
    patches->addItem("4 x 4", 4);
    patches->addItem("8 x 8", 8);
    patches->addItem("16 x 16", 16);
    patches->addItem("32 x 32 (experimental)", 32);

    QFormLayout *selectionLayout = new QFormLayout;
    selectionLayout->addRow("Heightmap profile:", profiles);
    selectionLayout->addRow("Patches per side:", patches);

    profileDescription->setWordWrap(true);

    QGroupBox *compatibility = new QGroupBox("Compatibility", this);
    QGridLayout *compatibilityLayout = new QGridLayout(compatibility);
    compatibilityLayout->setColumnStretch(1, 1);
    compatibilityLayout->addWidget(tsreName, 0, 0);
    compatibilityLayout->addWidget(tsreStatus, 0, 1);
    compatibilityLayout->addWidget(msts18Name, 1, 0);
    compatibilityLayout->addWidget(msts18Status, 1, 1);
    compatibilityLayout->addWidget(msts19Name, 2, 0);
    compatibilityLayout->addWidget(msts19Status, 2, 1);
    compatibilityLayout->addWidget(ortsMasterName, 3, 0);
    compatibilityLayout->addWidget(ortsMasterStatus, 3, 1);
    compatibilityLayout->addWidget(ortsUnstableName, 4, 0);
    compatibilityLayout->addWidget(ortsUnstableStatus, 4, 1);

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addLayout(selectionLayout);
    layout->addWidget(profileDescription);
    layout->addWidget(compatibility);

    connect(profiles, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int) {
        updatePatchAvailability();
        updateDescription();
    });
    connect(patches, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int) { updateDescription(); });

    setSelection(TerrainHeightProfile::Standard256x8,
                 TerrainGridLayout::DefaultPatchesPerSide);
}

TerrainHeightProfile TerrainProfileSelector::selectedProfile() const {
    return static_cast<TerrainHeightProfile>(profiles->currentData().toInt());
}

int TerrainProfileSelector::selectedPatchCount() const {
    return patches->currentData().toInt();
}

void TerrainProfileSelector::setSelection(TerrainHeightProfile profile,
                                          int patchCount) {
    const QSignalBlocker profileBlocker(profiles);
    const QSignalBlocker patchBlocker(patches);
    const int profileIndex = profiles->findData(static_cast<int>(profile));
    profiles->setCurrentIndex(profileIndex >= 0 ? profileIndex : 0);
    updatePatchAvailability();

    int patchIndex = patches->findData(patchCount);
    QStandardItemModel *model = qobject_cast<QStandardItemModel *>(patches->model());
    if (model == nullptr) {
        patches->setCurrentIndex(patchIndex >= 0 ? patchIndex : 0);
        updateDescription();
        return;
    }
    if (patchIndex < 0 || !model->item(patchIndex)->isEnabled()) {
        patchIndex = patches->findData(TerrainGridLayout::DefaultPatchesPerSide);
        if (patchIndex < 0 || !model->item(patchIndex)->isEnabled()) {
            for (int i = 0; i < patches->count(); ++i) {
                if (model->item(i)->isEnabled()) {
                    patchIndex = i;
                    break;
                }
            }
        }
    }
    patches->setCurrentIndex(patchIndex);
    updateDescription();
}

bool TerrainProfileSelector::choose(QWidget *parent, const QString &title,
                                    TerrainHeightProfile &profile,
                                    int &patchCount) {
    QDialog dialog(parent);
    dialog.setWindowTitle(title);
    QVBoxLayout *layout = new QVBoxLayout(&dialog);
    TerrainProfileSelector *selector = new TerrainProfileSelector(&dialog);
    selector->setSelection(profile, patchCount);
    layout->addWidget(selector);

    QDialogButtonBox *buttons = new QDialogButtonBox(
            QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    QObject::connect(buttons, &QDialogButtonBox::accepted,
                     &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected,
                     &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    dialog.setFixedWidth(SelectionDialogWidth);

    if (dialog.exec() != QDialog::Accepted)
        return false;
    profile = selector->selectedProfile();
    patchCount = selector->selectedPatchCount();
    return true;
}

void TerrainProfileSelector::updatePatchAvailability() {
    QStandardItemModel *model = qobject_cast<QStandardItemModel *>(patches->model());
    if (model == nullptr)
        return;
    for (int i = 0; i < patches->count(); ++i) {
        const TerrainGridLayout layout = TerrainGridLayout::profile(
                selectedProfile(), patches->itemData(i).toInt());
        model->item(i)->setEnabled(layout.sampleCount != 0);
    }

    const int currentIndex = patches->currentIndex();
    if (currentIndex >= 0 && model->item(currentIndex)->isEnabled())
        return;
    const int defaultIndex = patches->findData(
            TerrainGridLayout::DefaultPatchesPerSide);
    if (defaultIndex >= 0 && model->item(defaultIndex)->isEnabled()) {
        patches->setCurrentIndex(defaultIndex);
        return;
    }
    for (int i = 0; i < patches->count(); ++i) {
        if (model->item(i)->isEnabled()) {
            patches->setCurrentIndex(i);
            return;
        }
    }
}

void TerrainProfileSelector::updateDescription() {
    const TerrainGridLayout layout = TerrainGridLayout::profile(
            selectedProfile(), selectedPatchCount());
    profileDescription->setText("<b>" + TerrainGridLayout::profileName(
            selectedProfile(), selectedPatchCount()).toHtmlEscaped() + "</b>");
    setCompatibility(tsreName, tsreStatus, tsreCompatibility(layout));
    setCompatibility(msts18Name, msts18Status, msts18Compatibility(layout));
    setCompatibility(msts19Name, msts19Status, msts19Compatibility(layout));
    setCompatibility(ortsMasterName, ortsMasterStatus,
                     ortsMasterCompatibility(layout));
    setCompatibility(ortsUnstableName, ortsUnstableStatus,
                     ortsUnstableCompatibility(layout));
}

void TerrainProfileSelector::setCompatibility(
        QLabel *name, QLabel *status, const QString &text) {
    status->setText(text);
    QString color = "#d6a000";
    if (text.startsWith("Supported but not Recommended"))
        color = "#d6a000";
    else if (text.startsWith("Supported"))
        color = Game::StyleGreenText;
    else if (text.startsWith("Not compatible"))
        color = Game::StyleRedText;
    name->setStyleSheet(QString("QLabel { color: %1; font-weight: bold; }")
                        .arg(color));
}

QString TerrainProfileSelector::tsreCompatibility(
        const TerrainGridLayout &layout) const {
    if (layout.sampleCount == 2048)
        return "Supported but not Recommended";
    const bool recommended =
            (layout.sampleCount == 256 && layout.patchesPerSide == 16)
            || (layout.sampleCount == 512 && layout.patchesPerSide == 16)
            || (layout.sampleCount == 1024 && layout.patchesPerSide == 16);
    return recommended ? "Supported and Recommended" : "Supported";
}

QString TerrainProfileSelector::msts18Compatibility(
        const TerrainGridLayout &layout) const {
    if (layout.sampleCount == 256 && layout.patchesPerSide == 16)
        return "Supported standard profile";
    if (layout.sampleCount == 128 && layout.patchesPerSide == 16)
        return "Supported; confirmed in MSTS Route Editor";
    if (layout.sampleCount <= 256 && layout.patchesPerSide <= 16
            && layout.patchResolution <= 16)
        return "Within recovered limits; this tuple is not runtime-tested";
    return "Not compatible (requires N<=256, P<=16 and R<=16)";
}

QString TerrainProfileSelector::msts19Compatibility(
        const TerrainGridLayout &layout) const {
    const bool confirmed = (layout.sampleCount == 256 && layout.patchesPerSide == 16)
            || (layout.sampleCount == 512 && layout.patchesPerSide == 16)
            || (layout.sampleCount == 512 && layout.patchesPerSide == 32)
            // Requires the R64 update; detailed test limits are documented.
            || (layout.sampleCount == 1024 && layout.patchesPerSide == 16)
            || (layout.sampleCount == 1024 && layout.patchesPerSide == 32);
    if (confirmed)
        return "Supported; runtime-confirmed";
    if (layout.sampleCount <= 1024 && layout.patchesPerSide <= 32
            && layout.patchResolution <= 64)
        return "Within patched limits; this tuple is not runtime-tested";
    return "Not compatible (requires N<=1024, P<=32 and R<=64)";
}

QString TerrainProfileSelector::ortsMasterCompatibility(
        const TerrainGridLayout &layout) const {
    if (layout.patchResolution != 16)
        return "Not compatible: renderer assumes R=16";
    if (layout.sampleCount == 256 && layout.patchesPerSide == 16)
        return "Supported standard profile";
    if (layout.sampleSpacing != 8)
        return "Geometry source-compatible; untested; normals assume 8 m spacing";
    return "Source-compatible; this tuple is not runtime-tested";
}

QString TerrainProfileSelector::ortsUnstableCompatibility(
        const TerrainGridLayout &layout) const {
    if (layout.sampleCount == 256 && layout.patchesPerSide == 16)
        return "Supported standard profile";
    if (layout.sampleSpacing != 8)
        return "Geometry source-compatible; untested; normals assume 8 m spacing";
    return "Source-compatible; this tuple is not runtime-tested";
}
