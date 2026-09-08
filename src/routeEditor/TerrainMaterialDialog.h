#pragma once
#include <QDialog>
#include <memory>
class TerrainMaterialLibrary;
class QTableWidget;
class QLabel;
class QPushButton;
class TerrainMaterialDialog : public QDialog {
public:
    explicit TerrainMaterialDialog(QWidget *parent=nullptr, quint32 selected=0, const QString &message={});
    quint32 selectedUid() const;
private:
    void refresh(quint32 selected);
    std::shared_ptr<TerrainMaterialLibrary> library;
    QTableWidget *table;
    QLabel *status;
    QPushButton *choose, *fromImage;
};
