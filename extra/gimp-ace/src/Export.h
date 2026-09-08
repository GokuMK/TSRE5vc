#pragma once
#include <tsre/texture/AceDocument.h>
#include <string>

namespace AceExport {
struct Format {
    const char* id;
    const char* label;
    AceEncoding encoding;
    int alphaBits;
    bool suggested;
};
extern const Format formats[14];
const Format* findFormat(const char* id);
bool visible(const Format& format, bool suggestedOnly, bool matchSource, bool sourceAlpha);
bool validateSize(int width, int height, bool mipmaps, std::string& error);
bool encode(const unsigned char* pixels, std::size_t size, int width, int height,
            bool sourceAlpha, const char* format, bool mipmaps, bool zlib,
            QByteArray& output, std::string& error);
}
