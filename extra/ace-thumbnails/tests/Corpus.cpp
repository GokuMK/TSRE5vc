// This file is built twice: against the private shim and (optionally) real Qt.
// Cross-read each other's ACE files and compare pixels, masks and metadata.
#include <tsre/texture/AceDocument.h>
#include "ThumbnailReference.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
namespace fs = std::filesystem;
void save(const fs::path& p, const QByteArray& data) {
    std::ofstream f(p, std::ios::binary);
    f.write(data.constData(), data.size());
    if (!f) throw std::runtime_error("Corpus write failed");
}
QByteArray load(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) throw std::runtime_error("Corpus read failed");
    std::string s((std::istreambuf_iterator<char>(f)), {});
    return QByteArray(s.data(), s.size());
}
void fingerprint(const AceDocument& doc, const fs::path& path) {
    std::ofstream f(path, std::ios::binary);
    f << doc.surface() << ' ' << doc.options() << ' ' << doc.levels.size() << ' '
      << doc.hasAlpha() << ' ' << doc.compressedEnvelope << '\n';
    f.write(doc.metadata.header.constData(), doc.metadata.header.size());
    for (const auto& c : doc.metadata.channels) f << c.id << ':' << c.bits << ';';
    for (const auto& p : doc.metadata.palettes) {
        f << p.count << ':' << p.stride << ':' << p.type << ';';
        f.write(p.data.constData(), p.data.size());
    }
    for (int i = 0; i < doc.levels.size(); ++i) {
        QByteArray pixels, mask; QString error; int components = 0;
        if (!doc.decode(i, pixels, components, error, &mask)) throw std::runtime_error(error.toStdString());
        f << doc.levels[i].width << ':' << doc.levels[i].height << ':' << components << ':' << mask.size() << ';';
        f.write(pixels.constData(), pixels.size()); f.write(mask.constData(), mask.size());
        QByteArray thumbnail;
        const int tw = std::min(3, doc.levels[i].width), th = std::min(3, doc.levels[i].height);
        if (!doc.decodeThumbnail(i, tw, th, thumbnail, error) ||
            !thumbnailMatches(thumbnail, thumbnailReference(doc, i, tw, th)))
            throw std::runtime_error("Strip thumbnail differs from full-frame reference");
        f.write(thumbnail.constData(), thumbnail.size());
    }
    f.write(doc.metadata.trailing.constData(), doc.metadata.trailing.size());
    if (!f) throw std::runtime_error("Fingerprint write failed");
}
int main(int argc, char** argv) {
    try {
        if (argc != 3) return 2;
        const fs::path directory(argv[2]);
        if (std::string(argv[1]) == "generate") {
            fs::create_directories(directory);
            int count = 0;
            for (int format = 0; format < 14; ++format)
                for (int side : {1, 4, 8, 32})
                    for (bool mips : {false, true}) for (bool zipped : {false, true}) {
                        QByteArray pixels(side * side * 4, '\0');
                        const unsigned char colors[4][4] = {{255,0,0,255},{0,255,0,170},{0,0,255,85},{255,255,0,0}};
                        for (int y = 0; y < side; ++y) for (int x = 0; x < side; ++x)
                            for (int k = 0; k < 4; ++k)
                                pixels[(y * side + x) * 4 + k] = colors[(x >= side / 2) + 2 * (y >= side / 2)][k];
                        AceWriteOptions options;
                        options.encoding = static_cast<AceEncoding>(format);
                        options.mipmaps = mips;
                        AceDocument doc, parsed; QString error; QByteArray bytes;
                        if (!AceDocument::fromPixels(reinterpret_cast<const unsigned char*>(pixels.constData()), pixels.size(),
                                                      side, side, 4, options, doc, error)) throw std::runtime_error(error.toStdString());
                        doc.metadata.trailing = QByteArray("test\0metadata", 13);
                        if (!doc.serialize(bytes, zipped, error) || !AceDocument::parse(bytes, parsed, error))
                            throw std::runtime_error(error.toStdString());
                        const auto base = directory / std::to_string(count++);
                        save(base.string() + ".ace", bytes);
                        fingerprint(parsed, base.string() + ".expected");
                    }
            std::cout << "Generated " << count << " codec cases\n";
        } else if (std::string(argv[1]) == "verify") {
            int count = 0;
            for (const auto& item : fs::directory_iterator(directory)) {
                if (item.path().extension() != ".ace") continue;
                const QByteArray bytes = load(item.path());
                AceDocument doc; QString error;
                if (!AceDocument::parse(bytes, doc, error)) throw std::runtime_error(error.toStdString());
                auto expected = item.path(); expected.replace_extension(".expected");
                auto actual = item.path(); actual.replace_extension(".actual");
                fingerprint(doc, actual);
                if (load(expected) != load(actual)) throw std::runtime_error("Qt/std decoder mismatch: " + item.path().string());
                ++count;
            }
            if (count != 224) throw std::runtime_error("Incomplete codec corpus");
            std::cout << "Verified " << count << " codec cases\n";
        } else return 2;
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
