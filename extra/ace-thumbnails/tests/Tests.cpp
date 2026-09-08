#include "Identity.h"
#include "Thumbnail.h"
#include <propsys.h>
#include <shlwapi.h>
#include <shlobj.h>
#include <tsre/texture/AceDocument.h>
#include <QtEndian>
#include <QFile>
#include <QSaveFile>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <stdexcept>
#include <vector>
#include <chrono>
#include <psapi.h>
#include "ThumbnailReference.h"

namespace {
int checks = 0;
void check(bool ok, const char* message) {
    ++checks;
    if (!ok) throw std::runtime_error(message);
}
template<class T> struct Com {
    T* p = nullptr;
    ~Com() { if (p) p->Release(); }
    T* operator->() const { return p; }
    Com() = default;
    Com(const Com&) = delete;
    Com& operator=(const Com&) = delete;
};
struct Bitmap {
    HBITMAP handle = nullptr;
    ~Bitmap() { if (handle) DeleteObject(handle); }
    DIBSECTION info() const {
        DIBSECTION value{};
        check(GetObjectW(handle, sizeof(value), &value) == sizeof(value), "GetObject DIB");
        return value;
    }
};
using GetFactory = HRESULT (WINAPI*)(REFCLSID, REFIID, void**);
using DllFunction = HRESULT (WINAPI*)();
GetFactory getFactory;
DllFunction canUnload;
void provider(Com<IThumbnailProvider>& out, IStream* stream = nullptr) {
    Com<IClassFactory> factory;
    check(getFactory(CLSID_AceThumbnails, IID_IClassFactory, reinterpret_cast<void**>(&factory.p)) == S_OK, "Factory");
    check(factory->CreateInstance(nullptr, __uuidof(IThumbnailProvider), reinterpret_cast<void**>(&out.p)) == S_OK, "Create provider");
    if (stream) {
        Com<IInitializeWithStream> init;
        check(out->QueryInterface(IID_IInitializeWithStream, reinterpret_cast<void**>(&init.p)) == S_OK, "Stream interface");
        check(init->Initialize(stream, STGM_READ) == S_OK, "Initialize");
    }
}
QByteArray fixture(int width, int height, AceEncoding encoding, bool zipped = false, bool mips = false) {
    QByteArray pixels(width * height * 4, '\0');
    const unsigned char colors[4][4] = {{255,0,0,255},{0,255,0,170},{0,0,255,85},{255,255,0,0}};
    for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x)
        for (int k = 0; k < 4; ++k)
            pixels[(y * width + x) * 4 + k] = colors[(x >= width / 2) + 2 * (y >= height / 2)][k];
    AceWriteOptions options; options.encoding = encoding; options.mipmaps = mips;
    AceDocument doc; QByteArray file; QString error;
    check(AceDocument::fromPixels(reinterpret_cast<const unsigned char*>(pixels.constData()), pixels.size(),
                                  width, height, 4, options, doc, error), "Build ACE fixture");
    check(doc.serialize(file, zipped, error), "Serialize fixture");
    return file;
}
HRESULT thumbnail(const QByteArray& bytes, UINT edge, Bitmap& bitmap, WTS_ALPHATYPE& alpha) {
    Com<IStream> stream;
    stream.p = SHCreateMemStream(reinterpret_cast<const BYTE*>(bytes.constData()), static_cast<UINT>(bytes.size()));
    check(stream.p != nullptr, "Memory stream");
    Com<IThumbnailProvider> object; provider(object, stream.p);
    return object->GetThumbnail(edge, &bitmap.handle, &alpha);
}
// Scripted stream errors, partial reads and metadata exercise the actual COM boundary.
class FaultStream final : public IStream {
    LONG refs_ = 1;
    IStream* inner_;
public:
    enum Mode { Partial, Short, ReadError, SeekError, StatError, Oversized } mode;
    FaultStream(const QByteArray& file, Mode m) : mode(m) {
        inner_ = SHCreateMemStream(reinterpret_cast<const BYTE*>(file.constData()), static_cast<UINT>(file.size()));
        if (!inner_) throw std::bad_alloc();
    }
    ~FaultStream() { inner_->Release(); }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
        if (!out) return E_POINTER; *out = nullptr;
        if (iid != IID_IUnknown && iid != IID_IStream && iid != IID_ISequentialStream) return E_NOINTERFACE;
        *out = static_cast<IStream*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&refs_); }
    ULONG STDMETHODCALLTYPE Release() override { LONG n = InterlockedDecrement(&refs_); if (!n) delete this; return n; }
    HRESULT STDMETHODCALLTYPE Read(void* p, ULONG size, ULONG* got) override {
        if (mode == Short) { *got = 0; return S_FALSE; }
        if (mode == ReadError) { *got = 0; return STG_E_READFAULT; }
        return inner_->Read(p, mode == Partial ? std::min<ULONG>(size, 7) : size, got);
    }
    HRESULT STDMETHODCALLTYPE Seek(LARGE_INTEGER n, DWORD origin, ULARGE_INTEGER* pos) override {
        return mode == SeekError ? STG_E_SEEKERROR : inner_->Seek(n, origin, pos);
    }
    HRESULT STDMETHODCALLTYPE Stat(STATSTG* stat, DWORD flags) override {
        if (mode == StatError) return STG_E_ACCESSDENIED;
        HRESULT hr = inner_->Stat(stat, flags);
        if (mode == Oversized) stat->cbSize.QuadPart = AceThumbnail::MaxBytes + 1ULL;
        return hr;
    }
    HRESULT STDMETHODCALLTYPE Write(const void*, ULONG, ULONG*) override { return STG_E_ACCESSDENIED; }
    HRESULT STDMETHODCALLTYPE SetSize(ULARGE_INTEGER) override { return STG_E_ACCESSDENIED; }
    HRESULT STDMETHODCALLTYPE CopyTo(IStream*, ULARGE_INTEGER, ULARGE_INTEGER*, ULARGE_INTEGER*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Commit(DWORD) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Revert() override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE LockRegion(ULARGE_INTEGER, ULARGE_INTEGER, DWORD) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE UnlockRegion(ULARGE_INTEGER, ULARGE_INTEGER, DWORD) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Clone(IStream**) override { return E_NOTIMPL; }
};
void savePreview(const Bitmap& bitmap, const std::filesystem::path& path) {
    const auto dib = bitmap.info();
    const int w = dib.dsBm.bmWidth, h = dib.dsBm.bmHeight;
    const int stride = (w * 3 + 3) & ~3;
    std::vector<unsigned char> pixels(stride * h);
    const auto* src = static_cast<const unsigned char*>(dib.dsBm.bmBits);
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
        const auto* p = src + (y * w + x) * 4;
        const int background = ((x / 12 + y / 12) % 2) ? 210 : 245;
        for (int k = 0; k < 3; ++k) pixels[y * stride + x * 3 + k] =
            static_cast<unsigned char>(std::min(255, p[k] + (background * (255 - p[3]) + 127) / 255));
    }
    BITMAPFILEHEADER file{}; file.bfType = 0x4d42;
    file.bfOffBits = sizeof(file) + sizeof(BITMAPINFOHEADER); file.bfSize = file.bfOffBits + pixels.size();
    BITMAPINFOHEADER info{}; info.biSize = sizeof(info); info.biWidth = w; info.biHeight = -h;
    info.biPlanes = 1; info.biBitCount = 24; info.biSizeImage = pixels.size();
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(&file), sizeof(file));
    out.write(reinterpret_cast<const char*>(&info), sizeof(info));
    out.write(reinterpret_cast<const char*>(pixels.data()), pixels.size());
    check(bool(out), "Write preview BMP");
}
void testCompat() {
    QByteArray a("a\0bc", 4), b = a;
    b[0] = 'z'; check(a[0] == 'a' && b[0] == 'z', "Independent byte-array copies");
    check(a.size() == 4 && a.mid(1, 2) == QByteArray("\0b", 2), "Binary slicing");
    check(a.left(100) == a && a.mid(100).isEmpty(), "Slice clipping");
    QByteArray view = QByteArray::fromRawData(a.constData(), a.size());
    a.clear(); check(view.size() == 4 && view[0] == 'a', "Raw data owns bytes in shim");
    view.append(view.constData(), view.size()); check(view.size() == 8, "Self append");
    check(qCompress({}).size() == 4, "Empty compression wrapper");
    QFile read("unused"); QSaveFile write("unused");
    check(!read.open(QIODevice::ReadOnly) && !write.open(QIODevice::WriteOnly) &&
          write.write({}) == -1 && !write.commit(), "File APIs fail explicitly");
}
void testCom() {
    check(canUnload() == S_OK, "DLL initially unloadable");
    void* unknown = reinterpret_cast<void*>(1);
    check(getFactory(IID_IUnknown, IID_IClassFactory, &unknown) == CLASS_E_CLASSNOTAVAILABLE && !unknown, "Unknown CLSID");
    check(getFactory(CLSID_AceThumbnails, IID_IClassFactory, nullptr) == E_POINTER, "Null factory output");
    {
        Com<IClassFactory> factory;
        check(getFactory(CLSID_AceThumbnails, IID_IClassFactory, reinterpret_cast<void**>(&factory.p)) == S_OK, "Create factory");
        check(canUnload() == S_FALSE, "Factory keeps DLL loaded");
        check(factory->LockServer(TRUE) == S_OK && factory->LockServer(FALSE) == S_OK, "Balanced server locks");
        check(factory->LockServer(FALSE) == E_UNEXPECTED, "Unbalanced unlock rejected");
        check(factory->CreateInstance(factory.p, IID_IUnknown, &unknown) == CLASS_E_NOAGGREGATION, "Aggregation rejected");
        check(factory->CreateInstance(nullptr, IID_IStream, &unknown) == E_NOINTERFACE && !unknown, "Unknown interface rejected");
        Com<IThumbnailProvider> object; provider(object);
        Bitmap bitmap; WTS_ALPHATYPE alpha = WTSAT_ARGB;
        check(object->GetThumbnail(32, &bitmap.handle, &alpha) == CO_E_NOTINITIALIZED && !bitmap.handle && alpha == WTSAT_UNKNOWN,
              "Uninitialized provider clears outputs");
        check(object->GetThumbnail(32, nullptr, &alpha) == E_POINTER, "Null bitmap output");
        Com<IInitializeWithStream> init;
        check(object->QueryInterface(IID_IInitializeWithStream, reinterpret_cast<void**>(&init.p)) == S_OK, "Initialize interface");
        check(init->Initialize(nullptr, STGM_READ) == E_POINTER, "Null stream rejected");
        Com<IUnknown> identity1, identity2;
        check(object->QueryInterface(IID_IUnknown, reinterpret_cast<void**>(&identity1.p)) == S_OK &&
              init->QueryInterface(IID_IUnknown, reinterpret_cast<void**>(&identity2.p)) == S_OK && identity1.p == identity2.p, "COM identity");
        const auto bytes = fixture(8, 8, AceEncoding::Rgba);
        Com<IStream> stream; stream.p = SHCreateMemStream(reinterpret_cast<const BYTE*>(bytes.constData()), bytes.size());
        check(init->Initialize(stream.p, STGM_READ) == S_OK, "Initialize once");
        check(init->Initialize(stream.p, STGM_READ) == HRESULT_FROM_WIN32(ERROR_ALREADY_INITIALIZED), "Double init rejected");
        check(object->GetThumbnail(0, &bitmap.handle, &alpha) == E_INVALIDARG, "Zero edge rejected");
        check(object->GetThumbnail(4, &bitmap.handle, &alpha) == S_OK, "First thumbnail");
        Bitmap second;
        check(object->GetThumbnail(8, &second.handle, &alpha) == S_OK, "Repeated thumbnail rewinds stream");
    }
    check(canUnload() == S_OK, "COM references released");
}
void testImages() {
    for (int format = 0; format < 14; ++format) for (bool zipped : {false, true}) for (bool mips : {false, true}) {
        const auto bytes = fixture(8, 8, static_cast<AceEncoding>(format), zipped, mips);
        Bitmap bitmap; WTS_ALPHATYPE alpha;
        check(thumbnail(bytes, 4, bitmap, alpha) == S_OK, "Every encoding/envelope/mips produces thumbnail");
        const auto dib = bitmap.info();
        check(dib.dsBm.bmWidth == 4 && dib.dsBm.bmHeight == 4 && dib.dsBm.bmBitsPixel == 32, "Thumbnail dimensions and DIB depth");
        const auto* p = static_cast<const unsigned char*>(dib.dsBm.bmBits);
        for (int i = 0; i < 16; ++i) for (int k = 0; k < 3; ++k)
            check(p[4 * i + k] <= p[4 * i + 3], "Premultiplied alpha invariant");
    }
    for (auto size : {std::pair<int,int>{16, 8}, {8, 16}, {1, 32}, {32, 1}}) {
        Bitmap bitmap; WTS_ALPHATYPE alpha;
        check(thumbnail(fixture(size.first, size.second, AceEncoding::Rgb), 8, bitmap, alpha) == S_OK, "Rectangular image");
        const auto dib = bitmap.info();
        check(dib.dsBm.bmWidth <= 8 && dib.dsBm.bmHeight <= 8 && alpha == WTSAT_RGB, "Aspect bounds and opaque alpha");
        if (size.first == 16) check(dib.dsBm.bmWidth == 8 && dib.dsBm.bmHeight == 4, "Landscape aspect ratio");
        if (size.second == 16) check(dib.dsBm.bmWidth == 4 && dib.dsBm.bmHeight == 8, "Portrait aspect ratio");
    }
    Bitmap tiny; WTS_ALPHATYPE alpha;
    check(thumbnail(fixture(1, 1, AceEncoding::Rgb), UINT_MAX, tiny, alpha) == S_OK && tiny.info().dsBm.bmWidth == 1, "No upscaling and huge request capped");
    // Equal-area mix of four quadrants: transparent yellow must not contaminate RGB.
    Bitmap mixed;
    check(thumbnail(fixture(2, 2, AceEncoding::Rgba), 1, mixed, alpha) == S_OK, "Alpha-aware area filter");
    const auto* p = static_cast<const unsigned char*>(mixed.info().dsBm.bmBits);
    check(p[0] == 21 && p[1] == 43 && p[2] == 64 && p[3] == 128 && alpha == WTSAT_ARGB, "Exact BGRA premultiplied area average");
    Bitmap oriented;
    check(thumbnail(fixture(2, 2, AceEncoding::Rgba), 2, oriented, alpha) == S_OK, "Unscaled thumbnail");
    const auto* corners = static_cast<const unsigned char*>(oriented.info().dsBm.bmBits);
    check(corners[0] == 0 && corners[1] == 0 && corners[2] == 255 && corners[3] == 255 &&
          corners[8] == 85 && corners[9] == 0 && corners[10] == 0 && corners[11] == 85,
          "Top row red, bottom row blue: correct orientation and channel order");
    Bitmap preview;
    check(thumbnail(fixture(256, 128, AceEncoding::Rgba, true), 256, preview, alpha) == S_OK, "Preview");
    savePreview(preview, L"thumbnail-preview.bmp");
}
void testMalformed() {
    for (bool zipped : {false, true}) {
        const auto valid = fixture(4, 4, AceEncoding::Rgba, zipped);
        for (qsizetype n = 0; n < valid.size(); ++n) {
            Bitmap bitmap; WTS_ALPHATYPE alpha = WTSAT_ARGB;
            check(FAILED(thumbnail(valid.left(n), 32, bitmap, alpha)) && !bitmap.handle && alpha == WTSAT_UNKNOWN, "Every truncated prefix rejected");
        }
    }
    const auto valid = fixture(8, 8, AceEncoding::Rgba, true);
    for (auto mode : {FaultStream::Partial, FaultStream::Short, FaultStream::ReadError, FaultStream::SeekError, FaultStream::StatError, FaultStream::Oversized}) {
        Com<IStream> stream; stream.p = new FaultStream(valid, mode);
        Com<IThumbnailProvider> object; provider(object, stream.p);
        Bitmap bitmap; WTS_ALPHATYPE alpha;
        HRESULT hr = object->GetThumbnail(4, &bitmap.handle, &alpha);
        check(mode == FaultStream::Partial ? hr == S_OK : FAILED(hr) && !bitmap.handle && alpha == WTSAT_UNKNOWN, "Stream fault handling");
    }
    auto bomb = valid;
    qToLittleEndian<quint32>(AceThumbnail::MaxBytes + 1, bomb.data() + 8);
    Bitmap rejected; WTS_ALPHATYPE alpha;
    check(FAILED(thumbnail(bomb, 32, rejected, alpha)), "Inflation budget before allocation");
    auto huge = fixture(4, 4, AceEncoding::Rgb);
    qToLittleEndian<quint32>(16384, huge.data() + 24);
    qToLittleEndian<quint32>(16384, huge.data() + 28);
    check(FAILED(thumbnail(huge, 32, rejected, alpha)), "Pixel budget before decoding");
    std::mt19937 random(1234);
    const auto base = fixture(8, 8, AceEncoding::Dxt5);
    for (int n = 0; n < 300; ++n) {
        auto mutated = base;
        for (int k = 0; k < 3; ++k) mutated[random() % mutated.size()] = static_cast<char>(random());
        Bitmap bitmap;
        const HRESULT hr = thumbnail(mutated, 32, bitmap, alpha);
        check(SUCCEEDED(hr) ? bitmap.handle != nullptr : !bitmap.handle && alpha == WTSAT_UNKNOWN, "Mutated input returns coherent result");
    }
}
void testThumbnailDecode() {
    for (int format = 0; format < 14; ++format)
        for (auto size : {std::pair<int,int>{7,9}, {13,5}, {1,17}, {17,1}, {8,8}}) {
            AceDocument doc; QString error;
            check(AceDocument::parse(fixture(size.first, size.second, static_cast<AceEncoding>(format)), doc, error), "Strip fixture parse");
            for (int edge : {1, 3, 6, 17}) {
                const int w = std::min(edge, size.first), h = std::min(edge, size.second);
                QByteArray thumbnail;
                check(doc.decodeThumbnail(0, w, h, thumbnail, error), "Strip decode odd dimensions and partial DXT blocks");
                check(thumbnailMatches(thumbnail, thumbnailReference(doc, 0, w, h)), "Full-frame reference parity");
            }
            QByteArray unchanged("sentinel");
            check(!doc.decodeThumbnail(-1, 1, 1, unchanged, error) && unchanged == QByteArray("sentinel"), "Bad mip preserves output");
            check(!doc.decodeThumbnail(0, 1025, 1, unchanged, error), "Thumbnail edge limit");
            check(!doc.decodeThumbnail(0, 0, 1, unchanged, error), "Zero output rejected");
            doc.levels[0].data.resize(doc.levels[0].data.size() - 1);
            check(!doc.decodeThumbnail(0, 1, 1, unchanged, error) && unchanged == QByteArray("sentinel"), "Truncated strip preserves output");
        }
    // Large fixtures are encoded blocks, never full 8192 RGB(A) buffers.
    for (auto encoding : {AceEncoding::Dxt1, AceEncoding::Dxt5}) {
        AceDocument doc; QString error;
        check(AceDocument::parse(fixture(4, 4, encoding), doc, error), "Large fixture seed");
        const auto block = doc.levels[0].data;
        auto& level = doc.levels[0];
        level.width = level.height = 8192;
        level.data.resize(qsizetype(2048) * 2048 * block.size());
        for (qsizetype at = 0; at < level.data.size(); at += block.size())
            std::memcpy(level.data.data() + at, block.constData(), block.size());
        qToLittleEndian<quint32>(8192, doc.metadata.header.data() + 8);
        qToLittleEndian<quint32>(8192, doc.metadata.header.data() + 12);
        QByteArray bytes;
        check(doc.serialize(bytes, false, error), "Serialize 8192 fixture");
        doc = AceDocument();
        Bitmap bitmap; WTS_ALPHATYPE alpha;
        check(thumbnail(bytes, 32, bitmap, alpha) == S_OK, "8192 DXT without mips reaches the actual DLL");
        check(bitmap.info().dsBm.bmWidth == 32, "8192 thumbnail dimensions");
    }
}
std::wstring registryString(const wchar_t* key, const wchar_t* name = nullptr) {
    wchar_t value[32768]{}; DWORD bytes = sizeof(value);
    const LSTATUS hr = RegGetValueW(HKEY_CURRENT_USER, key, name, RRF_RT_REG_SZ, nullptr, value, &bytes);
    if (hr == ERROR_FILE_NOT_FOUND || hr == ERROR_PATH_NOT_FOUND) return {};
    check(hr == ERROR_SUCCESS, "Read test registry"); return value;
}
void setRegistry(const wchar_t* key, const wchar_t* value) {
    HKEY handle = nullptr;
    check(RegCreateKeyExW(HKEY_CURRENT_USER, key, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &handle, nullptr) == ERROR_SUCCESS, "Create test registry key");
    const LSTATUS result = RegSetValueExW(handle, nullptr, 0, REG_SZ, reinterpret_cast<const BYTE*>(value), (wcslen(value) + 1) * sizeof(wchar_t));
    RegCloseKey(handle); check(result == ERROR_SUCCESS, "Set test registry");
}
void testRegistration(HMODULE module) {
    auto reg = reinterpret_cast<DllFunction>(GetProcAddress(module, "DllRegisterServer"));
    auto unreg = reinterpret_cast<DllFunction>(GetProcAddress(module, "DllUnregisterServer"));
    check(reg && unreg, "Registration exports");
    // Process-local predefined-key override: the real .ace registration is never touched.
    struct Sandbox {
        std::wstring path = L"Software\\TSRE\\AceThumbnailTests-" + std::to_wstring(GetCurrentProcessId());
        HKEY key = nullptr;
        bool redirected = false;
        Sandbox() {
            DWORD disposition = 0;
            check(RegCreateKeyExW(HKEY_CURRENT_USER, path.c_str(), 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &key, &disposition) == ERROR_SUCCESS,
                  "Create registry sandbox");
            check(disposition == REG_CREATED_NEW_KEY, "Registry sandbox is new");
            check(RegOverridePredefKey(HKEY_CURRENT_USER, key) == ERROR_SUCCESS, "Redirect HKCU in test process");
            redirected = true;
        }
        ~Sandbox() {
            if (redirected) RegOverridePredefKey(HKEY_CURRENT_USER, nullptr);
            if (key) RegCloseKey(key);
            if (redirected) RegDeleteTreeW(HKEY_CURRENT_USER, path.c_str());
        }
    } sandbox;
    setRegistry(L"Software\\Classes\\.ace", L"Existing.Ace.Application");
    setRegistry(AceHandlerKey, L"{11111111-1111-1111-1111-111111111111}");
    check(reg() == S_OK && registryString(AceHandlerKey) == AceClassId, "Register in sandbox");
    check(registryString(L"Software\\Classes\\.ace") == L"Existing.Ace.Application", "Default application preserved");
    check(reg() == S_OK && unreg() == S_OK, "Repeated install and uninstall");
    check(registryString(AceHandlerKey) == L"{11111111-1111-1111-1111-111111111111}", "Previous provider restored");
    check(reg() == S_OK, "Reinstall");
    setRegistry(AceHandlerKey, L"{22222222-2222-2222-2222-222222222222}");
    check(unreg() == S_OK && registryString(AceHandlerKey) == L"{22222222-2222-2222-2222-222222222222}", "Replacement provider preserved");
    check(RegDeleteKeyValueW(HKEY_CURRENT_USER, AceHandlerKey, nullptr) == ERROR_SUCCESS, "Clear sandbox handler");
    check(reg() == S_OK && unreg() == S_OK && registryString(AceHandlerKey).empty(), "Absent previous override restored");
    check(unreg() == S_OK, "Repeated uninstall");
}
}
int wmain(int argc, wchar_t** argv) {
    if (argc < 2) { std::cerr << "Usage: ace_thumbnail_tests DLL [--render|--shell-render INPUT.ace OUTPUT.bmp [EDGE]]\n"; return 2; }
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(initialized)) return 1;
    HMODULE module = LoadLibraryW(argv[1]);
    if (!module) { std::cerr << "Cannot load DLL: " << GetLastError() << '\n'; CoUninitialize(); return 1; }
    int status = 0;
    try {
        getFactory = reinterpret_cast<GetFactory>(GetProcAddress(module, "DllGetClassObject"));
        canUnload = reinterpret_cast<DllFunction>(GetProcAddress(module, "DllCanUnloadNow"));
        check(getFactory && canUnload, "COM exports");
        if (argc >= 5 && (std::wstring(argv[2]) == L"--render" || std::wstring(argv[2]) == L"--shell-render")) {
            UINT edge = 256;
            if (argc == 6) {
                std::size_t used = 0; const auto value = std::stoul(argv[5], &used);
                check(used == wcslen(argv[5]) && value > 0 && value <= AceThumbnail::MaxEdge, "Edge must be 1..1024"); edge = value;
            } else check(argc == 5, "Invalid render arguments");
            Bitmap bitmap; WTS_ALPHATYPE alpha;
            const auto started = std::chrono::steady_clock::now();
            if (std::wstring(argv[2]) == L"--shell-render") {
                const auto shellPath = std::filesystem::absolute(argv[3]).make_preferred().wstring();
                Com<IShellItem> item;
                check(SHCreateItemFromParsingName(shellPath.c_str(), nullptr, IID_IShellItem,
                    reinterpret_cast<void**>(&item.p)) == S_OK, "Create shell item");
                Com<IThumbnailCache> cache;
                check(CoCreateInstance(__uuidof(LocalThumbnailCache), nullptr, CLSCTX_INPROC_SERVER,
                    __uuidof(IThumbnailCache), reinterpret_cast<void**>(&cache.p)) == S_OK, "Create Windows thumbnail cache");
                Com<ISharedBitmap> shared; WTS_CACHEFLAGS flags; WTS_THUMBNAILID id;
                const HRESULT hr = cache->GetThumbnail(item.p, edge, WTS_FORCEEXTRACTION, &shared.p, &flags, &id);
                if (FAILED(hr)) std::cerr << "Shell thumbnail HRESULT: 0x" << std::hex << hr << std::dec << '\n';
                check(SUCCEEDED(hr), "Windows shell thumbnail extraction");
                check(shared->Detach(&bitmap.handle) == S_OK, "Detach shell bitmap");
                SHChangeNotify(SHCNE_UPDATEITEM, SHCNF_PATHW, shellPath.c_str(), nullptr);
            } else {
                Com<IStream> stream;
                check(SHCreateStreamOnFileEx(argv[3], STGM_READ | STGM_SHARE_DENY_WRITE, 0, FALSE, nullptr, &stream.p) == S_OK, "Open input file");
                Com<IThumbnailProvider> object; provider(object, stream.p);
                check(object->GetThumbnail(edge, &bitmap.handle, &alpha) == S_OK, "Render ACE");
            }
            const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
            PROCESS_MEMORY_COUNTERS memory{};
            check(GetProcessMemoryInfo(GetCurrentProcess(), &memory, sizeof(memory)), "Measure process memory");
            std::cout << "Thumbnail: " << seconds << " s, process peak working set "
                      << memory.PeakWorkingSetSize / (1024.0 * 1024.0) << " MiB\n";
            savePreview(bitmap, argv[4]);
        } else {
            check(argc == 2, "Invalid arguments");
            testCompat(); testCom(); testImages(); testMalformed(); testThumbnailDecode(); testRegistration(module);
        }
        check(canUnload() == S_OK, "DLL unloadable after tests");
        std::cout << checks << " checks passed\n";
    } catch (const std::exception& e) { std::cerr << "FAIL after " << checks << " checks: " << e.what() << '\n'; status = 1; }
    FreeLibrary(module); CoUninitialize(); return status;
}
