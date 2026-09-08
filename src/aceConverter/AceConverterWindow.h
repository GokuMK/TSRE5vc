#ifndef TSRE_ACE_CONVERTER_WINDOW_H
#define TSRE_ACE_CONVERTER_WINDOW_H

#include "AceConverter.h"
#include <QMainWindow>
#include <QColor>
#include <functional>

class QLabel;
class QComboBox;
class QCheckBox;
class QGraphicsView;
class QGraphicsScene;
class QThread;
class QLineEdit;
class QPlainTextEdit;

class AceConverterWindow : public QMainWindow {
public:
    explicit AceConverterWindow(const QColor &mainLabelColor = {}, QWidget *parent = nullptr);
    ~AceConverterWindow() override;
    void loadFile(const QString &path);
    bool isBusy() const { return busy; }
private:
    AceConverter::Image source;
    QWidget *sourcePanel = nullptr, *exportPanel = nullptr;
    QLabel *optionHint = nullptr, *suggestionLabel = nullptr;
    QLineEdit *fileField = nullptr, *dimensionsField = nullptr, *formatField = nullptr;
    QLineEdit *storageField = nullptr, *alphaField = nullptr, *mipsField = nullptr, *maskField = nullptr;
    QLineEdit *compressionField = nullptr;
    QPlainTextEdit *notesField = nullptr;
    QComboBox *encoding = nullptr;
    QCheckBox *mipmaps = nullptr, *zlib = nullptr;
    QCheckBox *recommendedOnly = nullptr, *sourceOnly = nullptr;
    QGraphicsView *preview = nullptr;
    QGraphicsScene *scene = nullptr;
    QThread *worker = nullptr;
    bool busy = false;
    void updateOptions();
    void rebuildEncodings(bool newSource = false);
    void exportFile(bool ace);
    void startJob(const QString &message, std::function<void()> work,
                  std::function<void()> finished);
};
#endif
