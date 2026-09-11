#include <tsre/world/TerrainBakeCommand.h>
#include <tsre/world/TerrainSeason.h>
#include <tsre/world/TerrainMaterialMap.h>
#include <tsre/world/TerrainLib.h>
#include <tsre/Game.h>
#include <QDialog>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QComboBox>
#include <QPushButton>
#include <QPlainTextEdit>
#include <QLabel>
#include <QProcess>
#include <QCoreApplication>
#include <QMessageBox>
#include <QCloseEvent>
#include <QDir>

namespace {
class BakeDialog : public QDialog {
public:
    using QDialog::QDialog;
    bool running=false;
    void reject() override {if (!running) QDialog::reject();}
protected:
    void closeEvent(QCloseEvent *event) override {
        if (running) event->ignore();else QDialog::closeEvent(event);
    }
};
}
void TerrainBakeCommand::showDialog(QWidget *parent,const QString &route) {
    if (!Game::writeEnabled || Game::serverClient) {
        QMessageBox::warning(parent,"Bake terrain","Route writing is disabled in this session.");return;
    }
    BakeDialog dialog(parent);dialog.setWindowTitle("Bake procedural terrain textures");dialog.resize(640,400);
    auto *layout=new QVBoxLayout(&dialog);
    auto *description=new QLabel("Bake saved material maps. Editing is blocked until completion.\nAll includes Base, Snow and seasonal directories present in TERRTEX.",&dialog);
    layout->addWidget(description);
    auto *form=new QFormLayout();auto *season=new QComboBox(&dialog);
    season->setStyleSheet("combobox-popup: 0;");
    season->addItem("All available variants","all");
    for (const auto &v:TerrainSeason::available(QDir(route).filePath("terrtex"))) season->addItem(v,v);
    auto *resolution=new QComboBox(&dialog);
    resolution->setStyleSheet("combobox-popup: 0;");
    for (int n:{256,512,1024,2048,4096}) resolution->addItem(QString::number(n),n);
    resolution->setCurrentIndex(resolution->findData(TerrainMaterialMap::BakedSide));
    form->addRow("Season",season);form->addRow("Baked texture size",resolution);layout->addLayout(form);
    auto *output=new QPlainTextEdit(&dialog);output->setReadOnly(true);output->setMaximumBlockCount(2000);layout->addWidget(output);
    auto *start=new QPushButton("Bake",&dialog);auto *close=new QPushButton("Close",&dialog);
    layout->addWidget(start);layout->addWidget(close);
    QProcess process(&dialog);process.setProcessChannelMode(QProcess::MergedChannels);
    QObject::connect(close,&QPushButton::clicked,&dialog,&QDialog::reject);
    QObject::connect(&process,&QProcess::readyReadStandardOutput,&dialog,[&]{output->appendPlainText(QString::fromLocal8Bit(process.readAllStandardOutput()));});
    auto done=[&] {
        dialog.running=false;start->setEnabled(true);close->setEnabled(true);season->setEnabled(true);resolution->setEnabled(true);
        if (Game::terrainLib) Game::terrainLib->reloadProceduralBakeMetadata();
    };
    QObject::connect(&process,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),&dialog,[&](int code,QProcess::ExitStatus status){
        output->appendPlainText(code==0 && status==QProcess::NormalExit?"Finished.":"Baking failed; see errors above. Completed tiles remain saved.");done();
    });
    QObject::connect(&process,&QProcess::errorOccurred,&dialog,[&](QProcess::ProcessError e){
        output->appendPlainText(process.errorString());if (e==QProcess::FailedToStart) done();
    });
    QObject::connect(start,&QPushButton::clicked,&dialog,[&]{
        if (!Game::writeEnabled) return;
        dialog.running=true;start->setEnabled(false);close->setEnabled(false);season->setEnabled(false);resolution->setEnabled(false);
        QStringList arguments{"--refreshpmaptextures","--route",route,"--season",season->currentData().toString(),
                              "--res",resolution->currentData().toString(),"--patch-res",QString::number(TerrainMaterialMap::OutputSide)};
        if (TerrainMaterialMap::ValidateBakeOnLoad) arguments<<"--validate";
        process.start(QCoreApplication::applicationFilePath(),arguments);
    });
    dialog.exec();
}
