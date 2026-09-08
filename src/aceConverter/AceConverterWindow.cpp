#include "AceConverterWindow.h"
#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStatusBar>
#include <QSignalBlocker>
#include <QThread>
#include <QVBoxLayout>
#include <memory>

AceConverterWindow::AceConverterWindow(const QColor &mainLabelColor, QWidget *parent) : QMainWindow(parent) {
    setWindowTitle("TSRE — Ace Converter");
    resize(1280, 760);
    auto *central = new QWidget(this);
    auto *layout = new QHBoxLayout(central);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(8);
    const QColor accent = mainLabelColor.isValid() ? mainLabelColor : palette().color(QPalette::Highlight);
    auto heading = [accent](const QString &text, const QString &name, QWidget *parent) {
        auto *label = new QLabel(text, parent);
        label->setObjectName(name);
        label->setStyleSheet(QString("QLabel { color: %1; }").arg(accent.name()));
        label->setContentsMargins(3, 3, 0, 3);
        return label;
    };
    auto *left = new QWidget(central);
    left->setObjectName("sourcePanel");
    sourcePanel = left;
    left->setMinimumWidth(285);
    left->setMaximumWidth(320);
    auto *leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(0, 1, 1, 1);
    leftLayout->setSpacing(3);
    leftLayout->addWidget(heading("Source image:", "sourceHeading", left));
    auto *open = new QPushButton("Open image…", left);
    open->setObjectName("openImage");
    leftLayout->addWidget(open);
    auto *form = new QFormLayout;
    form->setContentsMargins(1, 3, 1, 1);
    form->setSpacing(3);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    auto field = [&](const QString &label, const QString &name) {
        auto *edit = new QLineEdit(left);
        edit->setObjectName(name);
        edit->setReadOnly(true);
        edit->setMinimumWidth(100);
        form->addRow(label, edit);
        return edit;
    };
    fileField = field("File name:", "sourceFile");
    fileField->setPlaceholderText("No image selected");
    dimensionsField = field("Dimensions:", "sourceDimensions");
    formatField = field("File format:", "sourceFormat");
    storageField = field("Pixel storage:", "sourceStorage");
    compressionField = field("ACE zlib:", "sourceCompression");
    alphaField = field("Alpha:", "sourceAlpha");
    mipsField = field("Mip levels:", "sourceMipCount");
    maskField = field("ACE mask:", "sourceMask");
    leftLayout->addLayout(form);
    leftLayout->addSpacing(10);
    leftLayout->addWidget(heading("Import notes:", "notesHeading", left));
    notesField = new QPlainTextEdit(left);
    notesField->setObjectName("sourceNotes");
    notesField->setReadOnly(true);
    notesField->setPlainText("Open an ACE, DDS or image file to begin.");
    notesField->setMaximumHeight(140);
    leftLayout->addWidget(notesField);
    leftLayout->addStretch();
    layout->addWidget(left);

    auto *center = new QVBoxLayout;
    auto *toolbar = new QHBoxLayout;
    toolbar->addWidget(heading("Texture preview:", "previewHeading", central));
    toolbar->addStretch();
    auto *actual = new QPushButton("100%", central);
    actual->setObjectName("actualSize");
    auto *fit = new QPushButton("Fit", central);
    fit->setObjectName("fitPreview");
    toolbar->addWidget(actual);
    toolbar->addWidget(fit);
    center->addLayout(toolbar);
    scene = new QGraphicsScene(this);
    preview = new QGraphicsView(scene, central);
    preview->setObjectName("texturePreview");
    preview->setMinimumSize(240, 200);
    preview->setDragMode(QGraphicsView::ScrollHandDrag);
    QPixmap checks(24, 24);
    const bool dark = palette().color(QPalette::Window).lightnessF() < 0.5;
    checks.fill(dark ? QColor(60, 60, 60) : QColor(195, 195, 195));
    QPainter painter(&checks);
    painter.fillRect(0, 0, 12, 12, dark ? QColor(85, 85, 85) : QColor(235, 235, 235));
    painter.fillRect(12, 12, 12, 12, dark ? QColor(85, 85, 85) : QColor(235, 235, 235));
    painter.end();
    preview->setBackgroundBrush(QBrush(checks));
    center->addWidget(preview, 1);
    layout->addLayout(center, 1);

    auto *right = new QWidget(central);
    right->setObjectName("exportPanel");
    exportPanel = right;
    right->setMinimumWidth(305);
    right->setMaximumWidth(340);
    auto *rightLayout = new QVBoxLayout(right);
    rightLayout->setContentsMargins(0, 1, 1, 1);
    rightLayout->setSpacing(3);
    rightLayout->addWidget(heading("ACE export:", "aceExportHeading", right));
    recommendedOnly = new QCheckBox("Suggested OR / MSTS formats only", right);
    recommendedOnly->setObjectName("recommendedFormatsOnly");
    recommendedOnly->setChecked(true);
    recommendedOnly->setToolTip("Show RGB, RGBA, RGB + mask, DXT1, DXT1 with alpha, DXT3 and DXT5. Turn off to include advanced packed, indexed and premultiplied formats.");
    sourceOnly = new QCheckBox("Match source image format", right);
    sourceOnly->setObjectName("sourceFormatsOnly");
    sourceOnly->setChecked(true);
    sourceOnly->setToolTip("Hide alpha formats for RGB sources and opaque formats for alpha sources. Suggest the original ACE/DDS encoding when available.");
    rightLayout->addWidget(recommendedOnly);
    rightLayout->addWidget(sourceOnly);
    rightLayout->addSpacing(6);
    rightLayout->addWidget(new QLabel("Pixel format:", right));
    encoding = new QComboBox(right);
    encoding->setStyleSheet("combobox-popup: 0;");
    encoding->setObjectName("aceEncoding");
    rightLayout->addWidget(encoding);
    suggestionLabel = new QLabel(right);
    suggestionLabel->setObjectName("formatSuggestion");
    suggestionLabel->setWordWrap(true);
    rightLayout->addWidget(suggestionLabel);
    rightLayout->addSpacing(6);
    mipmaps = new QCheckBox("Generate mipmaps", right);
    mipmaps->setObjectName("aceMipmaps");
    mipmaps->setToolTip("ACE mipmaps require square, power-of-two dimensions.");
    zlib = new QCheckBox("Compress ACE envelope (zlib)", right);
    zlib->setObjectName("aceZlib");
    zlib->setToolTip("Lossless file compression, independent of the pixel format.");
    rightLayout->addWidget(mipmaps);
    rightLayout->addWidget(zlib);
    optionHint = new QLabel(right);
    optionHint->setWordWrap(true);
    optionHint->setObjectName("exportHint");
    rightLayout->addWidget(optionHint);
    auto *saveAce = new QPushButton("Export ACE…", right);
    saveAce->setObjectName("exportAce");
    rightLayout->addWidget(saveAce);
    rightLayout->addSpacing(20);
    rightLayout->addWidget(heading("Image export:", "imageExportHeading", right));
    auto *saveImage = new QPushButton("Save image as…", right);
    saveImage->setObjectName("exportImage");
    rightLayout->addWidget(saveImage);
    auto *note = new QLabel("Image export uses the full-resolution base image. ACE format options apply only to ACE export.", right);
    note->setWordWrap(true);
    rightLayout->addWidget(note);
    rightLayout->addStretch();
    layout->addWidget(right);
    setCentralWidget(central);
    exportPanel->setEnabled(false);
    statusBar()->showMessage("Ready");

    connect(open, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getOpenFileName(this, "Open source image", source.path,
                                                          AceConverter::inputFilter());
        if (!path.isEmpty()) loadFile(path);
    });
    connect(actual, &QPushButton::clicked, this, [this] { preview->resetTransform(); });
    connect(fit, &QPushButton::clicked, this, [this] {
        if (!scene->sceneRect().isEmpty()) preview->fitInView(scene->sceneRect(), Qt::KeepAspectRatio);
    });
    connect(encoding, &QComboBox::currentIndexChanged, this, [this] { updateOptions(); });
    connect(recommendedOnly, &QCheckBox::toggled, this, [this] { rebuildEncodings(); });
    connect(sourceOnly, &QCheckBox::toggled, this, [this] { rebuildEncodings(); });
    connect(saveAce, &QPushButton::clicked, this, [this] { exportFile(true); });
    connect(saveImage, &QPushButton::clicked, this, [this] { exportFile(false); });
    rebuildEncodings();
}

AceConverterWindow::~AceConverterWindow() {
    if (worker) worker->wait();
}

void AceConverterWindow::startJob(const QString &message, std::function<void()> work,
                                   std::function<void()> finished) {
    busy = true;
    sourcePanel->setEnabled(false);
    exportPanel->setEnabled(false);
    statusBar()->showMessage(message);
    worker = QThread::create(std::move(work));
    worker->setParent(this);
    connect(worker, &QThread::finished, this, [this, finished = std::move(finished)] {
        worker->deleteLater();
        worker = nullptr;
        busy = false;
        sourcePanel->setEnabled(true);
        exportPanel->setEnabled(!source.pixels.isNull());
        finished();
    });
    worker->start();
}

void AceConverterWindow::loadFile(const QString &path) {
    if (busy) return;
    auto loaded = std::make_shared<AceConverter::Image>();
    auto error = std::make_shared<QString>();
    startJob("Loading image…", [path, loaded, error] {
        AceConverter::load(path, *loaded, *error);
    }, [this, loaded, error] {
        if (!error->isEmpty()) {
            statusBar()->showMessage("Image could not be loaded");
            QMessageBox::warning(this, "Cannot open image", *error);
            return;
        }
        source = std::move(*loaded);
        fileField->setText(QFileInfo(source.path).fileName());
        fileField->setToolTip(source.path);
        dimensionsField->setText(QString("%1 × %2").arg(source.pixels.width()).arg(source.pixels.height()));
        formatField->setText(source.format);
        storageField->setText(source.storage);
        storageField->setToolTip(source.storage);
        compressionField->setText(source.zlibEnvelope.has_value()
                                      ? (*source.zlibEnvelope ? "Yes" : "No")
                                      : "Not applicable");
        QString alpha = source.alpha == AceConverter::AlphaKind::Opaque ? "None (RGB)"
                        : (source.alpha == AceConverter::AlphaKind::Binary ? "1-bit mask" : "Alpha channel");
        if (source.alpha != AceConverter::AlphaKind::Opaque && !source.transparency)
            alpha += "; opaque pixels";
        alphaField->setText(alpha);
        alphaField->setToolTip(alpha);
        mipsField->setText(QString::number(source.mipCount));
        maskField->setText(source.mask.isEmpty() ? "None" : "Independent mask");
        notesField->setPlainText(source.warnings.isEmpty() ? "No import warnings." : source.warnings.join("\n\n"));
        for (auto *edit : {fileField, storageField, alphaField}) edit->setCursorPosition(0);
        scene->clear();
        scene->addPixmap(QPixmap::fromImage(source.pixels));
        scene->setSceneRect(QRectF(0, 0, source.pixels.width(), source.pixels.height()));
        preview->resetTransform();
        mipmaps->setChecked(false);
        mipmaps->setEnabled(AceConverter::canGenerateMips(source.pixels));
        rebuildEncodings(true);
        exportPanel->setEnabled(true);
        statusBar()->showMessage("Loaded " + source.path);
        setWindowTitle(QFileInfo(source.path).fileName() + " — Ace Converter");
    });
}

void AceConverterWindow::rebuildEncodings(bool newSource) {
    const QVariant previous = encoding->currentData();
    const auto suggested = AceConverter::suggestedEncoding(source, recommendedOnly->isChecked());
    {
        const QSignalBlocker blocker(encoding);
        encoding->clear();
        auto add = [&](const AceConverter::Encoding &entry) {
            if (recommendedOnly->isChecked() && !AceConverter::recommendedForSimulators(entry.value)) return;
            if (sourceOnly->isChecked() && !source.pixels.isNull() && !AceConverter::matchesSource(entry.value, source)) return;
            encoding->addItem(QString::fromUtf8(entry.label), int(entry.value));
        };
        // Place the source-aware suggestion first, without losing a still-valid user choice.
        for (const auto &entry : AceConverter::encodings()) if (entry.value == suggested) add(entry);
        for (const auto &entry : AceConverter::encodings()) if (entry.value != suggested) add(entry);
        const int previousIndex = encoding->findData(previous);
        if (!newSource && previousIndex >= 0) encoding->setCurrentIndex(previousIndex);
    }
    QString suggestedName;
    for (const auto &entry : AceConverter::encodings())
        if (entry.value == suggested) suggestedName = QString::fromUtf8(entry.label);
    suggestionLabel->setText(source.pixels.isNull() ? "Open a source image for a format suggestion."
                                                  : "Suggested: " + suggestedName);
    updateOptions();
}

void AceConverterWindow::updateOptions() {
    const auto value = AceEncoding(encoding->currentData().toInt());
    QStringList hints;
    if (source.transparency && (value == AceEncoding::Rgb || value == AceEncoding::Rgb565 ||
                               value == AceEncoding::Dxt1 || value == AceEncoding::IndexedRgb))
        hints << "This format removes transparency.";
    if (value == AceEncoding::Mask || value == AceEncoding::Dxt1Mask || value == AceEncoding::Argb1555)
        hints << "Transparency is reduced to a 1-bit mask (alpha threshold: 128).";
    if (value == AceEncoding::IndexedRgb || value == AceEncoding::IndexedRgba)
        hints << "Indexed export requires at most 256 distinct colors; colors are not quantized.";
    if (value == AceEncoding::Dxt2 || value == AceEncoding::Dxt4)
        hints << "Premultiplied alpha can appear too dark in MSTS/MSRE. DXT3 or DXT5 is usually preferable.";
    if (value == AceEncoding::IndexedRgba)
        hints << "Native MSTS/MSRE may reduce indexed alpha to cutouts.";
    if (!AceConverter::canGenerateMips(source.pixels))
        hints << "Mipmaps need square, power-of-two dimensions. This image keeps its original size.";
    hints << "ACE export rebuilds pixels and optional mipmaps; source metadata and resources are not copied.";
    optionHint->setText(hints.join("\n\n"));
}

void AceConverterWindow::exportFile(bool ace) {
    if (busy || source.pixels.isNull()) return;
    const QFileInfo input(source.path);
    const QString suggested = input.absolutePath() + '/' + input.completeBaseName() +
                              (ace ? ".ace" : ".png");
    QFileDialog dialog(this, ace ? "Export ACE" : "Save image as", suggested);
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setFileMode(QFileDialog::AnyFile);
    dialog.setNameFilters(ace ? QStringList{"ACE texture (*.ace)"} : AceConverter::outputFilters());
    dialog.selectNameFilter(ace ? "ACE texture (*.ace)" : "PNG image (*.png)");
    dialog.setDefaultSuffix(ace ? "ace" : "png");
    connect(&dialog, &QFileDialog::filterSelected, &dialog, [&dialog](const QString &filter) {
        const int start = filter.indexOf("*.") + 2;
        dialog.setDefaultSuffix(filter.mid(start, filter.indexOf(')', start) - start));
    });
    if (dialog.exec() != QDialog::Accepted || dialog.selectedFiles().isEmpty()) return;
    const QString path = dialog.selectedFiles().first();
    const QString filter = dialog.selectedNameFilter();
    const int start = filter.indexOf("*.") + 2;
    const QByteArray format = ace ? QByteArray("ace") : filter.mid(start, filter.indexOf(')', start) - start).toLatin1();
    if (QFileInfo(path).suffix().compare(QString::fromLatin1(format), Qt::CaseInsensitive) != 0) {
        QMessageBox::warning(this, "Output extension", "Choose a filename ending in ." + QString::fromLatin1(format) +
                             " to match the selected format.");
        return;
    }
    AceWriteOptions options;
    options.encoding = AceEncoding(encoding->currentData().toInt());
    options.mipmaps = mipmaps->isChecked();
    options.zlib = zlib->isChecked();
    auto error = std::make_shared<QString>();
    const auto image = source;
    startJob("Exporting image…", [image, path, options, format, error] {
        AceConverter::save(image, path, options, *error, format);
    }, [this, path, error] {
        if (!error->isEmpty()) {
            statusBar()->showMessage("Export failed");
            QMessageBox::warning(this, "Cannot export image", *error);
        } else {
            statusBar()->showMessage("Saved " + path);
        }
    });
}
