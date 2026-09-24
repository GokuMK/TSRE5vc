/* TSRE5 - Copyright (C) 2016 Piotr Gadecki. GPL-3.0-or-later. */
#pragma once

#include <QDialog>
#include <QHash>
#include <QImage>

class QComboBox;
class QLabel;
class QPushButton;
class QTextBrowser;

class ImageryWindow : public QDialog {
    Q_OBJECT
public:
    explicit ImageryWindow(QWidget *parent = nullptr);
    int tileX = 0, tileZ = 0, terrainSize = 0;
    bool distantTerrain = false;
    int exec() override;

public slots:
    void loadPreview();
    void apply();
    void done(int result) override;

private:
    QComboBox *sourceBox = nullptr;
    QComboBox *resolutionBox = nullptr;
    QLabel *imageLabel = nullptr;
    QLabel *resolutionLabel = nullptr;
    QPushButton *loadButton = nullptr;
    QPushButton *applyButton = nullptr;
    QTextBrowser *reportText = nullptr;
    QImage preparedImage;
    bool loading = false;
    QHash<QString,int> selectedRequestSizes;

    void invalidatePreview();
    void showSourceInformation();
    void updateRequestSizes();
};
