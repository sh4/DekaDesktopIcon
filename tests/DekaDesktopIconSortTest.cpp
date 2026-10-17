// SPDX-License-Identifier: MIT
// Copyright (c) 2026 sh4
// Developer regression helper: use Explorer's real background context-menu verbs.
#include "ShellDesktop.h"
#include <shellapi.h>
#include <sstream>
using namespace deka;

struct MenuEntry {
    UINT id;
    std::wstring title, verb, parent;
};

void Collect(IContextMenu* context, HMENU menu, std::vector<MenuEntry>& entries,
             const std::wstring& parent = L"") {
    for (int i = 0; i < GetMenuItemCount(menu); ++i) {
        wchar_t title[512]{};
        MENUITEMINFOW item{sizeof(item)};
        item.fMask = MIIM_SUBMENU | MIIM_ID | MIIM_STRING;
        item.dwTypeData = title;
        item.cch = 512;
        if (!GetMenuItemInfoW(menu, i, TRUE, &item)) {
            continue;
        }
        if (item.hSubMenu) {
            Collect(context, item.hSubMenu, entries, title);
            continue;
        }
        wchar_t verb[512]{};
        if (item.wID >= 1 && item.wID < 0x7fff) {
            context->GetCommandString(item.wID - 1, GCS_VERBW, nullptr,
                                      reinterpret_cast<LPSTR>(verb), 512);
        }
        entries.push_back(MenuEntry{item.wID, title, verb, parent});
    }
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    int argc = 0;
    auto args = CommandLineToArgvW(GetCommandLineW(), &argc);
    int result = 1;
    HMENU menu = nullptr;
    try {
        Require(argc == 3 || argc == 4,
                "Usage: --list report.ini | --invoke-sort-id menu-id report.ini");
        auto folder = GetDesktopFolderView();
        Require(folder != nullptr, "Desktop view unavailable");
        if (argc == 3 &&
            (!wcscmp(args[1], L"--save-sort") || !wcscmp(args[1], L"--restore-sort"))) {
            if (!wcscmp(args[1], L"--save-sort")) {
                int count = 0;
                Require(SUCCEEDED(folder->GetSortColumnCount(&count)) && count >= 0 && count <= 16,
                        "Sort column count unavailable");
                std::vector<SORTCOLUMN> columns(count);
                if (count) {
                    Require(SUCCEEDED(folder->GetSortColumns(columns.data(), count)),
                            "Sort columns unavailable");
                }
                DWORD flags = 0;
                Require(SUCCEEDED(folder->GetCurrentFolderFlags(&flags)),
                        "Folder flags unavailable");
                std::wstring output = L"[Sort]\r\nCount=" + std::to_wstring(count) + L"\r\nFlags=" +
                                      std::to_wstring(flags) + L"\r\n";
                for (int i = 0; i < count; ++i) {
                    wchar_t guid[64]{};
                    StringFromGUID2(columns[i].propkey.fmtid, guid, 64);
                    output += L"[Column" + std::to_wstring(i) + L"]\r\nGUID=" + guid + L"\r\nPID=" +
                              std::to_wstring(columns[i].propkey.pid) + L"\r\nDirection=" +
                              std::to_wstring(columns[i].direction) + L"\r\n";
                }
                WriteUtf16(args[2], output);
            } else {
                UINT count = GetPrivateProfileIntW(L"Sort", L"Count", 17, args[2]);
                Require(count <= 16, "Invalid saved sort columns");
                std::vector<SORTCOLUMN> columns(count);
                for (UINT i = 0; i < count; ++i) {
                    auto section = L"Column" + std::to_wstring(i);
                    auto guid = IniString(args[2], section, L"GUID");
                    Require(SUCCEEDED(CLSIDFromString(guid.c_str(), &columns[i].propkey.fmtid)),
                            "Invalid property GUID");
                    columns[i].propkey.pid =
                        GetPrivateProfileIntW(section.c_str(), L"PID", 0, args[2]);
                    columns[i].direction = static_cast<SORTDIRECTION>(
                        GetPrivateProfileIntW(section.c_str(), L"Direction", 1, args[2]));
                }
                if (count) {
                    Require(SUCCEEDED(folder->SetSortColumns(columns.data(), count)),
                            "Sort restore failed");
                }
                // A manually positioned desktop can have no explicit sort columns.
                // SetSortColumns(nullptr, 0) is rejected by Explorer; in that case
                // restore flags here and let the patch restore the saved positions.
                DWORD flags = GetPrivateProfileIntW(L"Sort", L"Flags", 0, args[2]);
                DWORD mask = FWF_AUTOARRANGE | FWF_SNAPTOGRID;
                Require(SUCCEEDED(folder->SetCurrentFolderFlags(mask, flags & mask)),
                        "Folder flags restore failed");
            }
            folder.Reset();
            LocalFree(args);
            CoUninitialize();
            return 0;
        }
        ComPtr<IShellView> view;
        Require(SUCCEEDED(folder.As(&view)), "Shell view unavailable");
        ComPtr<IContextMenu> context;
        Require(SUCCEEDED(view->GetItemObject(SVGIO_BACKGROUND, IID_PPV_ARGS(&context))),
                "Background context menu unavailable");
        menu = CreatePopupMenu();
        Require(menu != nullptr, "Menu allocation failed");
        Require(SUCCEEDED(context->QueryContextMenu(menu, 0, 1, 0x7ffe,
                                                    CMF_NORMAL | CMF_SYNCCASCADEMENU)),
                "QueryContextMenu failed");
        std::vector<MenuEntry> entries;
        Collect(context.Get(), menu, entries);
        std::wstring output = L"[Menu]\r\nCount=" + std::to_wstring(entries.size()) + L"\r\n";
        for (size_t i = 0; i < entries.size(); ++i) {
            output += L"[Entry" + std::to_wstring(i) + L"]\r\nId=" +
                      std::to_wstring(entries[i].id) + L"\r\nTitle=" + entries[i].title +
                      L"\r\nVerb=" + entries[i].verb + L"\r\nParent=" + entries[i].parent + L"\r\n";
        }
        if (argc == 4 && !wcscmp(args[1], L"--invoke-sort-id")) {
            // This test helper recognizes Japanese/English Sort by submenus only.
            UINT id = static_cast<UINT>(std::stoul(args[2]));
            auto found = std::find_if(entries.begin(), entries.end(), [&](const MenuEntry& entry) {
                return entry.id == id;
            });
            Require(found != entries.end() &&
                        (found->parent.find(L"並べ替え") != std::wstring::npos ||
                         found->parent.find(L"Sort by") != std::wstring::npos),
                    "ID is not in Explorer's Sort by submenu");
            CMINVOKECOMMANDINFOEX invoke{sizeof(invoke)};
            invoke.fMask = CMIC_MASK_UNICODE;
            invoke.hwnd = FindDesktop().parent;
            invoke.lpVerb = MAKEINTRESOURCEA(found->id - 1);
            invoke.lpVerbW = MAKEINTRESOURCEW(found->id - 1);
            invoke.nShow = SW_SHOWNORMAL;
            HRESULT hr = context->InvokeCommand(reinterpret_cast<CMINVOKECOMMANDINFO*>(&invoke));
            output += L"[Invocation]\r\nTitle=" + found->title + L"\r\nHRESULT=" +
                      std::to_wstring(hr) + L"\r\n";
            Require(SUCCEEDED(hr), "Explorer sort invocation failed");
        } else {
            Require(argc == 3 && !wcscmp(args[1], L"--list"), "Invalid arguments");
        }
        WriteUtf16(args[argc - 1], output);
        result = 0;
    } catch (const std::exception& ex) {
        if (argc >= 3) {
            wchar_t error[512]{};
            MultiByteToWideChar(CP_UTF8, 0, ex.what(), -1, error, 512);
            try {
                WriteUtf16(std::wstring(args[argc - 1]) + L".error.ini",
                           std::wstring(L"[Error]\r\nMessage=") + error + L"\r\n");
            } catch (...) {
            }
        }
    }
    if (menu) {
        DestroyMenu(menu);
    }
    LocalFree(args);
    CoUninitialize();
    return result;
}
