/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 */

#include <routeEditor/TerrainLodProfileDialog.h>

#include <algorithm>

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>

TerrainLodProfileDialog::TerrainLodProfileDialog(QWidget *parent)
    : QDialog(parent) {
    setWindowTitle("Terrain mesh LOD profile");
    setMinimumWidth(560);

    QVBoxLayout *layout = new QVBoxLayout(this);
    QLabel *description = new QLabel(
                "Choose the preferred terrain sample spacing for each distance "
                "range. The last level continues beyond its preferred end until "
                "normal terrain visibility culling. Change the first sample "
                "spacing; following levels adjust automatically.");
    description->setWordWrap(true);
    layout->addWidget(description);

    enabled = new QCheckBox("Use a route-specific terrain mesh LOD profile");
    layout->addWidget(enabled);

    table = new QTableWidget(0, 2);
    table->setHorizontalHeaderLabels({"Sample spacing", "Preferred end distance"});
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    table->verticalHeader()->setVisible(false);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    layout->addWidget(table);

    QHBoxLayout *editButtons = new QHBoxLayout;
    addButton = new QPushButton("Add level");
    removeButton = new QPushButton("Remove selected");
    editButtons->addWidget(addButton);
    editButtons->addWidget(removeButton);
    editButtons->addStretch();
    layout->addLayout(editButtons);

    validation = new QLabel;
    validation->setStyleSheet("color: #c03030;");
    layout->addWidget(validation);

    QDialogButtonBox *buttons = new QDialogButtonBox(
                QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    okButton = buttons->button(QDialogButtonBox::Ok);
    layout->addWidget(buttons);

    connect(enabled, &QCheckBox::toggled, this,
            [this]() { updateEnabledState(); validateInput(); });
    connect(addButton, &QPushButton::clicked, this,
            [this]() { addSuggestedLevel(); });
    connect(removeButton, &QPushButton::clicked, this,
            [this]() { removeSelectedLevel(); });
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void TerrainLodProfileDialog::setLevels(
        const QVector<TerrainLodLevel> &newLevels) {
    table->setRowCount(0);
    enabled->setChecked(!newLevels.isEmpty());
    const QVector<TerrainLodLevel> &displayed = newLevels.isEmpty()
            ? TerrainLod::defaultProfile() : newLevels;
    for (const TerrainLodLevel &level : displayed)
        addLevelRow(level);
    synchronizeSpacings();
    updateEnabledState();
    validateInput();
}

void TerrainLodProfileDialog::addLevelRow(const TerrainLodLevel &level) {
    const int row = table->rowCount();
    table->insertRow(row);
    QComboBox *spacing = new QComboBox;
    spacing->setStyleSheet("combobox-popup: 0;");
    for (int value : {1, 2, 4, 8, 16, 32})
        spacing->addItem(QString("%1 m").arg(value), value);
    int spacingIndex = spacing->findData(level.sampleSpacing);
    spacing->setCurrentIndex(spacingIndex >= 0 ? spacingIndex : 0);
    QSpinBox *endDistance = new QSpinBox;
    endDistance->setRange(1, 1000000);
    endDistance->setSuffix(" m");
    endDistance->setValue(std::max(1, level.preferredEndDistance));
    table->setCellWidget(row, 0, spacing);
    table->setCellWidget(row, 1, endDistance);
    connect(spacing, &QComboBox::currentIndexChanged, this,
            [this, spacing]() {
        if (table->rowCount() > 0 && table->cellWidget(0, 0) == spacing)
            synchronizeSpacings();
        validateInput();
    });
    connect(endDistance, &QSpinBox::valueChanged, this,
            [this]() { validateInput(); });
    table->selectRow(row);
    validateInput();
}

QVector<TerrainLodLevel> TerrainLodProfileDialog::levels() const {
    QVector<TerrainLodLevel> result;
    if (!enabled->isChecked())
        return result;
    for (int row = 0; row < table->rowCount(); ++row) {
        const QComboBox *spacing = qobject_cast<QComboBox*>(
                    table->cellWidget(row, 0));
        const QSpinBox *endDistance = qobject_cast<QSpinBox*>(
                    table->cellWidget(row, 1));
        if (spacing == nullptr || endDistance == nullptr)
            continue;
        result.append({spacing->currentData().toInt(), endDistance->value()});
    }
    return result;
}

QString TerrainLodProfileDialog::summary(
        const QVector<TerrainLodLevel> &levels) {
    return TerrainLod::profileSummary(levels);
}

void TerrainLodProfileDialog::addSuggestedLevel() {
    TerrainLodLevel next{1, 1000};
    if (table->rowCount() > 0) {
        const QSpinBox *endDistance = qobject_cast<QSpinBox*>(
                    table->cellWidget(table->rowCount() - 1, 1));
        if (endDistance != nullptr)
            next.preferredEndDistance = endDistance->value() + 1000;
    }
    addLevelRow(next);
    synchronizeSpacings();
    validateInput();
}

void TerrainLodProfileDialog::removeSelectedLevel() {
    if (table->currentRow() >= 0)
        table->removeRow(table->currentRow());
    synchronizeSpacings();
    validateInput();
}

void TerrainLodProfileDialog::synchronizeSpacings() {
    const int rows = table->rowCount();
    if (rows <= 0)
        return;
    QComboBox *first = qobject_cast<QComboBox*>(table->cellWidget(0, 0));
    if (first == nullptr)
        return;
    const int maximumFirstIndex = std::max(0, first->count() - rows);
    QStandardItemModel *firstModel =
            qobject_cast<QStandardItemModel*>(first->model());
    if (firstModel != nullptr) {
        for (int index = 0; index < first->count(); ++index)
            firstModel->item(index)->setEnabled(index <= maximumFirstIndex);
    }
    if (first->currentIndex() > maximumFirstIndex) {
        const QSignalBlocker blocker(first);
        first->setCurrentIndex(maximumFirstIndex);
    }
    const int firstIndex = first->currentIndex();
    for (int row = 0; row < rows; ++row) {
        QComboBox *spacing = qobject_cast<QComboBox*>(table->cellWidget(row, 0));
        if (spacing == nullptr)
            continue;
        const QSignalBlocker blocker(spacing);
        spacing->setCurrentIndex(firstIndex + row);
        spacing->setEnabled(enabled->isChecked() && row == 0);
        spacing->setToolTip(row == 0
                ? "Following LOD resolutions are derived from this value."
                : "Automatically set to twice the preceding LOD resolution.");
    }
}

void TerrainLodProfileDialog::updateEnabledState() {
    const bool isEnabled = enabled->isChecked();
    table->setEnabled(isEnabled);
    synchronizeSpacings();
    const QComboBox *lastSpacing = table->rowCount() > 0
            ? qobject_cast<QComboBox*>(table->cellWidget(table->rowCount() - 1, 0))
            : nullptr;
    addButton->setEnabled(isEnabled && table->rowCount() < 6
                          && (lastSpacing == nullptr
                              || lastSpacing->currentData().toInt() < 32));
    removeButton->setEnabled(isEnabled);
}

void TerrainLodProfileDialog::validateInput() {
    QString error;
    const bool valid = TerrainLod::validateProfile(
                levels(), &error, !enabled->isChecked());
    validation->setText(valid ? QString() : error);
    okButton->setEnabled(valid);
    updateEnabledState();
}
