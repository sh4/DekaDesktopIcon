// SPDX-License-Identifier: MIT
// Copyright (c) 2026 sh4
#pragma once
#include "Protocol.h"

namespace deka {
struct DesktopTarget {
    HWND parent = nullptr, list = nullptr;
    DWORD pid = 0, thread = 0;
    bool test = false;
};

inline DesktopTarget FindDesktop(bool test = false) {
    DesktopTarget result{};
    result.test = test;
    EnumWindows(
        [](HWND top, LPARAM data) -> BOOL {
            auto& r = *reinterpret_cast<DesktopTarget*>(data);
            HWND parent = r.test && GetWindowClassName(top) == L"DekaDesktopIcon.TestHost"
                              ? top
                              : (!r.test ? FindWindowExW(top, nullptr, L"SHELLDLL_DefView", nullptr)
                                         : nullptr);
            if (parent) {
                HWND list = FindWindowExW(parent, nullptr, WC_LISTVIEWW, nullptr);
                if (list) {
                    r.parent = parent;
                    r.list = list;
                    r.thread = GetWindowThreadProcessId(list, &r.pid);
                    return FALSE;
                }
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&result));
    return result;
}

inline ComPtr<IFolderView2> GetDesktopFolderView() {
    ComPtr<IShellWindows> windows;
    ComPtr<IDispatch> dispatch;
    if (FAILED(CoCreateInstance(CLSID_ShellWindows, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&windows)))) {
        return {};
    }
    VARIANT loc{}, empty{};
    loc.vt = VT_I4;
    loc.lVal = CSIDL_DESKTOP;
    long hwnd = 0;
    if (FAILED(windows->FindWindowSW(&loc, &empty, SWC_DESKTOP, &hwnd, SWFO_NEEDDISPATCH,
                                     &dispatch))) {
        return {};
    }
    ComPtr<IServiceProvider> services;
    if (FAILED(dispatch.As(&services))) {
        return {};
    }
    ComPtr<IShellBrowser> browser;
    if (FAILED(services->QueryService(SID_STopLevelBrowser, IID_PPV_ARGS(&browser)))) {
        return {};
    }
    ComPtr<IShellView> view;
    if (FAILED(browser->QueryActiveShellView(&view))) {
        return {};
    }
    ComPtr<IFolderView2> folder;
    view.As(&folder);
    return folder;
}

struct Item {
    std::wstring key, name, overrideImage;
    POINT position{};
    bool link = false;
};

inline std::wstring DisplayName(IShellItem* item, SIGDN kind) {
    PWSTR text = nullptr;
    if (FAILED(item->GetDisplayName(kind, &text))) {
        return {};
    }
    std::wstring result(text);
    CoTaskMemFree(text);
    return result;
}

inline std::vector<Item> ReadDesktopItems(HWND list, bool test) {
    int count = ListView_GetItemCount(list);
    Require(count >= 0 && count <= MaxItems, "Desktop item count exceeds 512");
    std::vector<Item> result;
    ComPtr<IFolderView2> view;
    ComPtr<IShellFolder> folder;
    if (!test) {
        view = GetDesktopFolderView();
        Require(view != nullptr, "Desktop IFolderView2 unavailable");
        Require(SUCCEEDED(view->GetFolder(IID_PPV_ARGS(&folder))),
                "Desktop IShellFolder unavailable");
    }
    for (int i = 0; i < count; ++i) {
        Item item;
        if (test) {
            wchar_t text[2048]{};
            ListView_GetItemText(list, i, 0, text, 2048);
            item.name = text;
            LVITEMW native{};
            native.mask = LVIF_PARAM;
            native.iItem = i;
            Require(ListView_GetItem(list, &native) != FALSE, "Test item identity unavailable");
            item.key = native.lParam == 100 ? L"C:\\Windows" : L"C:\\Windows\\System32";
        } else {
            PITEMID_CHILD pidl = nullptr;
            Require(SUCCEEDED(view->Item(i, &pidl)), "Desktop PIDL unavailable");
            ComPtr<IShellItem> shellItem;
            HRESULT hr =
                SHCreateItemWithParent(nullptr, folder.Get(), pidl, IID_PPV_ARGS(&shellItem));
            view->GetItemPosition(pidl, &item.position);
            CoTaskMemFree(pidl);
            Require(SUCCEEDED(hr), "Desktop shell item unavailable");
            item.key = DisplayName(shellItem.Get(), SIGDN_DESKTOPABSOLUTEPARSING);
            item.name = DisplayName(shellItem.Get(), SIGDN_NORMALDISPLAY);
            SFGAOF attributes = 0;
            shellItem->GetAttributes(SFGAO_LINK, &attributes);
            item.link = (attributes & SFGAO_LINK) != 0;
        }
        Require(!item.key.empty(), "Desktop parsing name is empty");
        if (test) {
            ListView_GetItemPosition(list, i, &item.position);
        }
        result.push_back(std::move(item));
    }
    return result;
}

inline void WriteRollback(const std::wstring& profile, const std::vector<Item>& items,
                          LONG_PTR style, DWORD spacing, int width, int height) {
    std::wstring text = L"[Original]\r\nStyle=" + std::to_wstring(style) + L"\r\nSpacing=" +
                        std::to_wstring(spacing) + L"\r\nImageWidth=" + std::to_wstring(width) +
                        L"\r\nImageHeight=" + std::to_wstring(height) + L"\r\nCount=" +
                        std::to_wstring(items.size()) + L"\r\n";
    for (size_t i = 0; i < items.size(); ++i) {
        text += L"[Item" + std::to_wstring(i) + L"]\r\nKey=" + items[i].key + L"\r\nX=" +
                std::to_wstring(items[i].position.x) + L"\r\nY=" +
                std::to_wstring(items[i].position.y) + L"\r\n";
    }
    WriteUtf16(profile + L".rollback.ini", text);
}
} // namespace deka
