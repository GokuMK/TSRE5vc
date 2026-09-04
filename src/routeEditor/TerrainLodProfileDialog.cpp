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
#include <QStringList>
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
                "normal terrain visibility culling.");
    description->setWordWrap(true);
    layout->addWidget(description);

    enabled = new QCheckBox("Enable terrain mesh LOD for this route");
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
    const QVector<TerrainLodLevel> displayed = newLevels.isEmpty()
            ? QVector<TerrainLodLevel>{{4, 1000}, {8, 2000}, {16, 4000}}
            : newLevels;
    for (const TerrainLodLevel &level : displayed)
        addLevelRow(level);
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
            [this]() { validateInput(); });
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
    if (levels.isEmpty())
        return "Disabled - native terrain resolution at all distances";
    QStringList parts;
    int startDistance = 0;
    for (const TerrainLodLevel &level : levels) {
        parts.append(QString("%1 m: %2-%3 m")
                     .arg(level.sampleSpacing)
                     .arg(startDistance)
                     .arg(level.preferredEndDistance));
        startDistance = level.preferredEndDistance;
    }
    parts.last().append(" and beyond");
    return parts.join(", ");
}

void TerrainLodProfileDialog::addSuggestedLevel() {
    TerrainLodLevel next{1, 1000};
    if (table->rowCount() > 0) {
        const QComboBox *spacing = qobject_cast<QComboBox*>(
                    table->cellWidget(table->rowCount() - 1, 0));
        const QSpinBox *endDistance = qobject_cast<QSpinBox*>(
                    table->cellWidget(table->rowCount() - 1, 1));
        if (spacing != nullptr)
            next.sampleSpacing = std::min(32,
                                          spacing->currentData().toInt() * 2);
        if (endDistance != nullptr)
            next.preferredEndDistance = endDistance->value() + 1000;
    }
    addLevelRow(next);
}

void TerrainLodProfileDialog::removeSelectedLevel() {
    if (table->currentRow() >= 0)
        table->removeRow(table->currentRow());
    validateInput();
}

void TerrainLodProfileDialog::updateEnabledState() {
    const bool isEnabled = enabled->isChecked();
    table->setEnabled(isEnabled);
    addButton->setEnabled(isEnabled);
    removeButton->setEnabled(isEnabled);
}

void TerrainLodProfileDialog::validateInput() {
    QString error;
    const bool valid = TerrainLod::validateProfile(
                levels(), &error, !enabled->isChecked());
    validation->setText(valid ? QString() : error);
    okButton->setEnabled(valid);
}
