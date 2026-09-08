#include "AceConverter.h"
#include "AceConverterWindow.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QFileInfo>
#include <QImageReader>
#include <QTextStream>
#include <memory>

namespace AceConverter {
int run(int argc, char **argv, const std::function<int(const QString &)> &launchGui) {
    QCommandLineParser parser;
    parser.setSingleDashWordOptionMode(QCommandLineParser::ParseAsLongOptions);
    parser.setApplicationDescription("Ace Converter: preview images or convert one file without opening a window.");
    parser.addHelpOption();
    parser.addOption({"aceconv", "Run Ace Converter."});
    parser.addOption({"file", "Input ACE, DDS or image file.", "path"});
    parser.addOption({"output", "Output file; runs a console conversion and exits.", "path"});
    QStringList names;
    for (const auto &entry : encodings()) names << QLatin1String(entry.key);
    parser.addOption({"ace-format", "ACE pixel format: " + names.join(", ") + ". Default: rgba for transparency, otherwise rgb.", "format"});
    parser.addOption({"mipmaps", "Generate ACE mipmaps (square, power-of-two images only)."});
    parser.addOption({"zlib", "Compress the ACE file envelope with zlib."});
    parser.addOption({"overwrite", "Allow a console conversion to replace an existing output file."});
    parser.addOption({"profile", "TSRE settings profile for the GUI.", "name"});
    parser.addOption({"settings", "TSRE settings JSON file for the GUI.", "file"});
    parser.addOption({"appdata-profile", "Use the TSRE user-application-data settings profile for the GUI."});
    parser.addOption({"set", "Override a TSRE GUI setting (key=value).", "key=value"});
    parser.addPositionalArgument("input", "Optional input path instead of --file.", "[input]");
    QStringList args;
    for (int i = 0; i < argc; ++i) args << QString::fromLocal8Bit(argv[i]);
    auto usageError = [](const QString &message) {
        QTextStream(stderr) << "Ace Converter: " << message << "\nUse --aceconv --help for usage.\n";
        return 2;
    };
    if (!parser.parse(args)) return usageError(parser.errorText());
    // Help and ordinary raster conversion do not need a GUI platform plugin.
    if (parser.isSet("help")) {
        QCoreApplication app(argc, argv);
        QTextStream(stdout) << parser.helpText();
        return 0;
    }
    const QStringList positional = parser.positionalArguments();
    if (positional.size() > 1 || (parser.isSet("file") && !positional.isEmpty()))
        return usageError("Supply one input path, using --file or a positional argument.");
    const QString input = parser.isSet("file") ? parser.value("file") : positional.value(0);
    if (parser.isSet("file") && input.isEmpty()) return usageError("The input path is empty.");
    const bool console = parser.isSet("output");
    if (!console && (parser.isSet("ace-format") || parser.isSet("mipmaps") || parser.isSet("zlib") || parser.isSet("overwrite")))
        return usageError("Export arguments require --output; choose export settings in the GUI.");
    if (!console) {
        if (launchGui) return launchGui(input);
        QApplication app(argc, argv);
        app.setApplicationName("Ace Converter");
        app.setStyle("Fusion");
        AceConverterWindow window;
        window.show();
        if (!input.isEmpty()) window.loadFile(input);
        return app.exec();
    }
    const QString output = parser.value("output");
    if (parser.isSet("profile") || parser.isSet("settings") || parser.isSet("appdata-profile") || parser.isSet("set"))
        return usageError("TSRE settings arguments apply to GUI launches, not console conversion.");
    if (input.isEmpty() || output.isEmpty()) return usageError("Console conversion requires input and output paths.");
    const bool ace = QFileInfo(output).suffix().compare("ace", Qt::CaseInsensitive) == 0;
    if (!ace && (parser.isSet("ace-format") || parser.isSet("mipmaps") || parser.isSet("zlib")))
        return usageError("ACE export arguments require an .ace output file.");
    AceWriteOptions options;
    if (parser.isSet("ace-format") && !parseEncoding(parser.value("ace-format"), options.encoding))
        return usageError("Unknown ACE pixel format: " + parser.value("ace-format"));
    options.mipmaps = parser.isSet("mipmaps");
    options.zlib = parser.isSet("zlib");
    auto app = std::make_unique<QCoreApplication>(argc, argv);
    if (QFileInfo::exists(output) && !parser.isSet("overwrite")) {
        QTextStream(stderr) << "Ace Converter: Output already exists; use --overwrite to replace it.\n";
        return 1;
    }
    // Qt's optional SVG reader uses the GUI font database for text. It still
    // requires no window or OpenGL context, but does need a GUI platform.
    if (QImageReader::imageFormat(input).startsWith("svg")) {
        app.reset();
        app = std::make_unique<QGuiApplication>(argc, argv);
    }
    Image image;
    QString error;
    if (!load(input, image, error)) {
        QTextStream(stderr) << "Ace Converter: " << error << '\n';
        return 1;
    }
    if (!parser.isSet("ace-format")) options.encoding = image.transparency ? AceEncoding::Rgba : AceEncoding::Rgb;
    if (!save(image, output, options, error)) {
        QTextStream(stderr) << "Ace Converter: " << error << '\n';
        return 1;
    }
    QTextStream(stdout) << "Converted " << input << " -> " << output << '\n';
    return 0;
}
}
