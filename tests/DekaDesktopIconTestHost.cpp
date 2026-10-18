// SPDX-License-Identifier: MIT
// Copyright (c) 2026 sh4
#include "ImagingChecks.h"
#include <shellapi.h>
#include <windowsx.h>
using namespace deka;

namespace {
HWND list = nullptr;
HIMAGELIST images = nullptr;

UINT ExerciseMessage() {
    return RegisterWindowMessageW(L"DekaDesktopIcon.TestHost.Exercise.v1");
}

int CALLBACK Compare(LPARAM left, LPARAM right, LPARAM direction) {
    return (left < right ? -1 : left > right ? 1 : 0) * static_cast<int>(direction);
}

LRESULT CALLBACK Proc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_CONTEXTMENU) {
        POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        if (point.x == -1 && point.y == -1) {
            RECT rect{};
            ListView_GetItemRect(list, 0, &rect, LVIR_ICON);
            point = POINT{rect.left + 20, rect.top + 20};
            ClientToScreen(list, &point);
        }
        HMENU menu = CreatePopupMenu();
        AppendMenuW(menu, MF_STRING, 9001,
                    ListView_GetSelectedCount(list) ? L"Test item menu" : L"Test background menu");
        TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, point.x, point.y, hwnd, nullptr);
        DestroyMenu(menu);
        return 0;
    }
    if (message == WM_COPYDATA && lParam) {
        auto data = reinterpret_cast<COPYDATASTRUCT*>(lParam);
        if (data->dwData != Magic || !data->lpData || data->cbData < 2 || data->cbData > 65536 ||
            data->cbData % 2) {
            return FALSE;
        }
        auto path = static_cast<const wchar_t*>(data->lpData);
        if (path[data->cbData / 2 - 1]) {
            return FALSE;
        }
        try {
            DWORD spacing = static_cast<DWORD>(ListView_GetItemSpacing(list, FALSE));
            std::wstring json =
                L"{\"spacingX\":" + std::to_wstring(LOWORD(spacing)) + L",\"spacingY\":" +
                std::to_wstring(HIWORD(spacing)) + L",\"autoArrange\":" +
                std::to_wstring((GetWindowLongPtrW(list, GWL_STYLE) & LVS_AUTOARRANGE) != 0) +
                L",\"snapToGrid\":" +
                std::to_wstring((ListView_GetExtendedListViewStyle(list) & LVS_EX_SNAPTOGRID) !=
                                0) +
                L",\"items\":[";
            for (int i = 0; i < ListView_GetItemCount(list); ++i) {
                LVITEMW item{};
                item.mask = LVIF_PARAM;
                item.iItem = i;
                ListView_GetItem(list, &item);
                POINT point{};
                ListView_GetItemPosition(list, i, &point);
                if (i) {
                    json += L",";
                }
                json += L"{\"identity\":" + std::to_wstring(item.lParam) + L",\"x\":" +
                        std::to_wstring(point.x) + L",\"y\":" + std::to_wstring(point.y) + L"}";
            }
            WriteUtf16(path, json + L"]}");
            return TRUE;
        } catch (...) {
            return FALSE;
        }
    }
    if (message == WM_CREATE) {
        list = CreateWindowExW(
            0, WC_LISTVIEWW, L"DekaDesktopIcon test items",
            WS_CHILD | WS_VISIBLE | LVS_ICON | LVS_EDITLABELS | LVS_SHAREIMAGELISTS, 0, 0, 1600,
            1100, hwnd, reinterpret_cast<HMENU>(1), GetModuleHandleW(nullptr), nullptr);
        images = ImageList_Create(64, 64, ILC_COLOR32 | ILC_MASK, 1, 1);
        ImageList_AddIcon(images, LoadIconW(nullptr, IDI_APPLICATION));
        ListView_SetImageList(list, images, LVSIL_NORMAL);
        ListView_SetIconSpacing(list, 100, 110);
        for (int i = 0; i < 2; ++i) {
            LVITEMW item{};
            item.mask = LVIF_TEXT | LVIF_IMAGE | LVIF_PARAM;
            item.iItem = i;
            item.iImage = 0;
            item.lParam = 100 + i;
            item.pszText =
                const_cast<PWSTR>(i ? L"System32 (native item 101)" : L"Windows (native item 100)");
            ListView_InsertItem(list, &item);
            ListView_SetItemPosition32(list, i, 30 + i * 160, 40);
        }
        SetTimer(hwnd, 10, 120000, nullptr);
        return 0;
    }
    if (message == WM_SIZE && list) {
        MoveWindow(list, 0, 0, LOWORD(lParam), HIWORD(lParam), TRUE);
        return 0;
    }
    if (message == ExerciseMessage()) {
        SendMessageW(list, WM_SETREDRAW, FALSE, 0);
        bool ok = true;
        int attachedWidth = 0, attachedHeight = 0;
        ImageList_GetIconSize(ListView_GetImageList(list, LVSIL_NORMAL), &attachedWidth,
                              &attachedHeight);
        if (wParam != 2 && wParam != 4) {
            // Exercise the image-list ownership contract, including a temporary detach.
            if (wParam == 3) {
                auto previous = ListView_SetImageList(list, nullptr, LVSIL_NORMAL);
                ok = previous == images;
                if (previous == images) {
                    ImageList_Destroy(previous);
                }
                images = nullptr;
            }
            auto replacement = ImageList_Create(64, 64, ILC_COLOR32 | ILC_MASK, 1, 1);
            ImageList_AddIcon(replacement, LoadIconW(nullptr, IDI_APPLICATION));
            auto previous = ListView_SetImageList(list, replacement, LVSIL_NORMAL);
            ok = ok && previous == images;
            // A real owner may immediately free the returned list. It must not be the patch list.
            if (previous == images && previous) {
                ImageList_Destroy(previous);
            }
            images = replacement;
        }
        if (wParam == 4) {
            ok = ImageList_SetIconSize(ListView_GetImageList(list, LVSIL_NORMAL), 64, 64) != FALSE;
        }
        SetWindowLongPtrW(list, GWL_STYLE,
                          (GetWindowLongPtrW(list, GWL_STYLE) | LVS_AUTOARRANGE) &
                              ~LVS_SHAREIMAGELISTS);
        ListView_SetExtendedListViewStyle(list, LVS_EX_SNAPTOGRID | LVS_EX_AUTOAUTOARRANGE);
        ListView_SetIconSpacing(list, 100, 110);
        LVITEMW first{};
        first.mask = LVIF_PARAM;
        first.iItem = 0;
        ListView_GetItem(list, &first);
        ok = ListView_SortItems(list, Compare, first.lParam == 100 ? -1 : 1) != FALSE && ok;
        ListView_Arrange(list, LVA_DEFAULT);
        SendMessageW(list, WM_SETREDRAW, TRUE, 0);
        InvalidateRect(list, nullptr, TRUE);
        if (wParam == 4) {
            RedrawWindow(list, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW);
            int width = 0, height = 0;
            ImageList_GetIconSize(ListView_GetImageList(list, LVSIL_NORMAL), &width, &height);
            ok = ok && width == attachedWidth && height == attachedHeight;
        }
        return ok ? 1 : 0;
    }
    if (message == WM_NOTIFY) {
        auto h = reinterpret_cast<NMHDR*>(lParam);
        if (h->code == NM_DBLCLK) {
            SetWindowTextW(hwnd, L"DekaDesktopIcon TestHost — native double click received");
        }
        if (h->code == LVN_ENDLABELEDITW) {
            return TRUE;
        }
    }
    if (message == WM_TIMER && wParam == 10) {
        DestroyWindow(hwnd);
        return 0;
    }
    if (message == WM_DESTROY) {
        if (images) {
            ImageList_Destroy(images);
        }
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}
} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    int argc = 0;
    auto args = CommandLineToArgvW(GetCommandLineW(), &argc);
    bool imaging = argc == 5 && !wcscmp(args[1], L"--check-imaging");
    HRESULT com =
        CoInitializeEx(nullptr, imaging ? COINIT_MULTITHREADED : COINIT_APARTMENTTHREADED);
    if (imaging) {
        ULONG_PTR gdiplus = 0;
        Gdiplus::GdiplusStartupInput startup;
        int result = 1;
        try {
            Require(SUCCEEDED(com), "Test COM initialization failed");
            Require(Gdiplus::GdiplusStartup(&gdiplus, &startup, nullptr) == Gdiplus::Ok,
                    "Test GDI+ initialization failed");
            CheckImaging(args[2], args[3]);
            WriteUtf16(args[4], L"{\"passed\":true,\"thumbnail\":true,\"iconFallback\":true,"
                                L"\"override\":true,\"aspectRatio\":true,\"highResolution\":true}");
            result = 0;
        } catch (const std::exception& ex) {
            const char* message = ex.what();
            WriteUtf16(args[4], L"{\"passed\":false,\"message\":\"" +
                                    std::wstring(message, message + strlen(message)) + L"\"}");
        }
        if (gdiplus) {
            Gdiplus::GdiplusShutdown(gdiplus);
        }
        if (SUCCEEDED(com)) {
            CoUninitialize();
        }
        LocalFree(args);
        return result;
    }
    if (argc == 3 && !wcscmp(args[1], L"--snapshot")) {
        auto desktop = FindDesktop(true);
        DWORD_PTR result = 0;
        COPYDATASTRUCT data{Magic, static_cast<DWORD>((wcslen(args[2]) + 1) * sizeof(wchar_t)),
                            args[2]};
        bool ok =
            desktop.parent &&
            SendMessageTimeoutW(desktop.parent, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&data),
                                SMTO_ABORTIFHUNG, 5000, &result) &&
            result == TRUE;
        LocalFree(args);
        CoUninitialize();
        return ok ? 0 : 1;
    }
    if (argc == 2) {
        WPARAM command = !wcscmp(args[1], L"--exercise-refresh")        ? 1
                         : !wcscmp(args[1], L"--exercise-layout")       ? 2
                         : !wcscmp(args[1], L"--exercise-null-refresh") ? 3
                         : !wcscmp(args[1], L"--exercise-resize")       ? 4
                                                                        : 0;
        auto desktop = FindDesktop(true);
        DWORD_PTR result = 0;
        bool ok = command && desktop.parent &&
                  SendMessageTimeoutW(desktop.parent, ExerciseMessage(), command, 0,
                                      SMTO_ABORTIFHUNG, 5000, &result) &&
                  result == 1;
        LocalFree(args);
        CoUninitialize();
        return ok ? 0 : 1;
    }
    LocalFree(args);
    INITCOMMONCONTROLSEX cc{sizeof(cc), ICC_LISTVIEW_CLASSES};
    InitCommonControlsEx(&cc);
    WNDCLASSW wc{};
    wc.lpfnWndProc = Proc;
    wc.hInstance = instance;
    wc.lpszClassName = L"DekaDesktopIcon.TestHost";
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassW(&wc);
    HWND window =
        CreateWindowExW(0, wc.lpszClassName, L"DekaDesktopIcon TestHost", WS_OVERLAPPEDWINDOW, 30,
                        30, 1500, 1120, nullptr, nullptr, instance, nullptr);
    ShowWindow(window, SW_SHOW);
    UpdateWindow(window);
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    CoUninitialize();
    return 0;
}
