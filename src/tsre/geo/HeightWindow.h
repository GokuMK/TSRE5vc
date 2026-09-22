/* TSRE5 - Copyright (C) 2016 Piotr Gadecki. GPL-3.0-or-later. */
#pragma once
#include <QtWidgets>
#include <tsre/geo/ElevationSource.h>

class HeightWindow : public QDialog {
    Q_OBJECT
public:
    HeightWindow();
    ~HeightWindow() override;
    int tileX = 0, tileZ = 0, terrainResolution = 0, terrainSize = 0;
    bool ok = false;
    float **terrainData = nullptr;
    static void CheckForMissingGeodataFiles(QMap<int,QPair<int,int>*> &tileList);
    static bool lastLoadWasCancelled();
    static void resetLoadCancellation();
    int exec() override;
public slots:
    void load(bool gui = true);
    void hOffsetEnabled(QString value);
    void done(int result) override;
private:
    int allocatedTerrainResolution = 0;
    float yOffset = 0;
    bool prepared = false, loading = false;
    QLabel *imageLabel;
    QComboBox *sourceBox;
    QLineEdit *offsetEdit;
    QPushButton *loadButton, *applyButton;
    QTextBrowser *reportText;
    void clearData();
    void showSourceInformation();
};
