// SPDX-License-Identifier: MIT
// Copyright (c) 2026 sh4
// Included by the core inside its implementation namespace.
void DrawItem(NMLVCUSTOMDRAW* draw);
void Capture(const std::wstring& path);

DWORD NativeDropHighlightColor(const std::wstring& path) {
    struct Reference {
        HWND parent = CreateWindowExW(0, L"STATIC", L"", WS_POPUP, 0, 0, 224, 224, nullptr, nullptr,
                                      moduleHandle, nullptr);
        HWND list = nullptr;
        HIMAGELIST images = nullptr;

        ~Reference() {
            if (parent) {
                DestroyWindow(parent);
            }
            if (images) {
                ImageList_Destroy(images);
            }
        }
    } reference;

    Require(reference.parent != nullptr, "Native theme reference window unavailable");
    reference.list =
        CreateWindowExW(0, WC_LISTVIEWW, L"", WS_CHILD | LVS_ICON | LVS_SHAREIMAGELISTS, 0, 0, 224,
                        224, reference.parent, nullptr, moduleHandle, nullptr);
    Require(reference.list != nullptr, "Native theme reference list unavailable");
    SetWindowTheme(reference.list, L"Explorer", nullptr);
    ListView_SetBkColor(reference.list, RGB(20, 30, 40));
    reference.images = ImageList_Create(64, 64, ILC_COLOR32 | ILC_MASK, 1, 1);
    Require(reference.images && ImageList_SetImageCount(reference.images, 1),
            "Native theme reference image list unavailable");
    ListView_SetImageList(reference.list, reference.images, LVSIL_NORMAL);
    ListView_SetIconSpacing(reference.list, 100, 110);
    LVITEMW item{};
    item.mask = LVIF_TEXT | LVIF_IMAGE;
    item.pszText = const_cast<PWSTR>(L"Reference");
    Require(ListView_InsertItem(reference.list, &item) >= 0,
            "Native theme reference item unavailable");
    ListView_SetItemPosition32(reference.list, 0, 32, 32);
    ListView_SetItemState(reference.list, 0, LVIS_DROPHILITED, LVIS_DROPHILITED);
    Gdiplus::Bitmap bitmap(224, 224, PixelFormat32bppARGB);
    Gdiplus::Graphics graphics(&bitmap);
    graphics.Clear(Gdiplus::Color(255, 20, 30, 40));
    HDC dc = graphics.GetHDC();
    SendMessageW(reference.list, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(dc),
                 PRF_CLIENT | PRF_ERASEBKGND);
    graphics.ReleaseHDC(dc);
    auto png = PngEncoder();
    bitmap.Save(path.c_str(), &png, nullptr);
    RECT bounds{};
    ListView_GetItemRect(reference.list, 0, &bounds, LVIR_BOUNDS);
    std::map<DWORD, int> colors;
    for (int y = std::max(0L, bounds.top); y < std::min(224L, bounds.bottom); ++y) {
        for (int x = std::max(0L, bounds.left); x < std::min(224L, bounds.right); ++x) {
            Gdiplus::Color pixel;
            bitmap.GetPixel(x, y, &pixel);
            DWORD color = pixel.GetValue() & 0xffffff;
            if (color != 0x141e28 && color != 0) {
                ++colors[color];
            }
        }
    }
    Require(!colors.empty(), "Native drop theme did not paint");
    return std::max_element(colors.begin(), colors.end(),
                            [](const auto& left, const auto& right) {
                                return left.second < right.second;
                            })
        ->first;
}

LRESULT CALLBACK DropUpdateObserver(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR,
                                    DWORD_PTR reference) {
    if (message == WM_USER + 3) {
        ++*reinterpret_cast<int*>(reference);
        return 0;
    }
    return DefSubclassProc(hwnd, message, wParam, lParam);
}

void ValidateDropWindowUpdate(IDataObject* data) {
    HWND observer = CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr,
                                    moduleHandle, nullptr);
    Require(observer != nullptr, "Drop update observer unavailable");
    int updates = 0;
    SetWindowSubclass(observer, DropUpdateObserver, 1, reinterpret_cast<DWORD_PTR>(&updates));
    FORMATETC format{static_cast<CLIPFORMAT>(RegisterClipboardFormatW(L"DragWindow")), nullptr,
                     DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
    STGMEDIUM original{};
    bool saved = SUCCEEDED(data->GetData(&format, &original));
    STGMEDIUM medium{};
    medium.tymed = TYMED_HGLOBAL;
    medium.hGlobal = GlobalAlloc(GMEM_MOVEABLE, sizeof(DWORD));
    auto value = static_cast<DWORD*>(GlobalLock(medium.hGlobal));
    if (!value) {
        ReleaseStgMedium(&medium);
        if (saved) {
            ReleaseStgMedium(&original);
        }
        DestroyWindow(observer);
        throw std::runtime_error("Drop observer data allocation failed");
    }
    *value = HandleToULong(observer);
    GlobalUnlock(medium.hGlobal);
    HRESULT hr = data->SetData(&format, &medium, TRUE);
    if (FAILED(hr)) {
        ReleaseStgMedium(&medium);
    } else {
        DesktopDropSource source(data);
        source.GiveFeedback(DROPEFFECT_MOVE);
        MSG message{};
        while (PeekMessageW(&message, observer, WM_USER + 3, WM_USER + 3, PM_REMOVE)) {
            DispatchMessageW(&message);
        }
    }
    if (saved && FAILED(data->SetData(&format, &original, TRUE))) {
        ReleaseStgMedium(&original);
    }
    DestroyWindow(observer);
    Require(SUCCEEDED(hr) && updates == 1, "GiveFeedback did not update the drop image window");
}

struct DropFeedback {
    HRESULT entered = E_FAIL;
    HRESULT over = E_FAIL;
    DWORD effect = 0;
    UINT highlight = 0;
    std::wstring description;
};

class RecordingDropTarget final : public IDropTarget {
    ULONG references = 1;

  public:
    int enters = 0, overs = 0, leaves = 0, drops = 0;
    IDataObject* object = nullptr;
    DWORD modifiers = 0;
    POINTL point{};
    bool showHighlight = false;
    int hit = -1;

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** result) override {
        if (!result) {
            return E_POINTER;
        }
        *result = nullptr;
        if (iid != IID_IUnknown && iid != IID_IDropTarget) {
            return E_NOINTERFACE;
        }
        *result = static_cast<IDropTarget*>(this);
        AddRef();
        return S_OK;
    }

    ULONG STDMETHODCALLTYPE AddRef() override {
        return ++references;
    }

    ULONG STDMETHODCALLTYPE Release() override {
        ULONG remaining = --references;
        if (!remaining) {
            delete this;
        }
        return remaining;
    }

    HRESULT STDMETHODCALLTYPE DragEnter(IDataObject* data, DWORD keys, POINTL screen,
                                        DWORD* effect) override {
        ++enters;
        object = data;
        return DragOver(keys, screen, effect);
    }

    HRESULT STDMETHODCALLTYPE DragOver(DWORD keys, POINTL screen, DWORD* effect) override {
        ++overs;
        modifiers = keys;
        point = screen;
        POINT client{screen.x, screen.y};
        ScreenToClient(state.listView, &client);
        hit = HitTestNativeItem(client);
        if (showHighlight) {
            ListView_SetItemState(state.listView, -1, 0, LVIS_DROPHILITED);
            if (hit >= 0) {
                ListView_SetItemState(state.listView, hit, LVIS_DROPHILITED, LVIS_DROPHILITED);
            }
        }
        *effect = DROPEFFECT_COPY;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE DragLeave() override {
        ++leaves;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Drop(IDataObject* data, DWORD keys, POINTL screen,
                                   DWORD* effect) override {
        ++drops;
        object = data;
        return DragOver(keys, screen, effect);
    }
};

DropFeedback ProbeDropFeedback(IDropTarget* target, IDataObject* data, DWORD effects, POINT screen,
                               DWORD keys, int index = 1) {
    DropFeedback result;
    result.effect = effects;
    result.entered = target->DragEnter(data, keys, POINTL{screen.x, screen.y}, &result.effect);
    result.effect = effects;
    result.over = target->DragOver(keys, POINTL{screen.x, screen.y}, &result.effect);
    result.highlight = ListView_GetItemState(state.listView, index, LVIS_DROPHILITED);
    FORMATETC format{static_cast<CLIPFORMAT>(RegisterClipboardFormatW(CFSTR_DROPDESCRIPTION)),
                     nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
    STGMEDIUM medium{};
    if (SUCCEEDED(data->GetData(&format, &medium))) {
        auto drop = static_cast<DROPDESCRIPTION*>(GlobalLock(medium.hGlobal));
        if (drop && GlobalSize(medium.hGlobal) >= sizeof(DROPDESCRIPTION) &&
            drop->type != DROPIMAGE_INVALID) {
            result.description = std::wstring(drop->szMessage) + L" / " + drop->szInsert;
        }
        if (drop) {
            GlobalUnlock(medium.hGlobal);
        }
        ReleaseStgMedium(&medium);
    }
    target->DragLeave();
    result.effect &= DROPEFFECT_COPY | DROPEFFECT_MOVE | DROPEFFECT_LINK;
    return result;
}

Gdiplus::ARGB DropHighlightPixel(int index, UINT customState = 0) {
    RECT rect{};
    Require(GetPaintedItemRect(index, LVIR_ICON, rect), "Drop target rectangle unavailable");
    Gdiplus::Bitmap bitmap(state.iconSize + 16, state.iconSize + 80, PixelFormat32bppARGB);
    Gdiplus::Graphics graphics(&bitmap);
    graphics.Clear(Gdiplus::Color(255, 20, 30, 40));
    HDC dc = graphics.GetHDC();
    SetViewportOrgEx(dc, 8 - rect.left, 8 - rect.top, nullptr);
    NMLVCUSTOMDRAW draw{};
    draw.nmcd.hdc = dc;
    draw.nmcd.dwItemSpec = index;
    draw.nmcd.uItemState = customState;
    DrawItem(&draw);
    graphics.ReleaseHDC(dc);
    Gdiplus::Color pixel;
    Require(bitmap.GetPixel(6, 20, &pixel) == Gdiplus::Ok, "Drop highlight pixel unavailable");
    return pixel.GetValue();
}

void ValidateDragSourceExclusion(IDataObject* data, DWORD effects) {
    struct SourceSnapshot {
        bool external = state.externalDrag;
        std::vector<Item> items = state.dragItems;

        ~SourceSnapshot() {
            state.externalDrag = external;
            state.dragItems = std::move(items);
            ListView_SetItemState(state.listView, -1, 0, LVIS_DROPHILITED);
        }
    } saved;

    state.externalDrag = true;
    Require(IsDragSourceItem(0) && !IsDragSourceItem(1),
            "Drag sources do not match the initial drag selection");
    std::swap(state.items[0], state.items[1]);
    bool identityMatched = !IsDragSourceItem(0) && IsDragSourceItem(1);
    std::swap(state.items[0], state.items[1]);
    Require(identityMatched, "Drag source exclusion follows indices instead of item identity");

    ComPtr<RecordingDropTarget> recorded;
    recorded.Attach(new RecordingDropTarget);
    recorded->showHighlight = true;
    PaintedDropTarget proxy(recorded.Get());
    auto pointFor = [](int index) {
        RECT rect{};
        GetPaintedItemRect(index, LVIR_ICON, rect);
        POINT point{rect.left + state.iconSize / 2, rect.top + 2};
        ClientToScreen(state.listView, &point);
        return POINTL{point.x, point.y};
    };
    DWORD effect = effects;
    proxy.DragEnter(data, MK_LBUTTON, pointFor(0), &effect);
    Require(recorded->hit < 0, "DragEnter targeted the source icon");
    proxy.DragOver(MK_LBUTTON, pointFor(1), &effect);
    Require(recorded->hit == 1 && ListView_GetItemState(state.listView, 1, LVIS_DROPHILITED),
            "Source exclusion disabled an ordinary destination");
    proxy.DragOver(MK_LBUTTON, pointFor(0), &effect);
    Require(recorded->hit < 0 && !ListView_GetItemState(state.listView, 1, LVIS_DROPHILITED) &&
                IsItemSelected(0),
            "Returning to the source did not clear the destination highlight");
    proxy.DragLeave();

    // Every dragged item, including those that did not initiate the mouse gesture,
    // must be excluded for feedback and the actual drop, with all modifier keys.
    state.dragItems.push_back(state.items[1]);
    for (int index : {0, 1}) {
        Require(IsDragSourceItem(index), "A member of the drag group is still a destination");
        RECT icon{}, label{};
        GetPaintedItemRect(index, LVIR_ICON, icon);
        GetPaintedItemRect(index, LVIR_LABEL, label);
        for (auto point : {POINT{icon.left + state.iconSize / 2, icon.top + 2},
                           POINT{icon.left + state.iconSize / 2, icon.bottom - 2},
                           POINT{label.left + state.iconSize / 2, label.top + 2}}) {
            ClientToScreen(state.listView, &point);
            POINTL screen{point.x, point.y};
            for (DWORD keys :
                 {DWORD(MK_LBUTTON), DWORD(MK_LBUTTON | MK_CONTROL), DWORD(MK_LBUTTON | MK_SHIFT),
                  DWORD(MK_LBUTTON | MK_CONTROL | MK_SHIFT), DWORD(MK_RBUTTON)}) {
                effect = effects;
                proxy.DragEnter(data, keys, screen, &effect);
                Require(recorded->hit < 0, "DragEnter targeted a member of its source group");
                proxy.DragOver(keys, screen, &effect);
                Require(recorded->hit < 0, "DragOver targeted a member of its source group");
                proxy.Drop(data, keys, screen, &effect);
                Require(recorded->hit < 0 && recorded->modifiers == keys,
                        "Drop targeted a member of its source group or lost modifiers");

                if (!state.testHost) {
                    auto mapped = NativeDropPoint(screen);
                    auto expected = ProbeDropFeedback(state.originalDropTarget.Get(), data, effects,
                                                      POINT{mapped.x, mapped.y}, keys, index);
                    auto actual = ProbeDropFeedback(state.dropTarget.Get(), data, effects, point,
                                                    keys, index);
                    Require(SUCCEEDED(actual.entered) && SUCCEEDED(actual.over) &&
                                !actual.highlight && actual.effect == expected.effect &&
                                actual.description == expected.description,
                            "Source feedback differs from Explorer's desktop background");
                }
            }
        }
    }

    // An incoming drag may still target a selected icon in this view. A retained
    // snapshot from a completed local gesture must not suppress that destination.
    state.externalDrag = false;
    Require(!IsDragSourceItem(0), "Source exclusion leaked into an incoming drag");
    effect = effects;
    proxy.DragEnter(data, MK_LBUTTON, pointFor(0), &effect);
    Require(recorded->hit == 0, "An incoming drag cannot target a selected desktop icon");
    proxy.DragLeave();
}

void ValidateDragFeedback(const std::wstring& prefix) {
    ClearSelection();
    SetItemSelected(0, true);
    ListView_SetItemState(state.listView, -1, 0, LVIS_DROPHILITED);
    auto baseline = DropHighlightPixel(1);
    ListView_SetItemState(state.listView, 1, LVIS_DROPHILITED, LVIS_DROPHILITED);
    Require(DropHighlightPixel(1) != baseline, "Native drop highlight was not painted");
    Capture(prefix + L"drop-highlight.png");
    if (state.testHost) {
        // Both controls use Explorer::ListView. The actual desktop has its own
        // theme override, which remains authoritative for its custom drawing.
        DWORD nativeColor = NativeDropHighlightColor(prefix + L"native-drop-theme.png");
        DWORD paintedColor = DropHighlightPixel(1) & 0xffffff;
        if (paintedColor != nativeColor) {
            throw std::runtime_error("Drop highlight differs from native Explorer: painted=" +
                                     std::to_string(paintedColor) +
                                     " native=" + std::to_string(nativeColor));
        }
    }
    Require(IsItemSelected(0) && !IsItemSelected(1), "Drop highlight changed the drag selection");
    ListView_SetItemState(state.listView, -1, 0, LVIS_DROPHILITED);
    Require(DropHighlightPixel(1) == baseline, "Drop highlight remained after leaving the target");
    Require(DropHighlightPixel(1, CDIS_DROPHILITED) != baseline,
            "Custom-draw drop highlight was not painted");

    auto savedOrigin = ViewOrigin();
    for (int scrolled = 0; scrolled < 2; ++scrolled) {
        RECT icon{}, label{};
        GetPaintedItemRect(1, LVIR_ICON, icon);
        GetPaintedItemRect(1, LVIR_LABEL, label);
        for (auto point : {POINT{icon.left + state.iconSize / 2, icon.bottom - 2},
                           POINT{label.left + state.iconSize / 2, label.top + 2}}) {
            LVHITTESTINFO hit{};
            hit.pt = point;
            Require(ListView_HitTest(state.listView, &hit) == 1,
                    "Drop hit test missed the painted icon or label");
            hit = {};
            hit.pt = point;
            Require(ListView_SubItemHitTest(state.listView, &hit) == 1 && hit.iSubItem == 0,
                    "Shell subitem hit test missed the painted icon or label");
        }
        ListView_Scroll(state.listView, 110, 70);
    }
    auto origin = ViewOrigin();
    ListView_Scroll(state.listView, savedOrigin.x - origin.x, savedOrigin.y - origin.y);

    for (int index = 0; index < ListView_GetItemCount(state.listView); ++index) {
        RECT bounds{};
        GetPaintedItemRect(index, LVIR_BOUNDS, bounds);
        POINT empty{bounds.left + state.iconSize / 2, bounds.bottom + 1};
        if (HitTestPaintedItem(empty) < 0 && HitTestNativeItem(empty) >= 0) {
            ClientToScreen(state.listView, &empty);
            auto mapped = NativeDropPoint(POINTL{empty.x, empty.y});
            POINT native{mapped.x, mapped.y};
            ScreenToClient(state.listView, &native);
            Require(HitTestNativeItem(native) < 0,
                    "Visible empty space still targets a native icon or label");
        }
    }

    RECT sourceRect{};
    GetPaintedItemRect(0, LVIR_ICON, sourceRect);
    state.downView = ToView(POINT{sourceRect.left + state.iconSize / 2, sourceRect.bottom - 32});
    BuildDragBitmap();
    DWORD effects = 0;
    auto selection = ShellDragData(effects);
    Require((effects & DROPEFFECT_MOVE) != 0, "Desktop positioning is not an allowed drag effect");
    auto helper = InitializeShellDragImage(selection.Get());

    // Integration check against the real Windows helper: custom images must opt
    // into descriptions, even when the target supplies a valid DROPDESCRIPTION.
    FORMATETC flags{static_cast<CLIPFORMAT>(RegisterClipboardFormatW(L"DragSourceHelperFlags")),
                    nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
    STGMEDIUM medium{};
    Require(SUCCEEDED(selection->GetData(&flags, &medium)), "Shell drag image flags unavailable");
    auto value = static_cast<DWORD*>(GlobalLock(medium.hGlobal));
    bool descriptions = value && GlobalSize(medium.hGlobal) >= sizeof(DWORD) &&
                        (*value & DSH_ALLOWDROPDESCRIPTIONTEXT) != 0;
    if (value) {
        GlobalUnlock(medium.hGlobal);
    }
    ReleaseStgMedium(&medium);
    Require(descriptions, "Custom drag image suppresses Shell operation descriptions");
    flags.cfFormat = static_cast<CLIPFORMAT>(RegisterClipboardFormatW(L"IsShowingText"));
    medium = {};
    Require(SUCCEEDED(selection->GetData(&flags, &medium)),
            "Shell drop text visibility unavailable");
    value = static_cast<DWORD*>(GlobalLock(medium.hGlobal));
    bool showingText = value && GlobalSize(medium.hGlobal) >= sizeof(BOOL) && *value;
    if (value) {
        GlobalUnlock(medium.hGlobal);
    }
    ReleaseStgMedium(&medium);
    Require(showingText, "Custom drag bitmap disabled the operation text");
    ValidateDropWindowUpdate(selection.Get());

    // Verify that accepting feedback and the eventual Drop reach the same native
    // item, with the original data and modifiers. This target never touches files.
    ComPtr<RecordingDropTarget> recorded;
    recorded.Attach(new RecordingDropTarget);
    PaintedDropTarget proxy(recorded.Get());
    RECT dropRect{};
    GetPaintedItemRect(1, LVIR_ICON, dropRect);
    POINT dropPoint{dropRect.left + state.iconSize / 2, dropRect.bottom - 2};
    ClientToScreen(state.listView, &dropPoint);
    DWORD copy = DROPEFFECT_COPY;
    POINTL screen{dropPoint.x, dropPoint.y};
    proxy.DragEnter(selection.Get(), MK_LBUTTON | MK_CONTROL, screen, &copy);
    POINTL nativePoint = recorded->point;
    proxy.DragOver(MK_LBUTTON | MK_CONTROL, screen, &copy);
    proxy.Drop(selection.Get(), MK_CONTROL, screen, &copy);
    Require(recorded->enters == 1 && recorded->drops == 1 && recorded->object == selection.Get() &&
                recorded->modifiers == MK_CONTROL && recorded->point.x == nativePoint.x &&
                recorded->point.y == nativePoint.y && recorded->hit == 1 && copy == DROPEFFECT_COPY,
            "Shell Drop did not retain its target, data, modifiers, or effect");
    proxy.DragEnter(selection.Get(), MK_LBUTTON, screen, &copy);
    proxy.DragLeave();
    Require(recorded->leaves == 1, "Leaving a painted drop target was not delegated");

    ValidateDragSourceExclusion(selection.Get(), effects);

    if (!state.testHost) {
        Require(state.dropTarget && state.originalDropTarget, "Explorer drop target unavailable");
        for (DWORD keys : {DWORD(MK_LBUTTON), DWORD(MK_LBUTTON | MK_CONTROL),
                           DWORD(MK_LBUTTON | MK_SHIFT), DWORD(MK_RBUTTON)}) {
            RECT icon{}, label{};
            GetPaintedItemRect(1, LVIR_ICON, icon);
            GetPaintedItemRect(1, LVIR_LABEL, label);
            POINT mapped{nativePoint.x, nativePoint.y};
            auto expected = ProbeDropFeedback(state.originalDropTarget.Get(), selection.Get(),
                                              effects, mapped, keys);
            for (auto point : {POINT{icon.left + state.iconSize / 2, icon.bottom - 2},
                               POINT{label.left + state.iconSize / 2, label.top + 2}}) {
                ClientToScreen(state.listView, &point);
                auto actual = ProbeDropFeedback(state.dropTarget.Get(), selection.Get(), effects,
                                                point, keys);
                Require(SUCCEEDED(actual.entered) && SUCCEEDED(actual.over) &&
                            actual.effect == expected.effect &&
                            actual.highlight == expected.highlight &&
                            actual.description == expected.description,
                        "Painted drop target differs from Explorer's native feedback");
                Require(!ListView_GetItemState(state.listView, 1, LVIS_DROPHILITED),
                        "Explorer did not clear its drop highlight on leave");
            }
        }
    }

    DesktopDropSource source;
    state.rightButton = false;
    Require(source.QueryContinueDrag(FALSE, MK_LBUTTON) == S_OK &&
                source.QueryContinueDrag(TRUE, MK_LBUTTON) == DRAGDROP_S_CANCEL &&
                source.QueryContinueDrag(FALSE, MK_RBUTTON) == DRAGDROP_S_CANCEL &&
                source.QueryContinueDrag(FALSE, MK_CONTROL) == DRAGDROP_S_DROP &&
                source.QueryContinueDrag(FALSE, MK_CONTROL | MK_SHIFT) == DRAGDROP_S_DROP &&
                source.GiveFeedback(DROPEFFECT_COPY) == DRAGDROP_S_USEDEFAULTCURSORS,
            "Shell drag continuation, cancellation, or modifier handling changed");
    state.rightButton = true;
    Require(source.QueryContinueDrag(FALSE, MK_RBUTTON) == S_OK &&
                source.QueryContinueDrag(FALSE, 0) == DRAGDROP_S_DROP,
            "Right-button drag cannot use the Shell drop menu");
    state.rightButton = false;
    CancelInteraction();
}

void ValidateDropFeedback(const std::wstring& path) {
    Require(state.active && !state.pressed && !state.externalDrag && !path.empty() &&
                ListView_GetItemCount(state.listView) >= 2,
            "Drop feedback validation requires an idle patch and two items");
    std::vector<bool> selected;
    for (int i = 0; i < ListView_GetItemCount(state.listView); ++i) {
        selected.push_back(IsItemSelected(i));
    }
    auto savedOrigin = ViewOrigin();
    bool right = state.rightButton;
    auto cleanup = [&] {
        CancelInteraction();
        ListView_SetItemState(state.listView, -1, 0, LVIS_DROPHILITED);
        for (int i = 0; i < static_cast<int>(selected.size()); ++i) {
            SetItemSelected(i, selected[i]);
        }
        auto origin = ViewOrigin();
        ListView_Scroll(state.listView, savedOrigin.x - origin.x, savedOrigin.y - origin.y);
        state.rightButton = right;
    };
    try {
        ValidateDragFeedback(path + L"-");
        cleanup();
        WriteUtf16(path, L"{\"passed\":true,\"size\":" + std::to_wstring(state.iconSize) +
                             L",\"dropHighlight\":true,\"dropHitTests\":true,"
                             L"\"shellDropDescriptions\":true,\"dropDelegation\":true,"
                             L"\"dropTextVisibility\":true,\"dropImageWindowUpdate\":true,"
                             L"\"dragSourceExcluded\":true,\"dragGroupExcluded\":true,"
                             L"\"nativeDropFeedback\":" +
                             std::to_wstring(!state.testHost) + L"}");
    } catch (...) {
        cleanup();
        throw;
    }
}
