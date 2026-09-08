#include "Identity.h"
#include <shlobj.h>
#include <optional>
#include <string>
#include <vector>
#include <new>

namespace {
struct RegistryError { LSTATUS code; };
void checked(LSTATUS code) { if (code != ERROR_SUCCESS) throw RegistryError{code}; }
struct Key {
    HKEY handle = nullptr;
    ~Key() { if (handle) RegCloseKey(handle); }
};
std::optional<std::wstring> read(const wchar_t* path, const wchar_t* name = nullptr) {
    DWORD bytes = 0;
    LSTATUS result = RegGetValueW(HKEY_CURRENT_USER, path, name, RRF_RT_REG_SZ, nullptr, nullptr, &bytes);
    if (result == ERROR_FILE_NOT_FOUND || result == ERROR_PATH_NOT_FOUND) return std::nullopt;
    checked(result);
    std::vector<wchar_t> text(bytes / sizeof(wchar_t) + 1, L'\0');
    checked(RegGetValueW(HKEY_CURRENT_USER, path, name, RRF_RT_REG_SZ, nullptr, text.data(), &bytes));
    return std::wstring(text.data());
}
void write(const wchar_t* path, const wchar_t* name, const std::wstring& value) {
    Key key;
    checked(RegCreateKeyExW(HKEY_CURRENT_USER, path, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key.handle, nullptr));
    checked(RegSetValueExW(key.handle, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()),
                          static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t))));
}
void removeValue(const wchar_t* path) {
    const LSTATUS result = RegDeleteKeyValueW(HKEY_CURRENT_USER, path, nullptr);
    if (result != ERROR_FILE_NOT_FOUND && result != ERROR_PATH_NOT_FOUND) checked(result);
}
bool ours(const std::optional<std::wstring>& id) { return id && _wcsicmp(id->c_str(), AceClassId) == 0; }
void registerProvider() {
    std::vector<wchar_t> modulePath(32768);
    const DWORD length = GetModuleFileNameW(aceModule, modulePath.data(), static_cast<DWORD>(modulePath.size()));
    if (!length) throw RegistryError{static_cast<LSTATUS>(GetLastError())};
    if (length >= modulePath.size()) throw RegistryError{ERROR_INSUFFICIENT_BUFFER};
    const auto previous = read(AceHandlerKey);
    // Save only the per-user override: removing ours will reveal any machine-wide handler.
    if (!ours(previous)) {
        write(AceClassKey, L"PreviousHandler", previous.value_or(L""));
        write(AceClassKey, L"PreviousHandlerPresent", previous ? L"1" : L"0");
    }
    write(AceClassKey, nullptr, L"TSRE ACE Thumbnail Provider");
    const std::wstring inproc = std::wstring(AceClassKey) + L"\\InprocServer32";
    write(inproc.c_str(), nullptr, modulePath.data());
    write(inproc.c_str(), L"ThreadingModel", L"Apartment");
    // Publish the association last. No change to .ace's default application or ProgID.
    write(AceHandlerKey, nullptr, AceClassId);
}
void unregisterProvider() {
    if (ours(read(AceHandlerKey))) {
        const auto present = read(AceClassKey, L"PreviousHandlerPresent");
        if (present && *present == L"1") {
            const auto previous = read(AceClassKey, L"PreviousHandler");
            if (!previous) throw RegistryError{ERROR_INVALID_DATA};
            write(AceHandlerKey, nullptr, *previous);
        } else removeValue(AceHandlerKey);
    }
    // If another provider took over, its association remains untouched.
    const LSTATUS result = RegDeleteTreeW(HKEY_CURRENT_USER, AceClassKey);
    if (result != ERROR_FILE_NOT_FOUND && result != ERROR_PATH_NOT_FOUND) checked(result);
}
template<class F> HRESULT guarded(F function) {
    try {
        function();
        SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
        return S_OK;
    } catch (const RegistryError& e) { return HRESULT_FROM_WIN32(e.code); }
    catch (const std::bad_alloc&) { return E_OUTOFMEMORY; }
    catch (...) { return E_FAIL; }
}
}
extern "C" HRESULT WINAPI DllRegisterServer() { return guarded(registerProvider); }
extern "C" HRESULT WINAPI DllUnregisterServer() { return guarded(unregisterProvider); }
