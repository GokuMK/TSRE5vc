#include "Thumbnail.h"
#include <tsre/texture/AceDocument.h>
#include <algorithm>
#include <cstring>
#include <memory>

namespace AceThumbnail {
HRESULT fromStream(IStream* stream, UINT edge, HBITMAP* bitmap, WTS_ALPHATYPE* alpha) {
    if (bitmap) *bitmap = nullptr;
    if (alpha) *alpha = WTSAT_UNKNOWN;
    if (!bitmap || !alpha || !stream) return E_POINTER;
    if (!edge) return E_INVALIDARG;
    STATSTG stat{};
    HRESULT hr = stream->Stat(&stat, STATFLAG_NONAME);
    if (FAILED(hr)) return hr;
    if (stat.cbSize.QuadPart < 16 || stat.cbSize.QuadPart > MaxBytes)
        return HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
    hr = stream->Seek(LARGE_INTEGER{}, STREAM_SEEK_SET, nullptr);
    if (FAILED(hr)) return hr;
    QByteArray file(static_cast<qsizetype>(stat.cbSize.QuadPart), Qt::Uninitialized);
    ULONG offset = 0;
    while (offset < static_cast<ULONG>(file.size())) {
        ULONG read = 0;
        const ULONG request = std::min<ULONG>(1024 * 1024, file.size() - offset);
        hr = stream->Read(file.data() + offset, request, &read);
        if (FAILED(hr)) return hr;
        if (read > request || !read) return STG_E_READFAULT;
        offset += read;
        if (hr == S_FALSE && offset != static_cast<ULONG>(file.size())) return STG_E_READFAULT;
    }
    AceReadOptions limits;
    limits.maxBytes = MaxBytes;
    limits.maxPixels = MaxPixels;
    limits.maxDimension = MaxDimension;
    AceDocument doc;
    QString error;
    if (!AceDocument::parse(file, doc, error, limits)) return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    file = QByteArray(); // Release capacity too in the std-backed compatibility layer.
    edge = std::min(edge, MaxEdge);
    int mip = 0;
    // Choose the smallest available mip that will not require upscaling.
    for (int i = 1; i < doc.levels.size(); ++i)
        if (std::max(doc.levels[i].width, doc.levels[i].height) >= static_cast<int>(edge)) mip = i;
    const int sw = doc.levels[mip].width, sh = doc.levels[mip].height;
    const int longest = std::max(sw, sh);
    const int target = std::min<int>(longest, edge);
    const int dw = std::max(1, static_cast<int>((static_cast<long long>(sw) * target + longest / 2) / longest));
    const int dh = std::max(1, static_cast<int>((static_cast<long long>(sh) * target + longest / 2) / longest));
    QByteArray pixels;
    if (!doc.decodeThumbnail(mip, dw, dh, pixels, error)) return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = dw;
    info.bmiHeader.biHeight = -dh;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP dib = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!dib) return E_OUTOFMEMORY;
    struct DeleteBitmap { void operator()(void* p) const { DeleteObject(static_cast<HBITMAP>(p)); } };
    std::unique_ptr<void, DeleteBitmap> owner(dib);
    std::memcpy(bits, pixels.constData(), static_cast<std::size_t>(pixels.size()));
    const auto* src = reinterpret_cast<const unsigned char*>(pixels.constData());
    bool transparent = false;
    for (int i = 0; i < dw * dh; ++i) transparent |= src[i * 4 + 3] != 255;
    *alpha = transparent ? WTSAT_ARGB : WTSAT_RGB;
    *bitmap = static_cast<HBITMAP>(owner.release());
    return S_OK;
}
}
