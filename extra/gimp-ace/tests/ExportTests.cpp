#include "Export.h"
#include <cstdlib>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <iterator>
#include <vector>

int checks = 0;
void check(bool condition, const char* message) {
    ++checks;
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
int main(int argc, char** argv) {
    // Integration mode: inspect ACE files produced by a real GIMP batch.
    if (argc > 1) {
        for (int i = 1; i < argc; ++i) {
            std::ifstream stream(argv[i], std::ios::binary);
            check(bool(stream), "GIMP output exists");
            std::string bytes((std::istreambuf_iterator<char>(stream)), {});
            AceDocument doc; QString error; QByteArray pixels; int channels = 0;
            check(AceDocument::parse(QByteArray(bytes.data(), bytes.size()), doc, error), "GIMP output parses");
            check(doc.decode(0, pixels, channels, error), "GIMP output decodes");
            check(doc.levels[0].width == 8 && doc.levels[0].height == 8, "GIMP canvas dimensions");
            const auto name = std::filesystem::path(argv[i]).filename().string();
            auto component = [&](int x, int y, int c) { return static_cast<unsigned char>(pixels[(y*8+x)*channels+c]); };
            if (name == "plain.ace" || name == "rgb.ace" || name == "rgba.ace") {
                check(component(0,0,0) == 255 && component(0,0,1) == 0 && component(0,0,2) == 0, "red base, hidden blue ignored");
                check(component(3,3,0) == 0 && component(3,3,1) == 255 && component(3,3,2) == 0, "offset green layer composited");
                check(component(7,7,0) == 255 && component(7,7,1) == 0, "canvas outside patch preserved");
            }
            if (name == "alpha.ace" || name == "opaque-offset.ace") {
                check(channels == 4 && component(0,0,3) == 0, "transparent canvas outside layer");
                check(component(3,3,0) == 255 && component(3,3,3) == 255, "offset opaque patch");
            }
            if (name == "gray.ace") check(channels == 3 && component(0,0,0) == 255 && component(0,0,1) == 255, "gray conversion");
            if (name == "linear.ace") {
                check(std::abs(int(component(0,0,0)) - 128) <= 1 && std::abs(int(component(0,0,1)) - 64) <= 1 &&
                      std::abs(int(component(0,0,2)) - 32) <= 1, "linear float converts to sRGB u8");
            }
            if (name == "half-alpha.ace") {
                check(channels == 4 && component(3,3,0) == 255 && std::abs(int(component(3,3,3)) - 128) <= 1,
                      "straight RGB and fractional alpha preserved");
            }
            std::cout << argv[i] << ": " << doc.levels.size() << " mip levels, "
                      << channels << " channels, zlib=" << doc.compressedEnvelope << '\n';
        }
        return 0;
    }
    std::vector<unsigned char> pixels(8*8*4);
    for (int y = 0; y < 8; ++y) for (int x = 0; x < 8; ++x) {
        auto* p = &pixels[(y*8+x)*4];
        p[0] = x < 4 ? 255 : 0; p[1] = y < 4 ? 255 : 0; p[2] = 85;
        p[3] = x < 4 ? 255 : 0;
    }
    QByteArray bytes; std::string error;
    for (const auto& f : AceExport::formats) for (bool mips : {false, true}) for (bool zlib : {false, true}) {
        check(AceExport::encode(pixels.data(), pixels.size(), 8, 8, true, f.id, mips, zlib, bytes, error), f.id);
        AceDocument doc; QString codecError;
        check(AceDocument::parse(bytes, doc, codecError), "parse exported encoding");
        check(doc.compressedEnvelope == zlib, "compression option");
        check(doc.levels.size() == (mips ? 4 : 1), "mipmap option");
        for (int level = 0; level < doc.levels.size(); ++level) {
            QByteArray decoded; int channels = 0;
            check(doc.decode(level, decoded, channels, codecError), "decode every level");
            check(decoded.size() == doc.levels[level].width * doc.levels[level].height * channels, "decoded size");
        }
    }
    for (bool alpha : {false, true}) {
        check(AceExport::encode(pixels.data(), pixels.size(), 8, 8, alpha, "auto", false, false, bytes, error), "automatic export");
        AceDocument doc; QString codecError; QByteArray decoded; int channels = 0;
        check(AceDocument::parse(bytes, doc, codecError) && doc.decode(0, decoded, channels, codecError), "automatic parse");
        check(channels == (alpha ? 4 : 3), "automatic alpha capability");
        if (alpha) check(decoded == QByteArray(reinterpret_cast<const char*>(pixels.data()), pixels.size()), "lossless RGBA");
    }
    check(!AceExport::encode(pixels.data(), pixels.size()-1, 8, 8, true, "rgba", false, false, bytes, error), "short buffer rejected");
    check(bytes.isEmpty(), "failure leaves no output");
    check(!AceExport::encode(pixels.data(), pixels.size(), 8, 8, true, "unknown", false, false, bytes, error), "unknown encoding rejected");
    check(!AceExport::validateSize(8, 4, true, error), "rectangular mips rejected");
    check(!AceExport::validateSize(6, 6, true, error), "non power-of-two mips rejected");
    check(AceExport::validateSize(6, 4, false, error), "rectangle without mips allowed");
    check(!AceExport::validateSize(0, 4, false, error), "zero dimension rejected");
    check(!AceExport::validateSize(16385, 1, false, error), "dimension bound");
    check(!AceExport::validateSize(16384, 16384, false, error), "pixel bound");
    for (const auto& f : AceExport::formats) {
        check(AceExport::visible(f, false, false, false), "all formats available without filters");
        if (!f.suggested) check(!AceExport::visible(f, true, false, false), "shortlist filter");
        check(AceExport::visible(f, false, true, false) == (f.alphaBits == 0), "RGB filter");
        check(AceExport::visible(f, false, true, true) == (f.alphaBits >= 8), "RGBA filter");
    }
    std::vector<unsigned char> manyColors(32*32*4, 255);
    for (int i = 0; i < 1024; ++i) { manyColors[i*4] = i % 256; manyColors[i*4+1] = i / 256; }
    check(!AceExport::encode(manyColors.data(), manyColors.size(), 32, 32, false,
                            "indexed-rgb", false, false, bytes, error), "indexed overflow rejected");
    std::cout << checks << " ACE export checks passed\n";
}
