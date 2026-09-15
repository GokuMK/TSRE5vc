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
    setWindowTitle(
        //% "TSRE — Ace Converter"
        qtTrId("ace.converter.ace.converter.window.title.tsre.ace.converter"));
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
    leftLayout->addWidget(heading(
        //% "Source image:"
        qtTrId("ace.converter.source.heading"), "sourceHeading", left));
    auto *open = new QPushButton(
        //% "Open image…"
        qtTrId("ace.converter.ace.converter.window.button.open"), left);
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
    fileField = field(
        //% "File name:"
        qtTrId("ace.converter.ace.converter.window.label.file.field"), "sourceFile");
    fileField->setPlaceholderText(
        //% "No image selected"
        qtTrId("ace.converter.ace.converter.window.placeholder.no.image.selected"));
    dimensionsField = field(
        //% "Dimensions:"
        qtTrId("ace.converter.ace.converter.window.label.dimensions.field"), "sourceDimensions");
    formatField = field(
        //% "File format:"
        qtTrId("ace.converter.ace.converter.window.label.format.field"), "sourceFormat");
    storageField = field(
        //% "Pixel storage:"
        qtTrId("ace.converter.ace.converter.window.label.storage.field"), "sourceStorage");
    compressionField = field(
        //% "ACE zlib:"
        qtTrId("ace.converter.ace.converter.window.label.compression.field"), "sourceCompression");
    alphaField = field(
        //% "Alpha:"
        qtTrId("ace.converter.ace.converter.window.label.alpha.field"), "sourceAlpha");
    mipsField = field(
        //% "Mip levels:"
        qtTrId("ace.converter.ace.converter.window.label.mips.field"), "sourceMipCount");
    maskField = field(
        //% "ACE mask:"
        qtTrId("ace.converter.ace.converter.window.label.mask.field"), "sourceMask");
    leftLayout->addLayout(form);
    leftLayout->addSpacing(10);
    leftLayout->addWidget(heading(
        //% "Import notes:"
        qtTrId("ace.converter.import.notes.heading"), "notesHeading", left));
    notesField = new QPlainTextEdit(left);
    notesField->setObjectName("sourceNotes");
    notesField->setReadOnly(true);
    notesField->setPlainText(
        //% "Open an ACE, DDS or image file to begin."
        qtTrId("ace.converter.ace.converter.window.text.open.ace.dds.image.file.begin"));
    notesField->setMaximumHeight(140);
    leftLayout->addWidget(notesField);
    leftLayout->addStretch();
    layout->addWidget(left);

    auto *center = new QVBoxLayout;
    auto *toolbar = new QHBoxLayout;
    toolbar->addWidget(heading(
        //% "Texture preview:"
        qtTrId("ace.converter.preview.heading"), "previewHeading", central));
    toolbar->addStretch();
    auto *actual = new QPushButton("100%", central);
    actual->setObjectName("actualSize");
    auto *fit = new QPushButton(
        //% "Fit"
        qtTrId("ace.converter.ace.converter.window.button.fit"), central);
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
    rightLayout->addWidget(heading(
        //% "ACE export:"
        qtTrId("ace.converter.export.ace.heading"), "aceExportHeading", right));
    recommendedOnly = new QCheckBox(
        //% "Suggested OR / MSTS formats only"
        qtTrId("ace.converter.ace.converter.window.option.recommended.only"), right);
    recommendedOnly->setObjectName("recommendedFormatsOnly");
    recommendedOnly->setChecked(true);
    recommendedOnly->setToolTip(
        //% "Show RGB, RGBA, RGB + mask, DXT1, DXT1 with alpha, DXT3 and DXT5. Turn off to include advanced packed, indexed and premultiplied formats."
        qtTrId("ace.converter.ace.converter.window.tooltip.show.rgb.rgba.rgb.mask.dxt1.dxt1"));
    sourceOnly = new QCheckBox(
        //% "Match source image format"
        qtTrId("ace.converter.ace.converter.window.option.source.only"), right);
    sourceOnly->setObjectName("sourceFormatsOnly");
    sourceOnly->setChecked(true);
    sourceOnly->setToolTip(
        //% "Hide alpha formats for RGB sources and opaque formats for alpha sources. Suggest the original ACE/DDS encoding when available."
        qtTrId("ace.converter.ace.converter.window.tooltip.hide.alpha.formats.for.rgb.sources.opaque"));
    rightLayout->addWidget(recommendedOnly);
    rightLayout->addWidget(sourceOnly);
    rightLayout->addSpacing(6);
    rightLayout->addWidget(new QLabel(
        //% "Pixel format:"
        qtTrId("ace.converter.ace.converter.window.label.pixel.format"), right));
    encoding = new QComboBox(right);
    encoding->setStyleSheet("combobox-popup: 0;");
    encoding->setObjectName("aceEncoding");
    rightLayout->addWidget(encoding);
    suggestionLabel = new QLabel(right);
    suggestionLabel->setObjectName("formatSuggestion");
    suggestionLabel->setWordWrap(true);
    rightLayout->addWidget(suggestionLabel);
    rightLayout->addSpacing(6);
    mipmaps = new QCheckBox(
        //% "Generate mipmaps"
        qtTrId("ace.converter.ace.converter.window.option.mipmaps"), right);
    mipmaps->setObjectName("aceMipmaps");
    mipmaps->setToolTip(
        //% "ACE mipmaps require square, power-of-two dimensions."
        qtTrId("ace.converter.ace.converter.window.tooltip.ace.mipmaps.require.square.power.two.dimensions"));
    zlib = new QCheckBox(
        //% "Compress ACE envelope (zlib)"
        qtTrId("ace.converter.ace.converter.window.option.zlib"), right);
    zlib->setObjectName("aceZlib");
    zlib->setToolTip(
        //% "Lossless file compression, independent of the pixel format."
        qtTrId("ace.converter.ace.converter.window.tooltip.lossless.file.compression.independent.pixel.format"));
    rightLayout->addWidget(mipmaps);
    rightLayout->addWidget(zlib);
    optionHint = new QLabel(right);
    optionHint->setWordWrap(true);
    optionHint->setObjectName("exportHint");
    rightLayout->addWidget(optionHint);
    auto *saveAce = new QPushButton(
        //% "Export ACE…"
        qtTrId("ace.converter.ace.converter.window.button.save.ace"), right);
    saveAce->setObjectName("exportAce");
    rightLayout->addWidget(saveAce);
    rightLayout->addSpacing(20);
    rightLayout->addWidget(heading(
        //% "Image export:"
        qtTrId("ace.converter.export.image.heading"), "imageExportHeading", right));
    auto *saveImage = new QPushButton(
        //% "Save image as…"
        qtTrId("ace.converter.ace.converter.window.button.save.image"), right);
    saveImage->setObjectName("exportImage");
    rightLayout->addWidget(saveImage);
    auto *note = new QLabel(
        //% "Image export uses the full-resolution base image. ACE format options apply only to ACE export."
        qtTrId("ace.converter.ace.converter.window.label.note"), right);
    note->setWordWrap(true);
    rightLayout->addWidget(note);
    rightLayout->addStretch();
    layout->addWidget(right);
    setCentralWidget(central);
    exportPanel->setEnabled(false);
    statusBar()->showMessage(
        //% "Ready"
        qtTrId("ace.converter.ace.converter.window.text.ready"));

    connect(open, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getOpenFileName(this,
            //% "Open source image"
            qtTrId("ace.converter.ace.converter.window.dialog.title.path"), source.path,
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
    startJob(
        //% "Loading image…"
        qtTrId("ace.converter.ace.converter.window.status.loading.image"), [path, loaded, error] {
        AceConverter::load(path, *loaded, *error);
    }, [this, loaded, error] {
        if (!error->isEmpty()) {
            statusBar()->showMessage(
                //% "Image could not be loaded"
                qtTrId("ace.converter.ace.converter.window.text.image.could.not.be.loaded"));
            QMessageBox::warning(this,
                //% "Cannot open image"
                qtTrId("ace.converter.ace.converter.window.dialog.title.cannot.open.image"), *error);
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
            ? (*source.zlibEnvelope
                ?
                  //% "Yes"
                  qtTrId("ace.converter.source.compression.yes")
                :
                  //% "No"
                  qtTrId("ace.converter.source.compression.no"))
            :
              //% "Not applicable"
              qtTrId("ace.converter.source.compression.not.applicable"));
        QString alpha = source.alpha == AceConverter::AlphaKind::Opaque
                ?
                  //% "None (RGB)"
                  qtTrId("ace.converter.source.alpha.none")
                : (source.alpha == AceConverter::AlphaKind::Binary
                   ?
                     //% "1-bit mask"
                     qtTrId("ace.converter.source.alpha.mask")
                   :
                     //% "Alpha channel"
                     qtTrId("ace.converter.source.alpha.channel"));
        if (source.alpha != AceConverter::AlphaKind::Opaque && !source.transparency)
            alpha =
                //% "%1; opaque pixels"
                qtTrId("ace.converter.source.alpha.opaque.pixels").arg(alpha);
        alphaField->setText(alpha);
        alphaField->setToolTip(alpha);
        mipsField->setText(QString::number(source.mipCount));
        maskField->setText(source.mask.isEmpty()
            ?
              //% "None"
              qtTrId("ace.converter.source.mask.none")
            :
              //% "Independent mask"
              qtTrId("ace.converter.source.mask.independent"));
        notesField->setPlainText(source.warnings.isEmpty()
            ?
              //% "No import warnings."
              qtTrId("ace.converter.source.warnings.none")
            : source.warnings.join("\n\n"));
        for (auto *edit : {fileField, storageField, alphaField}) edit->setCursorPosition(0);
        scene->clear();
        scene->addPixmap(QPixmap::fromImage(source.pixels));
        scene->setSceneRect(QRectF(0, 0, source.pixels.width(), source.pixels.height()));
        preview->resetTransform();
        mipmaps->setChecked(false);
        mipmaps->setEnabled(AceConverter::canGenerateMips(source.pixels));
        rebuildEncodings(true);
        exportPanel->setEnabled(true);
        //% "Loaded %1"
        statusBar()->showMessage(qtTrId("ace.converter.status.loaded").arg(source.path));
        //% "%1 — Ace Converter"
        setWindowTitle(qtTrId("ace.converter.title.file").arg(
                           QFileInfo(source.path).fileName()));
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
            encoding->addItem(qtTrId(entry.labelId), int(entry.value));
        };
        // Place the source-aware suggestion first, without losing a still-valid user choice.
        for (const auto &entry : AceConverter::encodings()) if (entry.value == suggested) add(entry);
        for (const auto &entry : AceConverter::encodings()) if (entry.value != suggested) add(entry);
        const int previousIndex = encoding->findData(previous);
        if (!newSource && previousIndex >= 0) encoding->setCurrentIndex(previousIndex);
    }
    QString suggestedName;
    for (const auto &entry : AceConverter::encodings())
        if (entry.value == suggested) suggestedName = qtTrId(entry.labelId);
    suggestionLabel->setProperty("suggestedEncoding", int(suggested));
    suggestionLabel->setText(source.pixels.isNull()
        ?
          //% "Open a source image for a format suggestion."
          qtTrId("ace.converter.suggestion.open.source")
        :
          //% "Suggested: %1"
          qtTrId("ace.converter.suggestion.result").arg(suggestedName));
    updateOptions();
}

void AceConverterWindow::updateOptions() {
    const auto value = AceEncoding(encoding->currentData().toInt());
    QStringList hints;
    if (source.transparency && (value == AceEncoding::Rgb || value == AceEncoding::Rgb565 ||
                               value == AceEncoding::Dxt1 || value == AceEncoding::IndexedRgb))
        //% "This format removes transparency."
        hints << qtTrId("ace.converter.hint.transparency.removed");
    if (value == AceEncoding::Mask || value == AceEncoding::Dxt1Mask || value == AceEncoding::Argb1555)
        //% "Transparency is reduced to a 1-bit mask (alpha threshold: 128)."
        hints << qtTrId("ace.converter.hint.transparency.binary");
    if (value == AceEncoding::IndexedRgb || value == AceEncoding::IndexedRgba)
        //% "Indexed export requires at most 256 distinct colors; colors are not quantized."
        hints << qtTrId("ace.converter.hint.indexed.limit");
    if (value == AceEncoding::Dxt2 || value == AceEncoding::Dxt4)
        //% "Premultiplied alpha can appear too dark in MSTS/MSRE. DXT3 or DXT5 is usually preferable."
        hints << qtTrId("ace.converter.hint.premultiplied.alpha");
    if (value == AceEncoding::IndexedRgba)
        //% "Native MSTS/MSRE may reduce indexed alpha to cutouts."
        hints << qtTrId("ace.converter.hint.native.indexed.alpha");
    if (!AceConverter::canGenerateMips(source.pixels))
        //% "Mipmaps need square, power-of-two dimensions. This image keeps its original size."
        hints << qtTrId("ace.converter.hint.mipmap.dimensions");
    //% "ACE export rebuilds pixels and optional mipmaps; source metadata and resources are not copied."
    hints << qtTrId("ace.converter.hint.export.rebuilds");
    optionHint->setText(hints.join("\n\n"));
}

void AceConverterWindow::exportFile(bool ace) {
    if (busy || source.pixels.isNull()) return;
    const QFileInfo input(source.path);
    const QString suggested = input.absolutePath() + '/' + input.completeBaseName() +
                              (ace ? ".ace" : ".png");
    QFileDialog dialog(this, ace
        ?
          //% "Export ACE"
          qtTrId("ace.converter.export.ace.title")
        :
          //% "Save image as"
          qtTrId("ace.converter.export.image.title"), suggested);
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setFileMode(QFileDialog::AnyFile);
    // Keep the wildcard outside the translation so a visible missing ID still
    // produces a functional file filter.
    //% "ACE texture"
    const QString aceFilter = qtTrId("ace.converter.export.filter.ace") + " (*.ace)";
    const QStringList imageFilters = AceConverter::outputFilters();
    dialog.setNameFilters(ace ? QStringList{aceFilter} : imageFilters);
    QString pngFilter;
    for (const QString &filter : imageFilters)
        if (filter.endsWith("(*.png)", Qt::CaseInsensitive)) {
            pngFilter = filter;
            break;
        }
    dialog.selectNameFilter(ace ? aceFilter : pngFilter);
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
        QMessageBox::warning(this,
            //% "Output extension"
            qtTrId("ace.converter.ace.converter.window.dialog.title.output.extension"),
            //% "Choose a filename ending in .%1 to match the selected format."
            qtTrId("ace.converter.export.extension.message")
                .arg(QString::fromLatin1(format)));
        return;
    }
    AceWriteOptions options;
    options.encoding = AceEncoding(encoding->currentData().toInt());
    options.mipmaps = mipmaps->isChecked();
    options.zlib = zlib->isChecked();
    auto error = std::make_shared<QString>();
    const auto image = source;
    startJob(
        //% "Exporting image…"
        qtTrId("ace.converter.ace.converter.window.status.exporting.image"), [image, path, options, format, error] {
        AceConverter::save(image, path, options, *error, format);
    }, [this, path, error] {
        if (!error->isEmpty()) {
            statusBar()->showMessage(
                //% "Export failed"
                qtTrId("ace.converter.ace.converter.window.text.export.failed"));
            QMessageBox::warning(this,
                //% "Cannot export image"
                qtTrId("ace.converter.ace.converter.window.dialog.title.cannot.export.image"), *error);
        } else {
            //% "Saved %1"
            statusBar()->showMessage(qtTrId("ace.converter.status.saved").arg(path));
        }
    });
}
