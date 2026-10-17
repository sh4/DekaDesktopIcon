// SPDX-License-Identifier: MIT
// Copyright (c) 2026 sh4
// Included by the core inside its implementation namespace.
UINT FindAlignmentMenu(HMENU menu, bool automatic, bool& checked) {
    for (int i = 0; i < GetMenuItemCount(menu); ++i) {
        wchar_t title[512]{};
        MENUITEMINFOW item{sizeof(item)};
        item.fMask = MIIM_SUBMENU | MIIM_ID | MIIM_STRING | MIIM_STATE;
        item.dwTypeData = title;
        item.cch = 512;
        if (!GetMenuItemInfoW(menu, i, TRUE, &item)) {
            continue;
        }
        if (item.hSubMenu) {
            UINT found = FindAlignmentMenu(item.hSubMenu, automatic, checked);
            if (found) {
                return found;
            }
        }
        std::wstring text = title;
        bool match = automatic ? text.find(L"アイコンの自動整列") != text.npos ||
                                     text.find(L"Auto arrange icons") != text.npos
                               : text.find(L"アイコンを等間隔に整列") != text.npos ||
                                     text.find(L"Align icons to grid") != text.npos;
        if (match) {
            checked = (item.fState & MFS_CHECKED) != 0;
            return item.wID;
        }
    }
    return 0;
}

bool ToggleAlignmentMenu(bool automatic) {
    auto folder = GetDesktopFolderView();
    Require(folder != nullptr, "Alignment menu view unavailable");
    ComPtr<IShellView> view;
    Require(SUCCEEDED(folder.As(&view)), "Alignment menu shell view unavailable");
    ComPtr<IContextMenu> context;
    Require(SUCCEEDED(view->GetItemObject(SVGIO_BACKGROUND, IID_PPV_ARGS(&context))),
            "Alignment menu unavailable");
    HMENU menu = CreatePopupMenu();
    Require(menu != nullptr, "Alignment menu allocation failed");
    try {
        Require(SUCCEEDED(context->QueryContextMenu(menu, 0, 1, 0x7ffe,
                                                    CMF_NORMAL | CMF_SYNCCASCADEMENU)),
                "Alignment menu population failed");
        bool checked = false;
        UINT command = FindAlignmentMenu(menu, automatic, checked);
        Require(command != 0, "Alignment command not found");
        Require(SUCCEEDED(InvokeShellMenuCommand(context.Get(), command, POINT{0, 0})),
                "Alignment command invocation failed");
        DestroyMenu(menu);
        QueueSynchronization();
        SynchronizeDesktop();
        return !checked;
    } catch (...) {
        DestroyMenu(menu);
        throw;
    }
}

void SetTestAlignment(bool automatic, bool snap) {
    // Called only by developer regression commands, outside synchronous input hooks.
    if (!state.testHost) {
        auto view = GetDesktopFolderView();
        Require(view != nullptr, "Alignment view unavailable");
        DWORD mask = FWF_AUTOARRANGE | FWF_SNAPTOGRID,
              flags = (automatic ? FWF_AUTOARRANGE : 0) | (snap ? FWF_SNAPTOGRID : 0);
        Require(SUCCEEDED(view->SetCurrentFolderFlags(mask, flags)),
                "Shell alignment update failed");
    }
    auto style = GetWindowLongPtrW(state.listView, GWL_STYLE);
    SetWindowLongPtrW(state.listView, GWL_STYLE,
                      automatic ? style | LVS_AUTOARRANGE : style & ~LVS_AUTOARRANGE);
    ListView_SetExtendedListViewStyleEx(state.listView, LVS_EX_SNAPTOGRID,
                                        snap ? LVS_EX_SNAPTOGRID : 0);
    SynchronizeDesktop();
}

void Capture(const std::wstring& path);

void ValidateInteraction(const std::wstring& path) {
    Require(state.active && !path.empty() && ListView_GetItemCount(state.listView) >= 2,
            "Interaction validation needs two items and an active patch");
    auto fileName = path.substr(path.find_last_of(L"\\/") + 1);
    auto dot = fileName.find_last_of(L'.');
    if (dot != std::wstring::npos) {
        fileName.resize(dot);
    }
    auto capturePrefix = ParentPath(path) + L"\\" + fileName + L"-";
    auto original = ReadDesktopItems(state.listView, state.testHost);
    auto savedOrigin = ViewOrigin();
    int savedAnchor = state.selectionAnchor;
    bool savedAutomatic = IsAutoArrangeEnabled(), savedSnap = IsSnapToGridEnabled();
    int savedFocus = ListView_GetNextItem(state.listView, -1, LVNI_FOCUSED);
    std::vector<bool> savedSelection;
    for (int i = 0; i < static_cast<int>(original.size()); ++i) {
        savedSelection.push_back(IsItemSelected(i));
    }
    auto cleanup = [&] {
        CancelInteraction();
        state.pendingMoves = original;
        state.committing = true;
        for (size_t i = 0; i < original.size(); ++i) {
            ListView_SetItemPosition32(state.listView, static_cast<int>(i), original[i].position.x,
                                       original[i].position.y);
        }
        state.committing = false;
        CommitMoves();
        for (int i = 0; i < static_cast<int>(original.size()); ++i) {
            SetItemSelected(i, savedSelection[i]);
        }
        ListView_SetItemState(state.listView, -1, 0, LVIS_FOCUSED);
        if (savedFocus >= 0) {
            FocusItem(savedFocus);
        }
        state.selectionAnchor = savedAnchor;
        ListView_SetSelectionMark(state.listView, savedAnchor);
        auto origin = ViewOrigin();
        ListView_Scroll(state.listView, savedOrigin.x - origin.x, savedOrigin.y - origin.y);
        state.layoutPending = false;
        InvalidateRect(state.listView, nullptr, FALSE);
        SetTestAlignment(savedAutomatic, savedSnap);
    };
    auto mouse = [&](UINT message, WPARAM modifiers, POINT point) {
        SendMessageW(state.listView, message, modifiers, MAKELPARAM(point.x, point.y));
    };
    auto point = [&](int index) {
        RECT rect{};
        Require(GetPaintedItemRect(index, LVIR_ICON, rect), "Validation rectangle unavailable");
        return POINT{rect.left + state.iconSize / 2, rect.top + state.iconSize - 32};
    };
    auto click = [&](int index, WPARAM modifiers) {
        auto p = point(index);
        mouse(WM_LBUTTONDOWN, modifiers | MK_LBUTTON, p);
        mouse(WM_LBUTTONUP, modifiers, p);
    };
    try {
        SetTestAlignment(false, false);
        auto origin = ViewOrigin();
        ListView_Scroll(state.listView, -origin.x, -origin.y);
        ClearSelection();
        click(0, 0);
        Require(IsItemSelected(0) && !IsItemSelected(1) &&
                    ListView_GetSelectedCount(state.listView) == 1,
                "Lower icon click did not select exactly one item");
        click(1, MK_CONTROL);
        Require(IsItemSelected(0) && IsItemSelected(1), "Control click did not add selection");
        click(1, MK_CONTROL);
        Require(IsItemSelected(0) && !IsItemSelected(1),
                "Control click did not toggle selection off");
        click(0, 0);
        click(1, MK_SHIFT);
        Require(IsItemSelected(0) && IsItemSelected(1), "Shift click did not select the range");
        // Empty-space rectangle crosses the actual lower half of the image.
        RECT first{};
        GetPaintedItemRect(0, LVIR_BOUNDS, first);
        POINT a{first.right + 10, first.bottom + 10},
            b{first.left + state.iconSize / 2, first.top + state.iconSize / 2};
        Require(HitTestPaintedItem(a) < 0, "Validation rectangle must start in empty space");
        mouse(WM_LBUTTONDOWN, MK_LBUTTON, a);
        mouse(WM_MOUSEMOVE, MK_LBUTTON, b);
        RECT marquee = MarqueeRect();
        Require(state.marquee && marquee.left == b.x && marquee.top == b.y &&
                    marquee.right == a.x && marquee.bottom == a.y,
                "Reverse marquee coordinates changed");
        Require(IsItemSelected(0) && !IsItemSelected(1),
                "Marquee did not use the full painted image bounds");
        Capture(capturePrefix + L"marquee.png");
        mouse(WM_LBUTTONUP, 0, b);
        // The pointer must keep its exact grab offset in the high-resolution preview.
        auto down = point(0);
        POINT up{down.x + 30, down.y + 20};
        auto before = ReadDesktopItems(state.listView, state.testHost);
        mouse(WM_LBUTTONDOWN, MK_LBUTTON, down);
        mouse(WM_MOUSEMOVE, MK_LBUTTON, up);
        Require(state.moving && state.dragBitmap &&
                    state.dragBitmap->GetWidth() == static_cast<UINT>(state.iconSize) &&
                    state.dragBitmap->GetHeight() == static_cast<UINT>(state.iconSize + 52),
                "Drag preview dimensions are incorrect");
        Require(state.dragHotspot.x == state.iconSize / 2 &&
                    state.dragHotspot.y == state.iconSize - 32,
                "Drag preview hotspot changed");
        SHDRAGIMAGE image{};
        Require(
            SendMessageW(state.listView, DragImageMessage(), 0, reinterpret_cast<LPARAM>(&image)) &&
                image.hbmpDragImage,
            "Shell drag image unavailable");
        bool shellImageOk = image.sizeDragImage.cx == state.iconSize &&
                            image.sizeDragImage.cy == state.iconSize + 52 &&
                            image.ptOffset.x == state.dragHotspot.x &&
                            image.ptOffset.y == state.dragHotspot.y;
        DeleteObject(image.hbmpDragImage);
        Require(shellImageOk, "Shell drag image differs from pointer preview");
        CaptureDrag(capturePrefix + L"drag-preview.png");
        Capture(capturePrefix + L"dragging.png");
        mouse(WM_LBUTTONUP, 0, up);
        CommitMoves();
        Require(state.testHost || state.positionCommitSucceeded,
                "Shell did not accept the single item position commit");
        auto after = ReadDesktopItems(state.listView, state.testHost);
        Require(after[0].position.x == before[0].position.x + 30 &&
                    after[0].position.y == before[0].position.y + 20 &&
                    after[1].position.x == before[1].position.x &&
                    after[1].position.y == before[1].position.y,
                "Single item move delta was not preserved");
        SynchronizeDesktop();
        auto synchronized = ReadDesktopItems(state.listView, state.testHost);
        Require(synchronized[0].position.x == after[0].position.x &&
                    synchronized[0].position.y == after[0].position.y,
                "Refresh reset the manual move");
        click(1, MK_CONTROL);
        before = ReadDesktopItems(state.listView, state.testHost);
        down = point(0);
        up = POINT{down.x + 17, down.y + 11};
        mouse(WM_LBUTTONDOWN, MK_LBUTTON, down);
        mouse(WM_MOUSEMOVE, MK_LBUTTON, up);
        mouse(WM_LBUTTONUP, 0, up);
        CommitMoves();
        after = ReadDesktopItems(state.listView, state.testHost);
        Require(state.testHost || state.positionCommitSucceeded,
                "Shell did not accept the group position commit");
        if (!(after[0].position.x == before[0].position.x + 17 &&
              after[1].position.x == before[1].position.x + 17 &&
              after[0].position.y == before[0].position.y + 11 &&
              after[1].position.y == before[1].position.y + 11)) {
            InteractionReport(path + L".group-debug.json");
            throw std::runtime_error(
                "Multiple item move lost relative positions: auto=" +
                std::to_string(IsAutoArrangeEnabled()) +
                " snap=" + std::to_string(IsSnapToGridEnabled()) +
                " before=" + std::to_string(before[0].position.x) + "," +
                std::to_string(before[0].position.y) + ";" + std::to_string(before[1].position.x) +
                "," + std::to_string(before[1].position.y) +
                " after=" + std::to_string(after[0].position.x) + "," +
                std::to_string(after[0].position.y) + ";" + std::to_string(after[1].position.x) +
                "," + std::to_string(after[1].position.y));
        }
        // Test a scrolled view: rectangle math stays in one coordinate system.
        ListView_Scroll(state.listView, 110, 70);
        GetPaintedItemRect(0, LVIR_BOUNDS, first);
        a = POINT{first.right + 10, first.bottom + 10};
        b = POINT{first.left + state.iconSize / 2, first.top + state.iconSize / 2};
        mouse(WM_LBUTTONDOWN, MK_LBUTTON, a);
        mouse(WM_MOUSEMOVE, MK_LBUTTON, b);
        marquee = MarqueeRect();
        Require(state.marquee && marquee.left == b.x && marquee.top == b.y &&
                    marquee.right == a.x && marquee.bottom == a.y && IsItemSelected(0) &&
                    !IsItemSelected(1),
                "Scrolled marquee coordinates changed");
        mouse(WM_KEYDOWN, VK_ESCAPE, POINT{});
        Require(!state.pressed && !state.marquee && !state.moving,
                "Escape did not cancel the gesture");
        cleanup();
        auto restored = ReadDesktopItems(state.listView, state.testHost);
        bool exactPositions = restored.size() == original.size();
        for (const auto& item : original) {
            auto found = std::find_if(restored.begin(), restored.end(), [&](const Item& value) {
                return value.key == item.key;
            });
            Require(found != restored.end(), "Interaction cleanup lost an item");
            exactPositions = exactPositions && found->position.x == item.position.x &&
                             found->position.y == item.position.y;
        }
        Require(IsAutoArrangeEnabled() == savedAutomatic && IsSnapToGridEnabled() == savedSnap,
                "Interaction cleanup did not restore alignment flags");
        // Re-enabling a native alignment mode may normalize the grid origin. The
        // script verifies the exact pre-apply positions after the patch is removed.
        Require(exactPositions || savedAutomatic || savedSnap,
                "Interaction cleanup changed free-placement positions");
        WriteUtf16(
            path, L"{\"passed\":true,\"size\":" + std::to_wstring(state.iconSize) +
                      L",\"lowerIconSelection\":true,\"controlToggle\":true,\"shiftRange\":true,"
                      L"\"reverseMarquee\":true,\"scrolledMarquee\":true,\"singleMove\":true,"
                      L"\"groupMove\":true,\"refreshPreservesMove\":true,\"dragPreviewAndHotspot\":"
                      L"true,\"shellDragImage\":true,\"escapeCancellation\":true,"
                      L"\"alignmentRestored\":true,\"positionsRestoredExactly\":" +
                      std::to_wstring(exactPositions) + L",\"nativeAlignmentMayReposition\":" +
                      std::to_wstring(savedAutomatic || savedSnap) + L"}");
    } catch (...) {
        cleanup();
        throw;
    }
}

void ValidateShellBehavior(const std::wstring& path) {
    Require(state.active && !state.contextActive && !state.pressed &&
                ListView_GetItemCount(state.listView) >= 2 && !path.empty(),
            "Shell behavior validation requires an idle patch and two items");
    auto original = ReadDesktopItems(state.listView, state.testHost);
    bool automatic = IsAutoArrangeEnabled(), snap = IsSnapToGridEnabled();
    BYTE savedKeys[256]{};
    Require(GetKeyboardState(savedKeys) != FALSE, "Keyboard state unavailable");
    auto testKeys = [&](bool shift) {
        BYTE keys[256]{};
        std::copy(std::begin(savedKeys), std::end(savedKeys), std::begin(keys));
        keys[VK_SHIFT] = keys[VK_LSHIFT] = shift ? 0x80 : 0;
        keys[VK_RSHIFT] = 0;
        Require(SetKeyboardState(keys) != FALSE, "Test keyboard state unavailable");
    };
    std::vector<bool> selection;
    for (int i = 0; i < static_cast<int>(original.size()); ++i) {
        selection.push_back(IsItemSelected(i));
    }
    auto cleanup = [&] {
        SetKeyboardState(savedKeys);
        state.menuTest = false;
        state.contextPending = false;
        KillTimer(state.listView, MenuTestTimer);
        CancelInteraction();
        SetTestAlignment(false, false);
        state.committing = true;
        for (size_t i = 0; i < original.size(); ++i) {
            ListView_SetItemPosition32(state.listView, static_cast<int>(i), original[i].position.x,
                                       original[i].position.y);
        }
        state.committing = false;
        state.pendingMoves = original;
        CommitMoves();
        for (int i = 0; i < static_cast<int>(original.size()); ++i) {
            SetItemSelected(i, selection[i]);
        }
        state.layoutPending = false;
        SetTestAlignment(automatic, snap);
    };
    auto titles = [] {
        std::wstring json = L"[";
        for (size_t i = 0; i < state.menuTitles.size(); ++i) {
            if (i) {
                json += L",";
            }
            json += QuoteInteraction(state.menuTitles[i]);
        }
        return json + L"]";
    };
    auto waitMenu = [&] {
        auto deadline = GetTickCount64() + 2000;
        while (GetTickCount64() < deadline) {
            MSG message{};
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
            PollContextMenu();
            if (state.menuSeen) {
                DismissTestMenu();
                if (!state.contextPopup || !IsWindowVisible(state.contextPopup)) {
                    break;
                }
            }
            MsgWaitForMultipleObjectsEx(0, nullptr, 10, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        }
    };
    auto rightClick = [&](POINT point, bool doubleClick) {
        auto before = state.contextMenus;
        auto requests = state.contextRequests;
        SendMessageW(state.listView, doubleClick ? WM_RBUTTONDBLCLK : WM_RBUTTONDOWN, MK_RBUTTON,
                     MAKELPARAM(point.x, point.y));
        SendMessageW(state.listView, WM_RBUTTONUP, 0, MAKELPARAM(point.x, point.y));
        SendMessageW(state.listView, ContextMessage(), 0, 0);
        waitMenu();
        Require(state.contextRequests == requests + 1 && state.contextMenus == before + 1 &&
                    !state.menuStyle.empty(),
                "One right click did not open exactly one native menu");
    };
    try {
        SetTestAlignment(false, false);
        ListView_SetItemPosition32(state.listView, 0, 173, 219);
        SynchronizeDesktop();
        POINT free{};
        ListView_GetItemPosition(state.listView, 0, &free);
        Require(!IsAutoArrangeEnabled() && !IsSnapToGridEnabled() && free.x == 173 && free.y == 219,
                "Free placement was not preserved");
        SetTestAlignment(false, true);
        Require(!IsAutoArrangeEnabled() && IsSnapToGridEnabled(), "Snap to grid was disabled");
        state.committing = true;
        ListView_SetItemPosition32(state.listView, 0, 173, 219);
        ListView_Arrange(state.listView, LVA_SNAPTOGRID);
        state.committing = false;
        POINT aligned{};
        ListView_GetItemPosition(state.listView, 0, &aligned);
        state.committing = true;
        ListView_Arrange(state.listView, LVA_SNAPTOGRID);
        state.committing = false;
        POINT repeated{};
        ListView_GetItemPosition(state.listView, 0, &repeated);
        Require((aligned.x != 173 || aligned.y != 219) && aligned.x == repeated.x &&
                    aligned.y == repeated.y,
                "Grid placement was not stable");
        SynchronizeDesktop();
        Require(IsSnapToGridEnabled(), "Refresh disabled snap to grid");
        for (int i = 0; i < ListView_GetItemCount(state.listView); ++i) {
            for (int j = i + 1; j < ListView_GetItemCount(state.listView); ++j) {
                RECT a{}, b{}, intersection{};
                GetPaintedItemRect(i, LVIR_BOUNDS, a);
                GetPaintedItemRect(j, LVIR_BOUNDS, b);
                Require(!IntersectRect(&intersection, &a, &b),
                        "Snap grid collapsed large icons into one cell");
            }
        }
        ClearSelection();
        SetItemSelected(0, true);
        FocusItem(0);
        auto gridBefore = ReadDesktopItems(state.listView, state.testHost);
        RECT dragged{};
        GetPaintedItemRect(0, LVIR_ICON, dragged);
        POINT down{dragged.left + state.iconSize / 2, dragged.top + state.iconSize / 2},
            up{down.x + 37, down.y + 29};
        SendMessageW(state.listView, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(down.x, down.y));
        SendMessageW(state.listView, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(up.x, up.y));
        SendMessageW(state.listView, WM_LBUTTONUP, 0, MAKELPARAM(up.x, up.y));
        CommitMoves();
        auto gridAfter = ReadDesktopItems(state.listView, state.testHost);
        for (size_t i = 0; i < gridBefore.size(); ++i) {
            Require(gridBefore[i].position.x == gridAfter[i].position.x &&
                        gridBefore[i].position.y == gridAfter[i].position.y,
                    "A short drag escaped the snap grid");
        }
        SetTestAlignment(true, true);
        Require(IsAutoArrangeEnabled() && IsSnapToGridEnabled(), "Auto arrange was disabled");
        auto arranged = ReadDesktopItems(state.listView, state.testHost);
        QueueSynchronization(true);
        SynchronizeDesktop();
        auto again = ReadDesktopItems(state.listView, state.testHost);
        for (size_t i = 0; i < arranged.size(); ++i) {
            Require(arranged[i].position.x == again[i].position.x &&
                        arranged[i].position.y == again[i].position.y,
                    "Auto arrangement was not stable");
        }
        for (size_t i = 0; i < arranged.size(); ++i) {
            for (size_t j = i + 1; j < arranged.size(); ++j) {
                RECT a{}, b{}, intersection{};
                GetPaintedItemRect(static_cast<int>(i), LVIR_BOUNDS, a);
                GetPaintedItemRect(static_cast<int>(j), LVIR_BOUNDS, b);
                Require(!IntersectRect(&intersection, &a, &b), "Auto arranged giant icons overlap");
            }
        }
        GetPaintedItemRect(0, LVIR_ICON, dragged);
        down = POINT{dragged.left + state.iconSize / 2, dragged.top + state.iconSize / 2};
        up = POINT{down.x + 20, down.y + 20};
        auto movesBefore = state.moveCommits;
        SendMessageW(state.listView, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(down.x, down.y));
        SendMessageW(state.listView, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(up.x, up.y));
        SendMessageW(state.listView, WM_LBUTTONUP, 0, MAKELPARAM(up.x, up.y));
        SynchronizeDesktop();
        Require(IsAutoArrangeEnabled() && state.moveCommits == movesBefore,
                "Fast auto-arrange drop used free placement");
        auto fastDrop = ReadDesktopItems(state.listView, state.testHost);
        for (size_t i = 0; i < arranged.size(); ++i) {
            Require(arranged[i].position.x == fastDrop[i].position.x &&
                        arranged[i].position.y == fastDrop[i].position.y,
                    "Auto-arrange drop moved items off the grid");
        }
        if (!state.testHost) {
            for (bool mode : {true, false}) {
                for (int repeat = 0; repeat < 2; ++repeat) {
                    bool enabled = ToggleAlignmentMenu(mode);
                    Require((mode ? IsAutoArrangeEnabled() : IsSnapToGridEnabled()) == enabled,
                            "Native menu alignment setting was not respected");
                }
            }
        }
        // Exercise the complete production right-button path and native menu loop.
        // A test-only timer cancels the popup without choosing any menu command.
        state.menuTest = true;
        testKeys(false);
        RECT client{}, icon{};
        GetClientRect(state.listView, &client);
        GetPaintedItemRect(0, LVIR_ICON, icon);
        POINT item{icon.left + state.iconSize / 2, icon.top + state.iconSize - 24}, empty{-1, -1};
        for (LONG y = client.bottom - 4; y >= 0 && empty.x < 0; y -= 16) {
            for (LONG x = client.right - 4; x >= 0; x -= 16) {
                if (HitTestPaintedItem(POINT{x, y}) < 0) {
                    empty = POINT{x, y};
                    break;
                }
            }
        }
        Require(empty.x >= 0, "No empty desktop point available for menu validation");
        std::wstring backgroundTitles, itemTitles, keyboardTitles, defaultStyle;
        auto initialMenus = state.contextMenus;
        for (int i = 0; i < 6; ++i) {
            rightClick(empty, (i & 1) != 0);
            Require(state.testHost || !state.contextItem, "Background routed as selection");
            if (!i) {
                backgroundTitles = titles();
                defaultStyle = state.menuStyle;
            }
        }
        for (int i = 0; i < 6; ++i) {
            rightClick(item, (i & 1) != 0);
            Require(state.testHost || state.contextItem, "Selection routed as background");
            if (!i) {
                itemTitles = titles();
            }
        }
        if (state.menuStyle == L"classic") {
            Require(backgroundTitles != itemTitles, "Item right click opened the background menu");
        }
        for (int i = 0; i < 6; ++i) {
            auto before = state.contextMenus;
            SendMessageW(state.listView, WM_CONTEXTMENU, reinterpret_cast<WPARAM>(state.listView),
                         MAKELPARAM(-1, -1));
            SendMessageW(state.listView, ContextMessage(), 0, 0);
            waitMenu();
            Require(state.contextMenus == before + 1 && !state.menuStyle.empty(),
                    "Keyboard context menu did not open");
            if (!i) {
                keyboardTitles = titles();
            }
        }
        auto menus = state.contextMenus - initialMenus;
        UINT64 shiftMenus = 0;
        if (!state.testHost) {
            testKeys(true);
            auto before = state.contextMenus;
            rightClick(empty, false);
            Require(state.menuStyle == L"classic" && !state.menuTitles.empty(),
                    "Shift background menu was not classic");
            testKeys(true);
            rightClick(item, false);
            Require(state.menuStyle == L"classic" && !state.menuTitles.empty(),
                    "Shift item menu was not classic");
            testKeys(true);
            SendMessageW(state.listView, WM_CONTEXTMENU, reinterpret_cast<WPARAM>(state.listView),
                         MAKELPARAM(-1, -1));
            SendMessageW(state.listView, ContextMessage(), 0, 0);
            waitMenu();
            Require(state.menuStyle == L"classic" && !state.menuTitles.empty(),
                    "Shift keyboard menu was not classic");
            shiftMenus = state.contextMenus - before;
            Require(shiftMenus == 3, "Shift menu count changed");
        }
        state.menuTest = false;
        cleanup();
        WriteUtf16(path,
                   L"{\"passed\":true,\"size\":" + std::to_wstring(state.iconSize) +
                       L",\"freePlacement\":true,\"snapToGrid\":true,\"autoArrange\":true,"
                       L"\"noOverlap\":true,\"nativeAlignmentMenuToggle\":" +
                       std::to_wstring(!state.testHost) + L",\"nativeMenusOpened\":" +
                       std::to_wstring(menus) + L",\"defaultStyle\":" +
                       QuoteInteraction(defaultStyle) + L",\"shiftClassicMenusOpened\":" +
                       std::to_wstring(shiftMenus) +
                       L",\"singleRightClick\":true,\"keyboardContext\":true,\"backgroundMenu\":" +
                       backgroundTitles + L",\"itemMenu\":" + itemTitles + L",\"keyboardMenu\":" +
                       keyboardTitles + L"}");
    } catch (const std::exception& ex) {
        wchar_t message[512]{};
        MultiByteToWideChar(CP_UTF8, 0, ex.what(), -1, message, 512);
        try {
            WriteUtf16(path, L"{\"passed\":false,\"reason\":" + QuoteInteraction(message) +
                                 L",\"requests\":" + std::to_wstring(state.contextRequests) +
                                 L",\"menus\":" + std::to_wstring(state.contextMenus) +
                                 L",\"titles\":" + titles() + L"}");
        } catch (...) {
        }
        cleanup();
        throw;
    } catch (...) {
        cleanup();
        throw;
    }
}
