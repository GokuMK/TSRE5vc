#pragma once

#include <QString>

namespace ShapeConverter {

enum class Format { Preserve, Unicode, Binary };
enum class Compression { Preserve, Compressed, Uncompressed };

struct Options {
    QString inputPath;
    QString outputPath;
    Format format = Format::Preserve;
    Compression compression = Compression::Preserve;
    bool overwrite = false;
};

struct Result {
    Format format = Format::Preserve;
    Compression compression = Compression::Preserve;
};

bool convert(const Options &options, Result &result, QString &error);
int run(int argc, char **argv);

} // namespace ShapeConverter
