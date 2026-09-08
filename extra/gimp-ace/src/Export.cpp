#include "Export.h"
#include <cstring>
#include <exception>

namespace AceExport {
const Format formats[] = {
    {"rgb", "RGB (24-bit)", AceEncoding::Rgb, 0, true},
    {"mask", "RGB + mask (1-bit alpha)", AceEncoding::Mask, 1, true},
    {"rgba", "RGBA (8-bit alpha)", AceEncoding::Rgba, 8, true},
    {"rgb565", "RGB 565 (16-bit)", AceEncoding::Rgb565, 0, false},
    {"argb1555", "ARGB 1555 (1-bit alpha)", AceEncoding::Argb1555, 1, false},
    {"argb4444", "ARGB 4444 (4-bit alpha)", AceEncoding::Argb4444, 4, false},
    {"dxt1", "DXT1 (opaque)", AceEncoding::Dxt1, 0, true},
    {"dxt1mask", "DXT1 (1-bit alpha)", AceEncoding::Dxt1Mask, 1, true},
    {"dxt2", "DXT2 (premultiplied alpha)", AceEncoding::Dxt2, 8, false},
    {"dxt3", "DXT3 (explicit alpha)", AceEncoding::Dxt3, 8, true},
    {"dxt4", "DXT4 (premultiplied alpha)", AceEncoding::Dxt4, 8, false},
    {"dxt5", "DXT5 (interpolated alpha)", AceEncoding::Dxt5, 8, true},
    {"indexed-rgb", "Indexed RGB (up to 256 colors)", AceEncoding::IndexedRgb, 0, false},
    {"indexed-rgba", "Indexed RGBA (up to 256 colors)", AceEncoding::IndexedRgba, 8, false}
};
const Format* findFormat(const char* id) {
    if (id) for (const auto& f : formats) if (std::strcmp(id, f.id) == 0) return &f;
    return nullptr;
}
bool visible(const Format& f, bool suggestedOnly, bool matchSource, bool sourceAlpha) {
    return (!suggestedOnly || f.suggested) &&
           (!matchSource || (sourceAlpha ? f.alphaBits >= 8 : f.alphaBits == 0));
}
bool validateSize(int w, int h, bool mipmaps, std::string& error) {
    if (w <= 0 || h <= 0 || w > 16384 || h > 16384 || std::uint64_t(w) * h > 64ULL*1024*1024) {
        error = "ACE export requires dimensions of 1..16384 and at most 64 million pixels.";
        return false;
    }
    if (mipmaps && (w != h || (w & (w - 1)))) {
        error = "Mipmaps require a square, power-of-two image. Resize the image or turn off mipmaps.";
        return false;
    }
    return true;
}
bool encode(const unsigned char* pixels, std::size_t size, int w, int h,
            bool sourceAlpha, const char* format, bool mipmaps, bool zlib,
            QByteArray& output, std::string& error) {
    output.clear();
    error.clear();
    try {
        if (!validateSize(w, h, mipmaps, error)) return false;
        if (!pixels || size != std::size_t(w) * h * 4) {
            error = "Invalid RGBA input buffer."; return false;
        }
        if (format && std::strcmp(format, "auto") == 0) format = sourceAlpha ? "rgba" : "rgb";
        const auto* f = findFormat(format);
        if (!f) { error = "Unknown ACE encoding."; return false; }
        AceWriteOptions options;
        options.encoding = f->encoding;
        options.mipmaps = mipmaps;
        AceDocument document;
        QString codecError;
        if (!AceDocument::fromPixels(pixels, static_cast<qsizetype>(size), w, h, 4,
                                     options, document, codecError) ||
            !document.serialize(output, zlib, codecError)) {
            error = codecError.toStdString(); output.clear(); return false;
        }
        return true;
    } catch (const std::exception& e) {
        error = e.what(); output.clear(); return false;
    }
}
}
