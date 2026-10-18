// SPDX-License-Identifier: MIT
// Copyright (c) 2026 sh4
// Included after selection helpers. Queue the request after releasing capture,
// then obtain the real background/selection menu from Explorer's IShellView.
constexpr UINT_PTR MenuTestTimer = 0x44444904;

// Test telemetry observes the actual desktop thread's popup, including XAML menus.
void PollContextMenu() {
    if (!state.active || !state.contextTick || GetTickCount64() - state.contextTick > 5000) {
        return;
    }
    UINT keyTips = 0;
    EnumThreadWindows(
        GetCurrentThreadId(),
        [](HWND hwnd, LPARAM param) -> BOOL {
            if (!IsWindowVisible(hwnd)) {
                return TRUE;
            }
            auto cls = GetWindowClassName(hwnd);
            if (cls != L"Microsoft.UI.Content.PopupWindowSiteBridge" && cls != L"#32768") {
                return TRUE;
            }
            RECT rect{};
            GetWindowRect(hwnd, &rect);
            int dpi = static_cast<int>(std::max(96U, GetDpiForWindow(hwnd)));
            int width = MulDiv(rect.right - rect.left, 96, dpi);
            int height = MulDiv(rect.bottom - rect.top, 96, dpi);
            // WinUI keytip badges use separate, small popup windows, not UISF_HIDEACCEL.
            if (cls == L"Microsoft.UI.Content.PopupWindowSiteBridge" && width > 0 && width < 100 &&
                height > 0 && height < 80) {
                ++*reinterpret_cast<UINT*>(param);
            }
            if (rect.right - rect.left > 100 && rect.bottom - rect.top > 50) {
                state.contextPopup = hwnd;
                state.menuStyle = cls == L"#32768" ? L"classic" : L"modern";
                if (!state.menuSeen) {
                    state.menuSeen = true;
                    state.menuSeenTick = GetTickCount64();
                    state.contextMenus++;
                }
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&keyTips));
    state.contextKeyTips = std::max(state.contextKeyTips, keyTips);
}

void CaptureTestContextMenu(const std::wstring& path) {
    RECT bounds{};
    EnumThreadWindows(
        GetCurrentThreadId(),
        [](HWND window, LPARAM param) -> BOOL {
            auto cls = GetWindowClassName(window);
            if (IsWindowVisible(window) &&
                (cls == L"Microsoft.UI.Content.PopupWindowSiteBridge" || cls == L"#32768")) {
                RECT rect{};
                GetWindowRect(window, &rect);
                auto bounds = reinterpret_cast<RECT*>(param);
                UnionRect(bounds, bounds, &rect);
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&bounds));
    Require(!IsRectEmpty(&bounds), "Context menu capture is empty");
    Gdiplus::Bitmap bitmap(bounds.right - bounds.left, bounds.bottom - bounds.top,
                           PixelFormat32bppARGB);
    Gdiplus::Graphics graphics(&bitmap);
    HDC target = graphics.GetHDC();
    HDC screen = GetDC(nullptr);
    BOOL captured = BitBlt(target, 0, 0, bounds.right - bounds.left, bounds.bottom - bounds.top,
                           screen, bounds.left, bounds.top, SRCCOPY | CAPTUREBLT);
    ReleaseDC(nullptr, screen);
    graphics.ReleaseHDC(target);
    Require(captured != FALSE, "Context menu screen capture failed");
    auto png = PngEncoder();
    Require(bitmap.Save(path.c_str(), &png, nullptr) == Gdiplus::Ok, "Context menu capture failed");
}

void DismissTestMenu() {
    PollContextMenu();
    if (!state.menuSeen || state.menuDismissed) {
        return;
    }
    if (state.menuStyle == L"modern" && !state.menuCapturePath.empty() &&
        GetTickCount64() - state.menuSeenTick < 600) {
        return;
    }
    if (!state.menuCapturePath.empty()) {
        try {
            CaptureTestContextMenu(state.menuCapturePath);
        } catch (...) {
            state.menuCaptureFailed = true;
        }
        state.menuCapturePath.clear();
    }
    state.menuDismissed = true;
    EndMenu();
    HWND focus = GetFocus();
    if (focus && GetWindowThreadProcessId(focus, nullptr) == GetCurrentThreadId()) {
        PostMessageW(focus, WM_KEYDOWN, VK_ESCAPE, 1);
        PostMessageW(focus, WM_KEYUP, VK_ESCAPE, 0xc0000001);
    }
    if (state.contextPopup) {
        PostMessageW(state.contextPopup, WM_KEYDOWN, VK_ESCAPE, 1);
        PostMessageW(state.contextPopup, WM_KEYUP, VK_ESCAPE, 0xc0000001);
    }
}

UINT ContextMessage() {
    return RegisterWindowMessageW(L"DekaDesktopIcon.Context.v1");
}

void ObserveContextMenu(UINT message, WPARAM w, LPARAM) {
    if (!state.contextActive) {
        return;
    }
    if ((message == WM_ENTERMENULOOP || message == WM_INITMENUPOPUP) && !state.menuSeen) {
        state.menuSeen = true;
        state.menuSeenTick = GetTickCount64();
        state.contextMenus++;
        state.menuStyle = L"classic";
    }
    if (message == WM_INITMENUPOPUP && state.menuTitles.empty()) {
        HMENU menu = reinterpret_cast<HMENU>(w);
        for (int i = 0; i < GetMenuItemCount(menu); ++i) {
            wchar_t title[1024]{};
            GetMenuStringW(menu, i, title, 1024, MF_BYPOSITION);
            if (*title) {
                state.menuTitles.push_back(title);
            }
        }
    }
}

void QueueContext(POINT point) {
    if (!state.active || state.contextActive || state.contextPending) {
        return;
    }
    state.contextPoint = point;
    state.contextPending = true;
    state.contextRequests++;
    if (!PostMessageW(state.listView, ContextMessage(), 0, 0)) {
        state.contextPending = false;
    }
}

HRESULT InvokeShellMenuCommand(IContextMenu* context, UINT command, POINT point) {
    CMINVOKECOMMANDINFOEX invoke{sizeof(invoke)};
    invoke.fMask = CMIC_MASK_UNICODE | CMIC_MASK_PTINVOKE;
    invoke.hwnd = state.shellViewWindow;
    if (GetKeyState(VK_SHIFT) < 0) {
        invoke.fMask |= CMIC_MASK_SHIFT_DOWN;
    }
    if (GetKeyState(VK_CONTROL) < 0) {
        invoke.fMask |= CMIC_MASK_CONTROL_DOWN;
    }
    invoke.lpVerb = MAKEINTRESOURCEA(command - 1);
    invoke.lpVerbW = MAKEINTRESOURCEW(command - 1);
    invoke.nShow = SW_SHOWNORMAL;
    invoke.ptInvoke = point;
    return context->InvokeCommand(reinterpret_cast<CMINVOKECOMMANDINFO*>(&invoke));
}

void ShellContextMenu(POINT point) {
    bool keyboard = point.x == -1 && point.y == -1;
    int index = -1;
    if (keyboard) {
        index = ListView_GetNextItem(state.listView, -1, LVNI_FOCUSED);
        if (index < 0 || !IsItemSelected(index)) {
            index = ListView_GetNextItem(state.listView, -1, LVNI_SELECTED);
        }
        RECT rect{};
        point = index >= 0 && GetPaintedItemRect(index, LVIR_ICON, rect)
                    ? POINT{rect.left + state.iconSize / 2, rect.top + state.iconSize / 2}
                    : POINT{16, 16};
        ClientToScreen(state.listView, &point);
    } else {
        auto client = point;
        ScreenToClient(state.listView, &client);
        index = HitTestPaintedItem(client);
    }
    bool itemMenu = index >= 0 && ListView_GetSelectedCount(state.listView) > 0;
    state.contextItem = itemMenu;
    state.contextPoint = point;
    if (!keyboard) {
        // Explorer records the mouse invocation through its WM_CONTEXTMENU handler.
        // Calling IContextMenuSite directly skips that path and can show WinUI keytips.
        SendMessageW(state.shellViewWindow, WM_CONTEXTMENU,
                     reinterpret_cast<WPARAM>(state.listView), MAKELPARAM(point.x, point.y));
        return;
    }
    auto folder = GetDesktopFolderView();
    Require(folder != nullptr, "Context menu view unavailable");
    ComPtr<IShellView> view;
    Require(SUCCEEDED(folder.As(&view)), "Context menu shell view unavailable");
    ComPtr<IContextMenu> context;
    Require(SUCCEEDED(view->GetItemObject(itemMenu ? SVGIO_SELECTION : SVGIO_BACKGROUND,
                                          IID_PPV_ARGS(&context))),
            "Native context menu unavailable");
    ComPtr<IContextMenuSite> site;
    Require(SUCCEEDED(view.As(&site)), "Explorer's native context menu site is unavailable");
    UINT flags = itemMenu ? CMF_CANRENAME | CMF_ITEMMENU : CMF_NORMAL;
    if (GetKeyState(VK_SHIFT) < 0) {
        flags |= CMF_EXTENDEDVERBS;
    }
    Require(SUCCEEDED(site->DoContextMenuPopup(context.Get(), flags, point)),
            "Explorer context menu failed");
}

void ProcessContext() {
    if (!state.active || !state.contextPending || state.contextActive) {
        return;
    }
    state.contextPending = false;
    state.contextActive = true;
    state.menuSeen = false;
    state.menuSeenTick = 0;
    state.contextKeyTips = 0;
    state.menuCaptureFailed = false;
    state.menuTitles.clear();
    state.contextTick = GetTickCount64();
    state.contextPopup = nullptr;
    state.menuStyle.clear();
    state.menuDismissed = false;
    POINT point = state.contextPoint;
    CancelInteraction();
    SetForegroundWindow(GetAncestor(state.listView, GA_ROOT));
    SetFocus(state.listView);
    // Our mouse handler bypasses the native ListView path that resets keyboard cues.
    // Update the view subtree directly: the desktop root may suppress a change request
    // when its own state already matches, leaving stale flags in the menu owner.
    // Do this before asynchronous Win11 menus are created, preserving keyboard settings.
    BOOL alwaysShowCues = FALSE;
    SystemParametersInfoW(SPI_GETKEYBOARDCUES, 0, &alwaysShowCues, 0);
    bool showCues = (point.x == -1 && point.y == -1) || GetKeyState(VK_MENU) < 0 || alwaysShowCues;
    SendMessageW(state.shellViewWindow, WM_UPDATEUISTATE,
                 MAKEWPARAM(showCues ? UIS_CLEAR : UIS_SET, UISF_HIDEACCEL), 0);
    if (state.menuTest) {
        SetTimer(state.listView, MenuTestTimer, 30, nullptr);
    }
    // This is a queued dispatch: mouse capture is released and the button-up has finished.
    try {
        if (state.testHost) {
            SendMessageW(state.shellViewWindow, WM_CONTEXTMENU,
                         reinterpret_cast<WPARAM>(state.listView), MAKELPARAM(point.x, point.y));
        } else {
            ShellContextMenu(point);
        }
    } catch (const std::exception& ex) {
        wchar_t message[512]{};
        MultiByteToWideChar(CP_UTF8, 0, ex.what(), -1, message, 512);
        SetMessage(message);
    } catch (...) {
        SetMessage(L"コンテキストメニューを開けませんでした。");
    }
    if (state.menuTest) {
        KillTimer(state.listView, MenuTestTimer);
    }
    state.contextActive = false;
    QueueSynchronization();
}
