#pragma once
#include <windows.h>
// New identity, independent of the old thumbnail handler.
inline constexpr CLSID CLSID_AceThumbnails = {0x63ce2d66, 0x42b2, 0x42eb, {0x8c, 0xe5, 0x6f, 0x7b, 0x4c, 0xc9, 0xc6, 0x32}};
inline constexpr wchar_t AceClassId[] = L"{63CE2D66-42B2-42EB-8CE5-6F7B4CC9C632}";
inline constexpr wchar_t AceClassKey[] = L"Software\\Classes\\CLSID\\{63CE2D66-42B2-42EB-8CE5-6F7B4CC9C632}";
inline constexpr wchar_t AceHandlerKey[] = L"Software\\Classes\\.ace\\shellex\\{E357FCCD-A995-4576-B01F-234630154E96}";
extern HMODULE aceModule;
