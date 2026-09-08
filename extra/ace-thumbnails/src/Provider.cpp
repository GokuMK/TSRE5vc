#include "Identity.h"
#include "Thumbnail.h"
#include <propsys.h>
#include <new>

HMODULE aceModule = nullptr;
namespace {
LONG objects = 0, locks = 0;
class Provider final : public IThumbnailProvider, public IInitializeWithStream {
    LONG refs_ = 1;
    IStream* stream_ = nullptr;
public:
    Provider() { InterlockedIncrement(&objects); }
    ~Provider() { if (stream_) stream_->Release(); InterlockedDecrement(&objects); }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (IsEqualIID(iid, IID_IUnknown) || IsEqualIID(iid, __uuidof(IThumbnailProvider)))
            *out = static_cast<IThumbnailProvider*>(this);
        else if (IsEqualIID(iid, IID_IInitializeWithStream)) *out = static_cast<IInitializeWithStream*>(this);
        else return E_NOINTERFACE;
        AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&refs_); }
    ULONG STDMETHODCALLTYPE Release() override {
        const LONG count = InterlockedDecrement(&refs_);
        if (!count) delete this;
        return count;
    }
    HRESULT STDMETHODCALLTYPE Initialize(IStream* stream, DWORD) override {
        if (!stream) return E_POINTER;
        if (stream_) return HRESULT_FROM_WIN32(ERROR_ALREADY_INITIALIZED);
        stream->AddRef(); stream_ = stream; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetThumbnail(UINT edge, HBITMAP* bitmap, WTS_ALPHATYPE* alpha) override {
        if (bitmap) *bitmap = nullptr;
        if (alpha) *alpha = WTSAT_UNKNOWN;
        if (!bitmap || !alpha) return E_POINTER;
        if (!stream_) return CO_E_NOTINITIALIZED;
        try { return AceThumbnail::fromStream(stream_, edge, bitmap, alpha); }
        catch (const std::bad_alloc&) { return E_OUTOFMEMORY; }
        catch (...) { return E_FAIL; }
    }
};
class Factory final : public IClassFactory {
    LONG refs_ = 1;
public:
    Factory() { InterlockedIncrement(&objects); }
    ~Factory() { InterlockedDecrement(&objects); }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (!IsEqualIID(iid, IID_IUnknown) && !IsEqualIID(iid, IID_IClassFactory)) return E_NOINTERFACE;
        *out = static_cast<IClassFactory*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&refs_); }
    ULONG STDMETHODCALLTYPE Release() override {
        const LONG count = InterlockedDecrement(&refs_);
        if (!count) delete this;
        return count;
    }
    HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown* outer, REFIID iid, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (outer) return CLASS_E_NOAGGREGATION;
        auto* provider = new (std::nothrow) Provider;
        if (!provider) return E_OUTOFMEMORY;
        const HRESULT hr = provider->QueryInterface(iid, out);
        provider->Release(); return hr;
    }
    HRESULT STDMETHODCALLTYPE LockServer(BOOL lock) override {
        if (lock) InterlockedIncrement(&locks);
        else {
            LONG previous;
            do {
                previous = InterlockedCompareExchange(&locks, 0, 0);
                if (!previous) return E_UNEXPECTED;
            } while (InterlockedCompareExchange(&locks, previous - 1, previous) != previous);
        }
        return S_OK;
    }
};
}
extern "C" BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) aceModule = module;
    return TRUE;
}
extern "C" HRESULT WINAPI DllCanUnloadNow() {
    return !InterlockedCompareExchange(&objects, 0, 0) && !InterlockedCompareExchange(&locks, 0, 0) ? S_OK : S_FALSE;
}
extern "C" HRESULT WINAPI DllGetClassObject(REFCLSID clsid, REFIID iid, void** out) {
    if (!out) return E_POINTER;
    *out = nullptr;
    if (!IsEqualCLSID(clsid, CLSID_AceThumbnails)) return CLASS_E_CLASSNOTAVAILABLE;
    auto* factory = new (std::nothrow) Factory;
    if (!factory) return E_OUTOFMEMORY;
    const HRESULT hr = factory->QueryInterface(iid, out);
    factory->Release(); return hr;
}
