#pragma once
#include <windows.h>
#include <objidl.h>
#include <thumbcache.h>
namespace AceThumbnail {
constexpr unsigned MaxEdge = 1024;
// Includes 8192 RGBA + mask and the complete mip chain (~352 MiB).
constexpr unsigned MaxBytes = 384 * 1024 * 1024;
constexpr unsigned MaxPixels = 8192 * 8192;
constexpr unsigned MaxDimension = 8192;
// Returns a top-down 32-bit premultiplied BGRA DIB. Caller owns the HBITMAP.
HRESULT fromStream(IStream* stream, UINT edge, HBITMAP* bitmap, WTS_ALPHATYPE* alpha);
}
