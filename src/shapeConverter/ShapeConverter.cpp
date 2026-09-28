#include "ShapeConverter.h"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QFileInfo>
#include <QTextStream>
#include <tsre/shape/SFileDocument.h>

namespace ShapeConverter {
namespace {

bool parseFormat(const QString &value, Format &format) {
    const QString key = value.trimmed().toLower();
    if (key == "preserve")
        format = Format::Preserve;
    else if (key == "unicode" || key == "text")
        format = Format::Unicode;
    else if (key == "binary")
        format = Format::Binary;
    else
        return false;
    return true;
}

bool parseCompression(const QString &value, Compression &compression) {
    const QString key = value.trimmed().toLower();
    if (key == "preserve")
        compression = Compression::Preserve;
    else if (key == "compressed")
        compression = Compression::Compressed;
    else if (key == "uncompressed")
        compression = Compression::Uncompressed;
    else
        return false;
    return true;
}

QString formatName(Format format) {
    return format == Format::Binary ? "binary" : "unicode";
}

QString compressionName(Compression compression) {
    return compression == Compression::Compressed ? "compressed" : "uncompressed";
}

} // namespace

bool convert(const Options &options, Result &result, QString &error) {
    error.clear();
    result = {};
    const QFileInfo input(options.inputPath);
    if (!input.isFile()) {
        error = "Input shape does not exist or is not a file: " + options.inputPath;
        return false;
    }
    if (input.suffix().compare("s", Qt::CaseInsensitive) != 0) {
        error = "Input must be an MSTS .s shape file";
        return false;
    }
    if (options.outputPath.trimmed().isEmpty()) {
        error = "Output path is empty";
        return false;
    }
    const QFileInfo output(options.outputPath);
    if (output.suffix().compare("s", Qt::CaseInsensitive) != 0) {
        error = "Output must use the .s extension";
        return false;
    }
    if (output.exists() && !options.overwrite) {
        error = "Output already exists; use --overwrite or -w to replace it";
        return false;
    }

    SFileDetail::Document document;
    if (!document.read(options.inputPath) || document.damaged) {
        error = "Cannot read input shape";
        if (!document.diagnostics.isEmpty())
            error += ": " + document.diagnostics.join("; ");
        return false;
    }

    const bool binary = options.format == Format::Preserve
                            ? document.binary
                            : options.format == Format::Binary;
    const bool compressed = options.compression == Compression::Preserve
                                ? document.compressed
                                : options.compression == Compression::Compressed;
    if (!document.save(options.outputPath, binary, compressed, error))
        return false;

    result.format = binary ? Format::Binary : Format::Unicode;
    result.compression = compressed ? Compression::Compressed : Compression::Uncompressed;
    return true;
}

int run(int argc, char **argv) {
    QCommandLineParser parser;
    parser.setApplicationDescription(
        "Shape Converter: convert one MSTS .s file without opening a window.");
    parser.addHelpOption();
    parser.addOption({"shapeconv", "Run Shape Converter."});
    parser.addOption({{"i", "input"}, "Input MSTS shape file.", "path"});
    parser.addOption({{"o", "output"}, "Output MSTS shape file.", "path"});
    parser.addOption({{"f", "format"},
                      "Output format: preserve, unicode or binary. Default: preserve.",
                      "format", "preserve"});
    parser.addOption({{"c", "compression"},
                      "Output compression: preserve, compressed or uncompressed. Default: preserve.",
                      "mode", "preserve"});
    parser.addOption({{"w", "overwrite"}, "Allow replacement of an existing output file."});
    parser.addPositionalArgument("input", "Optional input path instead of --input.", "[input]");

    QStringList arguments;
    for (int i = 0; i < argc; ++i)
        arguments << QString::fromLocal8Bit(argv[i]);
    auto usageError = [](const QString &message) {
        QTextStream(stderr) << "Shape Converter: " << message
                            << "\nUse --shapeconv --help for usage.\n";
        return 2;
    };
    if (!parser.parse(arguments))
        return usageError(parser.errorText());
    if (parser.isSet("help")) {
        QCoreApplication app(argc, argv);
        QTextStream(stdout) << parser.helpText();
        return 0;
    }

    const QStringList positional = parser.positionalArguments();
    if (positional.size() > 1 || (parser.isSet("input") && !positional.isEmpty()))
        return usageError("Supply one input path, using --input/-i or a positional argument.");
    Options options;
    options.inputPath = parser.isSet("input") ? parser.value("input") : positional.value(0);
    options.outputPath = parser.value("output");
    options.overwrite = parser.isSet("overwrite");
    if (options.inputPath.isEmpty() || options.outputPath.isEmpty())
        return usageError("Conversion requires input and output paths.");
    if (!parseFormat(parser.value("format"), options.format))
        return usageError("Unknown output format: " + parser.value("format"));
    if (!parseCompression(parser.value("compression"), options.compression))
        return usageError("Unknown compression mode: " + parser.value("compression"));

    QCoreApplication app(argc, argv);
    Result result;
    QString error;
    if (!convert(options, result, error)) {
        QTextStream(stderr) << "Shape Converter: " << error << '\n';
        return 1;
    }
    QTextStream(stdout) << "Converted " << options.inputPath << " -> " << options.outputPath
                        << " (" << formatName(result.format) << ", "
                        << compressionName(result.compression) << ")\n";
    return 0;
}

} // namespace ShapeConverter
