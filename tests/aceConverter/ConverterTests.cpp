#include <aceConverter/AceConverter.h>
#include <aceConverter/AceConverterWindow.h>
#include <tsre/texture/DdsLib.h>
#include <tsre/texture/DxtCodec.h>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QFontDatabase>
#include <QGraphicsView>
#include <QLabel>
#include <QLineEdit>
#include <QImageReader>
#include <QPainter>
#include <QProcess>
#include <QPushButton>
#include <QScrollBar>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QtEndian>
#include <cstdio>

namespace {
int checks = 0, failures = 0;
void check(bool ok, const QString &message) {
    ++checks;
    if (!ok) { ++failures; fprintf(stderr, "FAIL: %s\n", qPrintable(message)); }
}
void put(QByteArray &data, int offset, quint32 value) {
    qToLittleEndian(value, data.data() + offset);
}
bool write(const QString &path, const QByteArray &data) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
}
QByteArray read(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return file.readAll();
}
QByteArray ddsHeader(int w, int h, quint32 formatFlags, quint32 fourcc = 0) {
    QByteArray data(128, '\0');
    data.replace(0, 4, "DDS ");
    put(data, 4, 124); put(data, 8, 0x1007);
    put(data, 12, h); put(data, 16, w);
    put(data, 76, 32); put(data, 80, formatFlags); put(data, 84, fourcc);
    put(data, 108, 0x1000);
    return data;
}
QImage pattern(int w, int h) {
    QImage image(w, h, QImage::Format_RGBA8888);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            image.setPixelColor(x, y, QColor((x * 37) % 256, (y * 61) % 256, 80, (x * 51) % 256));
    return image;
}
void conversionTests(const QString &dir) {
    QString error;
    const QString png = dir + "/odd width.png";
    const QImage original = pattern(9, 3);
    check(original.save(png), "write PNG fixture");
    AceConverter::Image image;
    check(AceConverter::load(png, image, error), "load PNG: " + error);
    check(image.pixels == original && image.transparency, "RGBA bytes and dimensions retained");
    check(image.format == "PNG", "source format metadata retained after reading");
    AceWriteOptions options;
    options.encoding = AceEncoding::Rgba;
    options.zlib = true;
    const QString ace = dir + "/roundtrip.ace";
    check(AceConverter::save(image, ace, options, error), "save RGBA ACE: " + error);
    AceConverter::Image decoded;
    check(AceConverter::load(ace, decoded, error), "load RGBA ACE: " + error);
    check(decoded.pixels == original, "odd-width RGBA ACE round trip is lossless");
    check(decoded.zlibEnvelope == true && !decoded.storage.contains("zlib"), "ACE envelope metadata separate from pixel storage");
    check(AceConverter::save(decoded, dir + "/roundtrip.png", {}, error), "ACE to PNG");
    check(QImage(dir + "/roundtrip.png").convertToFormat(QImage::Format_RGBA8888) == original,
          "PNG round trip preserves alpha");
    const QByteArray before = read(ace);
    options.mipmaps = true;
    check(!AceConverter::save(image, ace, options, error) && error.contains("square"), "invalid mip size diagnosed");
    check(read(ace) == before, "failed ACE export preserves existing output");
    check(!AceConverter::save(image, dir + "/bad.unknown", {}, error), "unsupported output rejected");
    const auto previous = image.pixels;
    check(!AceConverter::load(dir + "/missing.ace", image, error) && image.pixels == previous,
          "failed load preserves previous source");

    image.pixels = pattern(8, 8);
    image.mask = QByteArray(64, char(1));
    image.mask[0] = 0;
    for (const auto &entry : AceConverter::encodings()) {
        options.encoding = entry.value;
        options.mipmaps = true;
        options.zlib = true;
        const QString path = dir + '/' + entry.key + ".ace";
        check(AceConverter::save(image, path, options, error), QString("save %1: %2").arg(entry.key, error));
        AceDocument doc;
        check(AceDocument::read(path, doc, error) && doc.levels.size() == 4,
              QString("read %1 with mip chain").arg(entry.key));
        QByteArray rgba, mask;
        int components = 0;
        check(doc.decode(0, rgba, components, error, &mask), QString("decode %1").arg(entry.key));
        if (entry.value == AceEncoding::Rgba)
            check(mask == image.mask, "independent planar ACE mask preserved");
    }
    // Three-byte source rows must not leak Qt padding into ACE output.
    QImage rgb(3, 2, QImage::Format_RGB888);
    rgb.fill(QColor(23, 45, 67));
    check(rgb.save(dir + "/rgb.bmp"), "write RGB fixture");
    check(AceConverter::load(dir + "/rgb.bmp", image, error), "read RGB fixture");
    options = {};
    check(AceConverter::save(image, dir + "/rgb.ace", options, error), "write odd-width RGB ACE");
    check(AceConverter::load(dir + "/rgb.ace", decoded, error) &&
          decoded.pixels.pixelColor(2, 1) == QColor(23, 45, 67), "RGB row padding preserved correctly");
}
void ddsTests(const QString &dir) {
    const QString path = dir + "/fixture.dds";
    QImage image;
    DdsImageInfo info;
    QString error;
    for (int mode = 1; mode <= 5; ++mode) {
        QByteArray header = ddsHeader(4, 4, 4, 0x30545844 + (quint32(mode) << 24));
        // An opaque red block, with independent known BC1/2/3 bytes.
        QByteArray block(8, '\0');
        block[0] = char(0x00); block[1] = char(0xf8);
        if (mode == 2 || mode == 3) block.prepend(QByteArray(8, char(0xff)));
        if (mode == 4 || mode == 5) {
            QByteArray alpha(8, '\0'); alpha[0] = char(255); alpha[1] = char(255);
            block.prepend(alpha);
        }
        check(write(path, header + block), "write DDS DXT fixture");
        check(DdsLib::loadImage(path, image, info, error), QString("DDS DXT%1: %2").arg(mode).arg(error));
        check(!image.isNull() && image.pixelColor(3, 3) == QColor(255, 0, 0), "DDS known red block");
        const QString sourcePath = dir + QString("/opaque-dxt%1.dds").arg(mode);
        check(write(sourcePath, header + block), "write source-format fixture");
        AceConverter::Image converted;
        check(AceConverter::load(sourcePath, converted, error), "read DDS source metadata");
        check(converted.alpha == (mode == 1 ? AceConverter::AlphaKind::Opaque : AceConverter::AlphaKind::Full),
              "opaque DXT pixels retain the source's alpha capability");
        const AceEncoding expected = mode == 1 ? AceEncoding::Dxt1
                                     : ((mode == 2 || mode == 3) ? AceEncoding::Dxt3 : AceEncoding::Dxt5);
        check(AceConverter::suggestedEncoding(converted, true) == expected,
              "source DXT recommendation and premultiplied fallback");
        check(write(path, header + block.left(block.size() - 1)), "write truncated DDS");
        check(!DdsLib::loadImage(path, image, info, error), "truncated DXT payload rejected");
    }
    QByteArray header = ddsHeader(1, 2, 0x40);
    put(header, 8, 0x100f); put(header, 20, 4); put(header, 88, 24);
    put(header, 92, 0xff0000); put(header, 96, 0xff00); put(header, 100, 0xff);
    QByteArray rows = QByteArray::fromHex("030201ff060504ff");
    check(write(path, header + rows) && DdsLib::loadImage(path, image, info, error), "DDS padded BGR rows");
    check(image.pixelColor(0, 0) == QColor(1, 2, 3) && image.pixelColor(0, 1) == QColor(4, 5, 6),
          "DDS row pitch and BGR masks");
    check(write(path, header + rows.left(7)) && !DdsLib::loadImage(path, image, info, error),
          "truncated uncompressed DDS rejected without tight-pitch fallback");
    put(header, 20, 3); put(header, 88, 16);
    put(header, 92, 0xf800); put(header, 96, 0x07e0); put(header, 100, 0x001f);
    check(write(path, header + QByteArray::fromHex("00f800e00700")) &&
          DdsLib::loadImage(path, image, info, error), "DDS RGB565 with padding");
    check(image.pixelColor(0, 0) == QColor(255, 0, 0) && image.pixelColor(0, 1) == QColor(0, 255, 0),
          "DDS packed channel scaling");
    header = ddsHeader(1, 1, 0x41);
    put(header, 88, 32); put(header, 92, 0xff); put(header, 96, 0xff00);
    put(header, 100, 0xff0000); put(header, 104, 0xff000000);
    check(write(path, header + QByteArray::fromHex("01020380")) &&
          DdsLib::loadImage(path, image, info, error) && image.pixelColor(0, 0) == QColor(1, 2, 3, 128),
          "DDS RGBA masks and alpha");
    put(header, 80, 0x40);
    check(write(path, header + QByteArray::fromHex("01020300")) &&
          DdsLib::loadImage(path, image, info, error) && image.pixelColor(0, 0).alpha() == 255,
          "unused DDS alpha byte treated as opaque");
    put(header, 96, 0xff);
    check(write(path, header + rows) && !DdsLib::loadImage(path, image, info, error), "overlapping masks rejected");
    header = ddsHeader(4, 4, 4, 0x30315844);
    check(write(path, header + rows) && !DdsLib::loadImage(path, image, info, error) && error.contains("DX10"),
          "DX10 diagnosis");
    put(header, 84, 0x31545844); put(header, 112, 0x200);
    check(write(path, header + rows) && !DdsLib::loadImage(path, image, info, error), "cubemap rejected");
    put(header, 112, 0); put(header, 16, 0x7fffffff);
    check(write(path, header + rows) && !DdsLib::loadImage(path, image, info, error), "oversized DDS rejected");
    check(write(path, header.left(80)) && !DdsLib::loadImage(path, image, info, error), "truncated DDS header rejected");
}
void commandTests(const QString &dir, const QString &app) {
    auto run = [&](const QStringList &args, int expected, bool vectorImage = false) {
        QProcess process;
        auto env = QProcessEnvironment::systemEnvironment();
        env.insert("QT_QPA_PLATFORM", vectorImage ? "offscreen" : "intentionally-invalid-for-headless-test");
        process.setProcessEnvironment(env);
        process.setWorkingDirectory(dir);
        process.start(app, QStringList{"--aceconv"} + args);
        const bool finished = process.waitForFinished(30000);
        if (!finished) { process.kill(); process.waitForFinished(); }
        const QByteArray output = process.readAllStandardError();
        check(finished && process.exitStatus() == QProcess::NormalExit && process.exitCode() == expected,
              "CLI " + args.join(' ') + " exit=" + QString::number(process.exitCode()) + ' ' + QString::fromLocal8Bit(output));
    };
    run({"--help"}, 0);
    run({"--file", "odd width.png", "--output", "cli.ace", "--ace-format", "rgba", "--zlib"}, 0);
    run({"cli.ace", "--output", "cli.png"}, 0);
    check(QImage(dir + "/cli.png").convertToFormat(QImage::Format_RGBA8888) == pattern(9, 3), "CLI image round trip");
    const QByteArray before = read(dir + "/cli.ace");
    run({"odd width.png", "--output", "cli.ace"}, 1);
    check(read(dir + "/cli.ace") == before, "CLI protects existing output");
    run({"odd width.png", "--output", "cli.ace", "--overwrite", "--mipmaps"}, 1);
    check(read(dir + "/cli.ace") == before, "CLI failed overwrite preserves output");
    run({"odd width.png", "--output", "cli.ace", "--overwrite"}, 0);
    run({"odd width.png", "--output", "other.png", "--ace-format", "dxt5"}, 2);
    run({"odd width.png", "--output", "other.ace", "--ace-format", "unknown"}, 2);
    run({"--output", "missing.ace"}, 2);
    run({"--file", "missing.png", "--output", "missing.ace"}, 1);
    run({"--nonsense"}, 2);
    run({"--file", "odd width.png", "extra.png"}, 2);
    if (QImageReader::supportedImageFormats().contains("svg")) {
        check(write(dir + "/vector.svg", "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"32\" height=\"32\"><rect width=\"32\" height=\"32\" fill=\"red\"/><text x=\"2\" y=\"20\">A</text></svg>"),
              "write Qt SVG fixture");
        run({"vector.svg", "--output", "vector.ace"}, 0, true);
    }
}
void guiTests(const QString &dir, const QString &snapshot) {
    QImage image(1024, 512, QImage::Format_RGBA8888);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    QLinearGradient gradient(0, 0, 1024, 512);
    gradient.setColorAt(0, QColor(25, 70, 90)); gradient.setColorAt(1, QColor(70, 180, 150));
    painter.setBrush(gradient); painter.setPen(Qt::NoPen);
    painter.drawRoundedRect(QRectF(32, 32, 960, 448), 40, 40);
    painter.setPen(Qt::white); painter.setFont(QFont("Segoe UI", 42, QFont::Bold));
    painter.drawText(QRect(85, 135, 850, 90), "ACE CONVERTER");
    painter.setFont(QFont("Segoe UI", 20));
    painter.drawText(QRect(90, 245, 850, 60), "Texture preview • RGBA • 1024 × 512");
    painter.end();
    check(image.save(dir + "/preview.png"), "write preview fixture");
    const bool appPaletteIsDark = QApplication::palette().color(QPalette::Window).lightnessF() < 0.5;
    const QColor headingColor(appPaletteIsDark ? "#c4a480" : "#1565c0");
    AceConverterWindow window(headingColor);
    window.show();
    window.loadFile(dir + "/preview.png");
    QElapsedTimer timer;
    timer.start();
    while (window.isBusy() && timer.elapsed() < 10000) {
        QApplication::processEvents(); QThread::msleep(5);
    }
    QApplication::processEvents();
    check(!window.isBusy(), "GUI async image load completes");
    auto *info = window.findChild<QLineEdit *>("sourceDimensions");
    auto *mips = window.findChild<QCheckBox *>("aceMipmaps");
    auto *view = window.findChild<QGraphicsView *>("texturePreview");
    auto *encoding = window.findChild<QComboBox *>("aceEncoding");
    check(info && info->text().contains("1024"), "GUI image metadata");
    check(info && info->isReadOnly(), "metadata presented in read-only input boxes");
    check(info && info->palette().color(QPalette::Base) == QApplication::palette().color(QPalette::Base) &&
          window.palette().color(QPalette::Window) == QApplication::palette().color(QPalette::Window),
          "converter and input fields inherit the application palette");
    check(window.findChild<QLabel *>("aceExportHeading")->styleSheet().contains(headingColor.name()) &&
          window.findChild<QLabel *>("imageExportHeading")->styleSheet().contains(headingColor.name()),
          "ACE and image export sections use TSRE's supplied main-label color");
    check(mips && !mips->isEnabled(), "GUI disables non-square mipmaps");
    check(encoding && encoding->currentData().toInt() == int(AceEncoding::Rgba), "GUI alpha-aware default");
    auto *recommended = window.findChild<QCheckBox *>("recommendedFormatsOnly");
    auto *sourceMatch = window.findChild<QCheckBox *>("sourceFormatsOnly");
    check(recommended->isChecked() && sourceMatch->isChecked(), "both format filters default on");
    check(encoding->count() == 3 && encoding->findData(int(AceEncoding::Rgb)) < 0 &&
          encoding->findData(int(AceEncoding::Dxt2)) < 0, "RGBA source shows recommended full-alpha formats");
    sourceMatch->setChecked(false);
    check(encoding->count() == 7 && encoding->findData(int(AceEncoding::Rgb)) >= 0,
          "source filter can be disabled independently");
    recommended->setChecked(false);
    check(encoding->count() == AceConverter::encodings().size(), "disabling both filters exposes all formats");
    encoding->setCurrentIndex(encoding->findData(int(AceEncoding::Dxt4)));
    recommended->setChecked(true);
    check(encoding->findData(int(AceEncoding::Dxt4)) < 0 && encoding->currentIndex() >= 0,
          "filtering a selected advanced format chooses a valid fallback");
    sourceMatch->setChecked(true);
    check(view && view->horizontalScrollBar()->maximum() > 0, "native preview scrollbars");
    window.findChild<QPushButton *>("fitPreview")->click();
    QApplication::processEvents();
    check(view->transform().m11() < 1.0, "fit preview scales down");
    if (!snapshot.isEmpty()) check(window.grab().save(snapshot), "save GUI review snapshot");
    window.findChild<QPushButton *>("actualSize")->click();
    check(view->transform().m11() == 1.0, "native-size action resets scale");
    // Exercise both real save-dialog/button paths, not just the backend writer.
    for (const auto &entry : {qMakePair(QString("exportAce"), QString("gui-export.ace")),
                              qMakePair(QString("exportImage"), QString("gui-export.png"))}) {
        const QString destination = dir + '/' + entry.second;
        QTimer::singleShot(0, &window, [&window, destination] {
            auto *dialog = window.findChild<QFileDialog *>();
            check(dialog != nullptr, "GUI export opens save dialog");
            if (dialog) {
                dialog->selectFile(destination);
                QMetaObject::invokeMethod(dialog, "accept", Qt::DirectConnection);
            }
        });
        window.findChild<QPushButton *>(entry.first)->click();
        timer.restart();
        while (window.isBusy() && timer.elapsed() < 10000) {
            QApplication::processEvents(); QThread::msleep(5);
        }
        AceConverter::Image exported;
        QString error;
        check(!window.isBusy() && AceConverter::load(destination, exported, error) &&
              exported.pixels == image, "GUI export button round trip: " + entry.second + ' ' + error);
    }
    auto openSource = [&](const QString &name) {
        window.loadFile(dir + '/' + name);
        timer.restart();
        while (window.isBusy() && timer.elapsed() < 10000) {
            QApplication::processEvents(); QThread::msleep(5);
        }
        check(!window.isBusy(), "load source for format filter: " + name);
    };
    openSource("rgb.bmp");
    check(encoding->count() == 2 && encoding->findData(int(AceEncoding::Rgb)) >= 0 &&
          encoding->findData(int(AceEncoding::Dxt1)) >= 0 && encoding->findData(int(AceEncoding::Rgba)) < 0,
          "RGB source hides all alpha formats");
    recommended->setChecked(false);
    check(encoding->count() == 4 && encoding->findData(int(AceEncoding::Rgb565)) >= 0,
          "advanced filter expands RGB choices while retaining source filtering");
    recommended->setChecked(true);
    openSource("opaque-dxt5.dds");
    check(encoding->currentData().toInt() == int(AceEncoding::Dxt5) && encoding->count() == 3,
          "opaque DDS DXT5 still suggests DXT5 and retains alpha choices");
    openSource("opaque-dxt4.dds");
    check(encoding->currentData().toInt() == int(AceEncoding::Dxt5), "DDS DXT4 suggests DXT5 under simulator filter");
    recommended->setChecked(false);
    check(encoding->findData(int(AceEncoding::Dxt4)) >= 0 &&
          window.findChild<QLabel *>("formatSuggestion")->text().contains("DXT4"),
          "original DXT4 suggested when advanced formats are allowed");
}

void guiDispatchTest() {
    QByteArray program("TSRE5vc"), mode("--aceconv"), fileOption("--file"), input("relative.png");
    QByteArray profileOption("--profile"), profile("artwork");
    QByteArray overrideOption("--set"), overrideValue("core.system.systemTheme=true");
    char *argv[] = {program.data(), mode.data(), fileOption.data(), input.data(), profileOption.data(),
                    profile.data(), overrideOption.data(), overrideValue.data()};
    bool requested = false;
    const int result = AceConverter::run(8, argv, [&](const QString &path) {
        requested = true;
        check(path == "relative.png", "GUI dispatcher retains source path for TSRE initialization");
        return 27;
    });
    check(requested && result == 27, "GUI with TSRE profile/overrides delegates to main application setup");
}
}

int main(int argc, char **argv) {
    for (int i = 1; i < argc; ++i)
        if (QByteArray(argv[i]) == "--aceconv") return AceConverter::run(argc, argv);
    QApplication app(argc, argv);
#ifdef Q_OS_WIN
    // Qt's Windows offscreen plugin does not enumerate system fonts itself.
    // Register fonts for reproducible, readable visual-review snapshots.
    if (qEnvironmentVariable("QT_QPA_PLATFORM") == "offscreen") {
        const QString fonts = qEnvironmentVariable("WINDIR") + "/Fonts/";
        QFontDatabase::addApplicationFont(fonts + "segoeui.ttf");
        QFontDatabase::addApplicationFont(fonts + "segoeuib.ttf");
        app.setFont(QFont("Segoe UI", 9));
    }
#endif
    app.setStyle("Fusion");
    QTemporaryDir directory;
    if (!directory.isValid()) return 1;
    const QStringList args = app.arguments();
    auto value = [&](const QString &key) {
        const int index = args.indexOf(key);
        return index < 0 ? QString() : args.value(index + 1);
    };
    conversionTests(directory.path());
    ddsTests(directory.path());
    commandTests(directory.path(), value("--app").isEmpty() ? app.applicationFilePath() : value("--app"));
    guiDispatchTest();
    if (args.contains("--dark-palette")) {
        QPalette dark = app.palette();
        dark.setColor(QPalette::Window, QColor(53, 53, 53));
        dark.setColor(QPalette::Base, QColor(25, 25, 25));
        dark.setColor(QPalette::AlternateBase, QColor(53, 53, 53));
        dark.setColor(QPalette::Text, Qt::white);
        dark.setColor(QPalette::WindowText, Qt::white);
        dark.setColor(QPalette::Button, QColor(53, 53, 53));
        dark.setColor(QPalette::ButtonText, Qt::white);
        dark.setColor(QPalette::Highlight, QColor(240, 130, 0));
        dark.setColor(QPalette::HighlightedText, Qt::black);
        dark.setColor(QPalette::Disabled, QPalette::Text, QColor(153, 153, 153));
        dark.setColor(QPalette::Disabled, QPalette::WindowText, QColor(153, 153, 153));
        app.setPalette(dark);
    }
    guiTests(directory.path(), value("--snapshot"));
    fprintf(stdout, "Ace Converter checks %d failures %d\n", checks, failures);
    return failures ? 1 : 0;
}
