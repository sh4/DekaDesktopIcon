// SPDX-License-Identifier: MIT
// Copyright (c) 2026 sh4
#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commctrl.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <exdisp.h>
#include <servprov.h>
#include <wrl/client.h>
#include <string>
#include <vector>
#include <memory>
#include <algorithm>
#include <stdexcept>
#include <cstdint>
#pragma comment(                                                                                   \
    linker,                                                                                        \
    "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='amd64' publicKeyToken='6595b64144ccf1df' language='*'\"")

namespace deka {
using Microsoft::WRL::ComPtr;

inline std::wstring GetWindowClassName(HWND hwnd) {
    wchar_t b[256]{};
    GetClassNameW(hwnd, b, 256);
    return b;
}

inline std::wstring ModulePath(HMODULE module = nullptr) {
    wchar_t p[32768]{};
    GetModuleFileNameW(module, p, 32768);
    return p;
}

inline std::wstring ParentPath(const std::wstring& p) {
    auto i = p.find_last_of(L"\\/");
    return i == p.npos ? L"." : p.substr(0, i);
}

inline void Require(bool condition, const char* text) {
    if (!condition) {
        throw std::runtime_error(text);
    }
}

inline std::wstring DefaultProfile() {
    PWSTR path = nullptr;
    HRESULT hr =
        SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_NO_PACKAGE_REDIRECTION, nullptr, &path);
    Require(SUCCEEDED(hr), "LocalAppData not available");
    std::wstring folder = std::wstring(path) + L"\\DekaDesktopIcon";
    CoTaskMemFree(path);
    int created = SHCreateDirectoryExW(nullptr, folder.c_str(), nullptr);
    Require(created == ERROR_SUCCESS || created == ERROR_ALREADY_EXISTS ||
                created == ERROR_FILE_EXISTS,
            "Cannot create settings directory");
    auto profile = folder + L"\\profile.ini";
    // Import existing image assignments once; never overwrite DekaDesktopIcon settings.
    if (GetFileAttributesW(profile.c_str()) == INVALID_FILE_ATTRIBUTES) {
        auto legacyProfile = ParentPath(folder) + L"\\ExplorerIconPatch\\profile.ini";
        if (GetFileAttributesW(legacyProfile.c_str()) != INVALID_FILE_ATTRIBUTES) {
            Require(CopyFileW(legacyProfile.c_str(), profile.c_str(), TRUE) != FALSE ||
                        GetLastError() == ERROR_FILE_EXISTS,
                    "Cannot import previous image settings");
        }
    }
    return profile;
}

inline std::wstring IniString(const std::wstring& path, const std::wstring& section,
                              const wchar_t* name) {
    wchar_t text[32768]{};
    GetPrivateProfileStringW(section.c_str(), name, L"", text, 32768, path.c_str());
    return text;
}

inline void WriteUtf16(const std::wstring& path, const std::wstring& content) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        throw std::runtime_error("Cannot create UTF-16 file (Win32 " +
                                 std::to_string(GetLastError()) + ")");
    }
    DWORD written = 0;
    WORD bom = 0xFEFF;
    BOOL ok = WriteFile(file, &bom, sizeof(bom), &written, nullptr);
    DWORD length = static_cast<DWORD>(content.size() * sizeof(wchar_t));
    ok = ok && WriteFile(file, content.data(), length, &written, nullptr) && written == length;
    FlushFileBuffers(file);
    CloseHandle(file);
    Require(ok != FALSE, "Cannot save UTF-16 file");
}

} // namespace deka
