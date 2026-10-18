// SPDX-License-Identifier: MIT
// Copyright (c) 2026 sh4
// Included inside DekaDesktopIconCore.cpp's implementation namespace, after HitTestPaintedItem.
// Pointer gestures use view coordinates throughout. No synthetic cursor offsets.
UINT CommitMessage() {
    return RegisterWindowMessageW(L"DekaDesktopIcon.CommitMove.v1");
}

UINT NativeDragMessage() {
    return RegisterWindowMessageW(L"DekaDesktopIcon.NativeDrag.v1");
}

UINT DragImageMessage() {
    return RegisterWindowMessageW(L"ShellGetDragImage");
}

POINT ViewOrigin() {
    POINT point{};
    ListView_GetOrigin(state.listView, &point);
    return point;
}

POINT ToView(POINT point) {
    auto origin = ViewOrigin();
    return POINT{point.x + origin.x, point.y + origin.y};
}

POINT ToClient(POINT point) {
    auto origin = ViewOrigin();
    return POINT{point.x - origin.x, point.y - origin.y};
}

RECT MarqueeRect() {
    auto a = ToClient(state.downView), b = ToClient(state.currentView);
    return RECT{std::min(a.x, b.x), std::min(a.y, b.y), std::max(a.x, b.x), std::max(a.y, b.y)};
}

bool IsItemSelected(int index) {
    return (ListView_GetItemState(state.listView, index, LVIS_SELECTED) & LVIS_SELECTED) != 0;
}

void SetItemSelected(int index, bool value) {
    if (IsItemSelected(index) != value) {
        ListView_SetItemState(state.listView, index, value ? LVIS_SELECTED : 0, LVIS_SELECTED);
    }
}

void ClearSelection() {
    ListView_SetItemState(state.listView, -1, 0, LVIS_SELECTED);
}

void FocusItem(int index) {
    ListView_SetItemState(state.listView, -1, 0, LVIS_FOCUSED);
    ListView_SetItemState(state.listView, index, LVIS_FOCUSED, LVIS_FOCUSED);
}

void CancelInteraction() {
    state.pressed = state.moving = state.marquee = false;
    state.nativeDragPending = false;
    if (state.listView && GetCapture() == state.listView) {
        ReleaseCapture();
    }
    if (!state.externalDrag) {
        state.dragBitmap.reset();
        state.dragItems.clear();
    }
    if (state.listView && IsWindow(state.listView)) {
        InvalidateRect(state.listView, nullptr, FALSE);
    }
}

void ClickSelection(int index, WPARAM modifiers, bool right) {
    bool control = (modifiers & MK_CONTROL) != 0, shift = (modifiers & MK_SHIFT) != 0;
    if (index < 0) {
        if (!control && !shift) {
            ClearSelection();
        }
        return;
    }
    if (right && IsItemSelected(index)) {
        FocusItem(index);
        return;
    }
    if (shift) {
        int anchor = ListView_GetSelectionMark(state.listView);
        if (anchor < 0) {
            anchor = state.selectionAnchor;
        }
        if (anchor < 0 || anchor >= ListView_GetItemCount(state.listView)) {
            anchor = index;
        }
        if (!control) {
            ClearSelection();
        }
        for (int i = std::min(anchor, index); i <= std::max(anchor, index); ++i) {
            SetItemSelected(i, true);
        }
    } else if (control) {
        SetItemSelected(index, true); // toggle a selected item on mouse-up, unless dragged
    } else if (!IsItemSelected(index)) {
        ClearSelection();
        SetItemSelected(index, true);
    }
    FocusItem(index);
    if (!shift) {
        state.selectionAnchor = index;
        ListView_SetSelectionMark(state.listView, index);
    }
}

void UpdateMarquee() {
    RECT marquee = MarqueeRect();
    bool control = (state.modifiers & MK_CONTROL) != 0,
         additive = (state.modifiers & MK_SHIFT) != 0;
    for (int i = 0; i < ListView_GetItemCount(state.listView); ++i) {
        RECT item{}, intersection{};
        bool inside = GetPaintedItemRect(i, LVIR_SELECTBOUNDS, item) &&
                      IntersectRect(&intersection, &item, &marquee);
        bool before =
            i < static_cast<int>(state.selectionBefore.size()) && state.selectionBefore[i];
        SetItemSelected(i, control ? before != inside : additive ? before || inside : inside);
    }
    InvalidateRect(state.listView, nullptr, FALSE);
}

void BuildDragBitmap(int onlyItem = -1) {
    state.dragItems.clear();
    RECT bounds{};
    bool first = true;
    auto origin = ViewOrigin();
    for (int i = 0; i < ListView_GetItemCount(state.listView); ++i) {
        if ((onlyItem >= 0 ? i != onlyItem : !IsItemSelected(i)) ||
            i >= static_cast<int>(state.items.size())) {
            continue;
        }
        RECT rect{};
        if (!GetPaintedItemRect(i, LVIR_BOUNDS, rect)) {
            continue;
        }
        OffsetRect(&rect, origin.x, origin.y);
        if (first) {
            bounds = rect;
            first = false;
        } else {
            UnionRect(&bounds, &bounds, &rect);
        }
        Item item = state.items[i];
        ListView_GetItemPosition(state.listView, i, &item.position);
        state.dragItems.push_back(std::move(item));
    }
    Require(!first && bounds.right > bounds.left && bounds.bottom > bounds.top,
            "No selected drag items");
    double scale =
        std::min(1.0, 2048.0 / std::max(bounds.right - bounds.left, bounds.bottom - bounds.top));
    int width = std::max(1, int((bounds.right - bounds.left) * scale)),
        height = std::max(1, int((bounds.bottom - bounds.top) * scale));
    state.dragBitmap = std::make_unique<Gdiplus::Bitmap>(width, height, PixelFormat32bppPARGB);
    Gdiplus::Graphics graphics(state.dragBitmap.get());
    graphics.Clear(Gdiplus::Color(0, 0, 0, 0));
    graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
    HDC dc = GetDC(state.listView);
    HFONT nativeFont = reinterpret_cast<HFONT>(SendMessageW(state.listView, WM_GETFONT, 0, 0));
    Gdiplus::Font font(dc, nativeFont ? nativeFont
                                      : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)));
    Gdiplus::StringFormat format;
    format.SetAlignment(Gdiplus::StringAlignmentCenter);
    format.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
    for (int i = 0; i < ListView_GetItemCount(state.listView); ++i) {
        if ((onlyItem >= 0 ? i != onlyItem : !IsItemSelected(i)) ||
            i >= static_cast<int>(state.items.size())) {
            continue;
        }
        RECT rect{};
        if (!GetPaintedItemRect(i, LVIR_ICON, rect)) {
            continue;
        }
        OffsetRect(&rect, origin.x, origin.y);
        float x = float((rect.left - bounds.left) * scale),
              y = float((rect.top - bounds.top) * scale), size = float(state.iconSize * scale);
        std::shared_ptr<Picture> picture;
        {
            std::lock_guard<std::mutex> lock(state.mutex);
            auto found = state.pictures.find(state.items[i].key);
            if (found != state.pictures.end()) {
                picture = found->second;
            }
        }
        if (picture && picture->bitmap) {
            double imageScale =
                size / std::max(picture->bitmap->GetWidth(), picture->bitmap->GetHeight());
            float w = float(picture->bitmap->GetWidth() * imageScale),
                  h = float(picture->bitmap->GetHeight() * imageScale);
            graphics.DrawImage(picture->bitmap.get(),
                               Gdiplus::RectF(x + (size - w) / 2, y + (size - h) / 2, w, h));
        } else {
            Gdiplus::SolidBrush brush(Gdiplus::Color(140, 90, 160, 190));
            graphics.FillRectangle(&brush, x + size / 4, y + size / 4, size / 2, size / 2);
        }
        if (state.items[i].link) {
            Gdiplus::SolidBrush white(Gdiplus::Color(230, 250, 250, 250));
            graphics.FillRectangle(&white, x + float(4 * scale), y + size - float(32 * scale),
                                   float(28 * scale), float(28 * scale));
            Gdiplus::Pen arrow(Gdiplus::Color(255, 40, 85, 150), float(3 * scale));
            graphics.DrawLine(&arrow, x + float(10 * scale), y + size - float(10 * scale),
                              x + float(25 * scale), y + size - float(25 * scale));
            graphics.DrawLine(&arrow, x + float(14 * scale), y + size - float(25 * scale),
                              x + float(25 * scale), y + size - float(25 * scale));
            graphics.DrawLine(&arrow, x + float(25 * scale), y + size - float(25 * scale),
                              x + float(25 * scale), y + size - float(14 * scale));
        }
        Gdiplus::RectF label(x, y + size + float(4 * scale), size, float(48 * scale));
        Gdiplus::SolidBrush shadow(Gdiplus::Color(220, 0, 0, 0)),
            text(Gdiplus::Color(255, 255, 255, 255));
        auto shifted = label;
        shifted.X += 1;
        shifted.Y += 1;
        graphics.DrawString(state.items[i].name.c_str(), -1, &font, shifted, &format, &shadow);
        graphics.DrawString(state.items[i].name.c_str(), -1, &font, label, &format, &text);
    }
    ReleaseDC(state.listView, dc);
    state.dragHotspot =
        POINT{std::clamp(LONG((state.downView.x - bounds.left) * scale), 0L, LONG(width - 1)),
              std::clamp(LONG((state.downView.y - bounds.top) * scale), 0L, LONG(height - 1))};
}

void DrawInteraction(HDC dc) {
    Gdiplus::Graphics graphics(dc);
    if (state.marquee) {
        RECT rect = MarqueeRect();
        Gdiplus::SolidBrush fill(Gdiplus::Color(45, 40, 135, 230));
        Gdiplus::Pen edge(Gdiplus::Color(220, 80, 175, 255), 1);
        graphics.FillRectangle(&fill, int(rect.left), int(rect.top), int(rect.right - rect.left),
                               int(rect.bottom - rect.top));
        graphics.DrawRectangle(&edge, int(rect.left), int(rect.top), int(rect.right - rect.left),
                               int(rect.bottom - rect.top));
    }
    if (state.moving && state.dragBitmap) {
        auto cursor = ToClient(state.currentView);
        Gdiplus::ColorMatrix matrix = {1, 0, 0, 0, 0, 0,    1, 0, 0, 0, 0, 0, 1,
                                       0, 0, 0, 0, 0, 0.7f, 0, 0, 0, 0, 0, 1};
        Gdiplus::ImageAttributes attributes;
        attributes.SetColorMatrix(&matrix);
        int width = state.dragBitmap->GetWidth(), height = state.dragBitmap->GetHeight();
        graphics.DrawImage(state.dragBitmap.get(),
                           Gdiplus::Rect(cursor.x - state.dragHotspot.x,
                                         cursor.y - state.dragHotspot.y, width, height),
                           0, 0, width, height, Gdiplus::UnitPixel, &attributes);
    }
}

HBITMAP DragDib() {
    Require(state.dragBitmap != nullptr, "Drag bitmap unavailable");
    UINT width = state.dragBitmap->GetWidth(), height = state.dragBitmap->GetHeight();
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -LONG(height);
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP dib = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    Require(dib && bits, "Drag DIB allocation failed");
    Gdiplus::Rect rect(0, 0, width, height);
    Gdiplus::BitmapData data{};
    if (state.dragBitmap->LockBits(&rect, Gdiplus::ImageLockModeRead, PixelFormat32bppPARGB,
                                   &data) != Gdiplus::Ok) {
        DeleteObject(dib);
        throw std::runtime_error("Drag bitmap lock failed");
    }
    for (UINT y = 0; y < height; ++y) {
        memcpy(static_cast<BYTE*>(bits) + y * width * 4,
               static_cast<BYTE*>(data.Scan0) + ptrdiff_t(y) * data.Stride, width * 4);
    }
    state.dragBitmap->UnlockBits(&data);
    return dib;
}

void CommitMoves() {
    auto moves = std::move(state.pendingMoves);
    state.pendingMoves.clear();
    if (!state.active || moves.empty() || state.testHost) {
        return;
    }
    state.committing = true;
    state.positionCommitSucceeded = false;
    try {
        auto view = GetDesktopFolderView();
        Require(view != nullptr, "Desktop move view unavailable");
        auto items = ReadDesktopItems(state.listView, false);
        std::vector<PITEMID_CHILD> pidls;
        std::vector<POINT> points;
        for (const auto& move : moves) {
            auto found = std::find_if(items.begin(), items.end(), [&](const Item& item) {
                return item.key == move.key;
            });
            if (found == items.end()) {
                continue;
            }
            PITEMID_CHILD pidl = nullptr;
            if (SUCCEEDED(view->Item(static_cast<int>(found - items.begin()), &pidl))) {
                pidls.push_back(pidl);
                points.push_back(move.position);
            }
        }
        HRESULT hr = pidls.empty() ? E_FAIL
                                   : view->SelectAndPositionItems(
                                         static_cast<UINT>(pidls.size()),
                                         const_cast<PCUITEMID_CHILD*>(pidls.data()), points.data(),
                                         SVSI_POSITIONITEM | SVSI_SELECT | SVSI_NOTAKEFOCUS);
        for (auto pidl : pidls) {
            CoTaskMemFree(pidl);
        }
        Require(SUCCEEDED(hr), "Desktop position commit failed");
        state.positionCommitSucceeded = true;
        state.committing = false;
    } catch (...) {
        state.committing = false;
        SetMessage(L"移動を表示しましたが、Explorer への配置保存に失敗しました。");
    }
}

void FinishMove() {
    // A very fast button-up can precede the queued native drag handoff.
    // Auto-arrange must never fall through to free placement in that case.
    if (IsAutoArrangeEnabled()) {
        CancelInteraction();
        QueueSynchronization(true);
        return;
    }
    POINT delta{state.currentView.x - state.downView.x, state.currentView.y - state.downView.y};
    LONG minX = LONG_MAX, minY = LONG_MAX;
    for (const auto& item : state.dragItems) {
        minX = std::min(minX, item.position.x);
        minY = std::min(minY, item.position.y);
    }
    if (!state.dragItems.empty()) {
        delta.x = std::max(delta.x, -minX);
        delta.y = std::max(delta.y, -minY);
    }
    state.pendingMoves.clear();
    state.committing = true;
    for (auto item : state.dragItems) {
        auto found = std::find_if(state.items.begin(), state.items.end(), [&](const Item& current) {
            return current.key == item.key;
        });
        if (found == state.items.end()) {
            continue;
        }
        item.position.x += delta.x;
        item.position.y += delta.y;
        ListView_SetItemPosition32(state.listView, static_cast<int>(found - state.items.begin()),
                                   item.position.x, item.position.y);
        state.pendingMoves.push_back(std::move(item));
    }
    if (IsSnapToGridEnabled()) {
        ListView_Arrange(state.listView, LVA_SNAPTOGRID);
        for (auto& item : state.pendingMoves) {
            auto found =
                std::find_if(state.items.begin(), state.items.end(), [&](const Item& current) {
                    return current.key == item.key;
                });
            if (found != state.items.end()) {
                ListView_GetItemPosition(
                    state.listView, static_cast<int>(found - state.items.begin()), &item.position);
            }
        }
    }
    state.committing = false;
    state.lastMoveDelta = delta;
    state.moveCommits++;
    CancelInteraction();
    PostMessageW(state.listView, CommitMessage(), 0, 0);
    QueueSynchronization();
}

ComPtr<IDataObject> ShellDragData(DWORD& effects) {
    ComPtr<IShellItemArray> selection;
    ComPtr<IDataObject> data;
    if (state.testHost) {
        std::vector<PIDLIST_ABSOLUTE> pidls;
        for (const auto& item : state.dragItems) {
            PIDLIST_ABSOLUTE pidl = nullptr;
            if (SUCCEEDED(SHParseDisplayName(item.key.c_str(), nullptr, &pidl, 0, nullptr))) {
                pidls.push_back(pidl);
            }
        }
        HRESULT hr = SHCreateShellItemArrayFromIDLists(static_cast<UINT>(pidls.size()),
                                                       const_cast<PCIDLIST_ABSOLUTE*>(pidls.data()),
                                                       &selection);
        for (auto pidl : pidls) {
            CoTaskMemFree(pidl);
        }
        Require(SUCCEEDED(hr), "Test drag selection unavailable");
        Require(SUCCEEDED(selection->BindToHandler(nullptr, BHID_DataObject, IID_PPV_ARGS(&data))),
                "Test drag data unavailable");
    } else {
        auto folder = GetDesktopFolderView();
        Require(folder != nullptr, "Desktop drag view unavailable");
        ComPtr<IShellView> view;
        Require(SUCCEEDED(folder.As(&view)) &&
                    SUCCEEDED(view->GetItemObject(SVGIO_SELECTION, IID_PPV_ARGS(&data))) &&
                    SUCCEEDED(folder->GetSelection(FALSE, &selection)),
                "Desktop drag selection unavailable");
    }
    SFGAOF attributes = 0;
    Require(SUCCEEDED(selection->GetAttributes(
                SIATTRIBFLAGS_AND, SFGAO_CANCOPY | SFGAO_CANMOVE | SFGAO_CANLINK, &attributes)),
            "Drag capabilities unavailable");
    effects = (attributes & SFGAO_CANCOPY ? DROPEFFECT_COPY : 0) |
              (attributes & SFGAO_CANMOVE ? DROPEFFECT_MOVE : 0) |
              (attributes & SFGAO_CANLINK ? DROPEFFECT_LINK : 0);
    // Even virtual desktop icons can be repositioned within this view.
    effects |= DROPEFFECT_MOVE;
    return data;
}

ComPtr<IDragSourceHelper2> InitializeShellDragImage(IDataObject* data) {
    ComPtr<IDragSourceHelper2> helper;
    Require(SUCCEEDED(CoCreateInstance(CLSID_DragDropHelper, nullptr, CLSCTX_INPROC_SERVER,
                                       IID_PPV_ARGS(&helper))),
            "Shell drag image helper unavailable");
    // Supplying a custom bitmap otherwise disables Explorer's operation text.
    Require(SUCCEEDED(helper->SetFlags(DSH_ALLOWDROPDESCRIPTIONTEXT)),
            "Shell drop descriptions unavailable");
    auto anchor = ToClient(state.downView);
    Require(SUCCEEDED(helper->InitializeFromWindow(state.listView, &anchor, data)),
            "Shell drag image initialization failed");
    // InitializeFromWindow/Bitmap can clear this flag for custom bitmaps, even
    // with DSH_ALLOWDROPDESCRIPTIONTEXT. Windows Forms enables it for the same reason.
    FORMATETC format{static_cast<CLIPFORMAT>(RegisterClipboardFormatW(L"IsShowingText")), nullptr,
                     DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
    STGMEDIUM medium{};
    medium.tymed = TYMED_HGLOBAL;
    medium.hGlobal = GlobalAlloc(GMEM_MOVEABLE, sizeof(BOOL));
    auto value = static_cast<BOOL*>(GlobalLock(medium.hGlobal));
    if (!value) {
        ReleaseStgMedium(&medium);
        throw std::runtime_error("Shell drop text flag allocation failed");
    }
    *value = TRUE;
    GlobalUnlock(medium.hGlobal);
    HRESULT hr = data->SetData(&format, &medium, TRUE);
    if (FAILED(hr)) {
        ReleaseStgMedium(&medium);
        throw std::runtime_error("Shell drop text could not be enabled");
    }
    return helper;
}

bool IsInternalMoveTarget(POINT screen, DWORD keys) {
    if (state.rightButton || (keys & MK_CONTROL) || GetKeyState(VK_MENU) < 0 ||
        WindowFromPoint(screen) != state.listView) {
        return false;
    }
    ScreenToClient(state.listView, &screen);
    int target = HitTestPaintedItem(screen);
    return target < 0 || IsDragSourceItem(target);
}

class DesktopDropSource final : public IDropSource {
    ULONG references = 1;
    ComPtr<IDataObject> data;
    bool ownWindowOnly = false;

  public:
    bool internalMove = false;

    explicit DesktopDropSource(IDataObject* object = nullptr, bool hostOnly = false)
        : data(object), ownWindowOnly(hostOnly) {
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) {
            return E_POINTER;
        }
        *object = nullptr;
        if (iid != IID_IUnknown && iid != IID_IDropSource) {
            return E_NOINTERFACE;
        }
        *object = static_cast<IDropSource*>(this);
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

    HRESULT STDMETHODCALLTYPE QueryContinueDrag(BOOL escape, DWORD keys) override {
        if (escape || !state.active) {
            return DRAGDROP_S_CANCEL;
        }
        if (ownWindowOnly) {
            POINT pointer{};
            if (!GetCursorPos(&pointer) || WindowFromPoint(pointer) != state.listView) {
                // Test items represent Windows directories; never offer them to
                // another application during the UI-only feedback preview.
                return DRAGDROP_S_CANCEL;
            }
        }
        if (keys & (state.rightButton ? MK_LBUTTON : MK_RBUTTON)) {
            return DRAGDROP_S_CANCEL;
        }
        if (keys & (state.rightButton ? MK_RBUTTON : MK_LBUTTON)) {
            return S_OK;
        }
        POINT screen{};
        if (GetCursorPos(&screen) && IsInternalMoveTarget(screen, keys)) {
            ScreenToClient(state.listView, &screen);
            state.currentView = ToView(screen);
            internalMove = true;
            // Keep the existing exact-offset/grid-aware positioning after leaving
            // a folder target and returning to empty space in this desktop.
            return DRAGDROP_S_CANCEL;
        }
        return DRAGDROP_S_DROP;
    }

    HRESULT STDMETHODCALLTYPE GiveFeedback(DWORD) override {
        UpdateDropImage(data.Get());
        return DRAGDROP_S_USEDEFAULTCURSORS;
    }
};

void NativeDrag() {
    if (!state.pressed || !state.active) {
        return;
    }
    if (!state.dragBitmap) {
        BuildDragBitmap();
    }
    DWORD effects = 0;
    auto data = ShellDragData(effects);
    auto helper = InitializeShellDragImage(data.Get());
    ComPtr<DesktopDropSource> source;
    source.Attach(new DesktopDropSource(data.Get(), state.testHost));
    state.externalDrag = true;
    CancelInteraction();
    state.nativeDrags++;
    DWORD performed = DROPEFFECT_NONE;
    HRESULT hr = SHDoDragDrop(state.listView, data.Get(), source.Get(), effects, &performed);
    if (source->internalMove && state.active) {
        FinishMove();
    }
    state.externalDrag = false;
    state.dragBitmap.reset();
    state.dragItems.clear();
    ListView_SetItemState(state.listView, -1, 0, LVIS_DROPHILITED);
    QueueSynchronization();
    InvalidateRect(state.listView, nullptr, FALSE);
    Require(SUCCEEDED(hr), "Shell drag failed");
}

void CaptureDrag(const std::wstring& path) {
    Require(state.active && !path.empty(), "Drag capture needs an active patch and output path");
    if (!state.dragBitmap) {
        BuildDragBitmap();
    }
    auto png = PngEncoder();
    Require(state.dragBitmap->Save(path.c_str(), &png, nullptr) == Gdiplus::Ok,
            "Drag capture failed");
}

std::wstring QuoteInteraction(const std::wstring& value) {
    std::wstring result = L"\"";
    for (wchar_t c : value) {
        if (c == L'\\' || c == L'\"') {
            result += L'\\';
        }
        result += c < 32 ? L' ' : c;
    }
    return result + L"\"";
}

#include "ContextMenu.h"

void InteractionReport(const std::wstring& path) {
    Require(state.active && !path.empty(), "Interaction report needs an active patch");
    PollContextMenu();
    auto rect = MarqueeRect();
    auto origin = ViewOrigin();
    auto items = ReadDesktopItems(state.listView, state.testHost);
    std::wstring json =
        L"{\"pressed\":" + std::to_wstring(state.pressed) + L",\"moving\":" +
        std::to_wstring(state.moving) + L",\"marquee\":" + std::to_wstring(state.marquee) +
        L",\"rectangle\":[" + std::to_wstring(rect.left) + L"," + std::to_wstring(rect.top) + L"," +
        std::to_wstring(rect.right) + L"," + std::to_wstring(rect.bottom) + L"],\"origin\":[" +
        std::to_wstring(origin.x) + L"," + std::to_wstring(origin.y) + L"],\"dragWidth\":" +
        std::to_wstring(state.dragBitmap ? state.dragBitmap->GetWidth() : 0) + L",\"dragHeight\":" +
        std::to_wstring(state.dragBitmap ? state.dragBitmap->GetHeight() : 0) + L",\"hotspot\":[" +
        std::to_wstring(state.dragHotspot.x) + L"," + std::to_wstring(state.dragHotspot.y) +
        L"],\"moveCommits\":" + std::to_wstring(state.moveCommits) + L",\"nativeDrags\":" +
        std::to_wstring(state.nativeDrags) + L",\"positionCommitSucceeded\":" +
        std::to_wstring(state.positionCommitSucceeded) + L",\"autoArrange\":" +
        std::to_wstring(IsAutoArrangeEnabled()) + L",\"snapToGrid\":" +
        std::to_wstring(IsSnapToGridEnabled()) + L",\"contextRequests\":" +
        std::to_wstring(state.contextRequests) + L",\"contextMenus\":" +
        std::to_wstring(state.contextMenus) + L",\"items\":[";
    for (size_t i = 0; i < items.size(); ++i) {
        RECT bounds{};
        GetPaintedItemRect(static_cast<int>(i), LVIR_BOUNDS, bounds);
        if (i) {
            json += L",";
        }
        json += L"{\"key\":" + QuoteInteraction(items[i].key) + L",\"selected\":" +
                std::to_wstring(IsItemSelected(static_cast<int>(i))) + L",\"x\":" +
                std::to_wstring(items[i].position.x) + L",\"y\":" +
                std::to_wstring(items[i].position.y) + L",\"bounds\":[" +
                std::to_wstring(bounds.left) + L"," + std::to_wstring(bounds.top) + L"," +
                std::to_wstring(bounds.right) + L"," + std::to_wstring(bounds.bottom) + L"]}";
    }
    WriteUtf16(path, json + L"],\"contextStyle\":" + QuoteInteraction(state.menuStyle) +
                         L",\"contextItem\":" + std::to_wstring(state.contextItem) +
                         L",\"contextPoint\":[" + std::to_wstring(state.contextPoint.x) + L"," +
                         std::to_wstring(state.contextPoint.y) + L"]}");
}

bool HandleInteraction(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam, LRESULT& result) {
    if (message == ContextMessage()) {
        ProcessContext();
        return true;
    }
    if (message == WM_CONTEXTMENU) {
        if (state.contextActive) {
            result = DefSubclassProc(hwnd, message, wParam,
                                     MAKELPARAM(state.contextPoint.x, state.contextPoint.y));
        } else {
            QueueContext(POINT{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)});
        }
        return true;
    }
    if (message == WM_TIMER && wParam == MenuTestTimer && state.menuTest) {
        DismissTestMenu();
        return true;
    }
    if (message == CommitMessage()) {
        CommitMoves();
        return true;
    }
    if (message == NativeDragMessage()) {
        if (!state.nativeDragPending || wParam != state.gesture) {
            return true;
        }
        state.nativeDragPending = false;
        try {
            NativeDrag();
        } catch (...) {
            state.externalDrag = false;
            CancelInteraction();
        }
        return true;
    }
    if (message == DragImageMessage() && lParam) {
        try {
            if (!state.dragBitmap) {
                BuildDragBitmap();
            }
            auto drag = reinterpret_cast<SHDRAGIMAGE*>(lParam);
            drag->hbmpDragImage = DragDib();
            drag->sizeDragImage =
                SIZE{LONG(state.dragBitmap->GetWidth()), LONG(state.dragBitmap->GetHeight())};
            drag->ptOffset = state.dragHotspot;
            drag->crColorKey = CLR_NONE;
            result = TRUE;
        } catch (...) {
            result = FALSE;
        }
        return true;
    }
    if (message == LVM_CREATEDRAGIMAGE) {
        struct Snapshot {
            std::unique_ptr<Gdiplus::Bitmap> bitmap = std::move(state.dragBitmap);
            std::vector<Item> items = std::move(state.dragItems);
            POINT hotspot = state.dragHotspot;

            ~Snapshot() {
                state.dragBitmap = std::move(bitmap);
                state.dragItems = std::move(items);
                state.dragHotspot = hotspot;
            }
        } snapshot;

        try {
            BuildDragBitmap(static_cast<int>(wParam));
            auto bitmap = DragDib();
            HIMAGELIST images = ImageList_Create(state.dragBitmap->GetWidth(),
                                                 state.dragBitmap->GetHeight(), ILC_COLOR32, 1, 1);
            if (images && ImageList_Add(images, bitmap, nullptr) < 0) {
                ImageList_Destroy(images);
                images = nullptr;
            }
            DeleteObject(bitmap);
            if (lParam) {
                RECT rect{};
                GetPaintedItemRect(static_cast<int>(wParam), LVIR_BOUNDS, rect);
                *reinterpret_cast<POINT*>(lParam) = ToView(POINT{rect.left, rect.top});
            }
            result = reinterpret_cast<LRESULT>(images);
        } catch (...) {
            result = 0;
        }
        return true;
    }
    if (message == WM_CANCELMODE ||
        (message == WM_CAPTURECHANGED && state.pressed && reinterpret_cast<HWND>(lParam) != hwnd)) {
        CancelInteraction();
        QueueSynchronization();
        return false;
    }
    if (message == WM_KEYDOWN && wParam == VK_ESCAPE && state.pressed) {
        if (state.marquee) {
            for (int i = 0; i < static_cast<int>(state.selectionBefore.size()); ++i) {
                SetItemSelected(i, state.selectionBefore[i]);
            }
        }
        CancelInteraction();
        QueueSynchronization();
        return true;
    }
    if (message == WM_LBUTTONDBLCLK) {
        CancelInteraction();
        POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        int index = HitTestPaintedItem(point);
        RECT rect{};
        if (index >= 0 && GetNativeIconRect(index, rect)) {
            ClickSelection(index, wParam, message == WM_RBUTTONDBLCLK);
            LPARAM nativePoint =
                MAKELPARAM((rect.left + rect.right) / 2, (rect.top + rect.bottom) / 2);
            result = DefSubclassProc(hwnd, message, wParam, nativePoint);
            DefSubclassProc(hwnd, message == WM_LBUTTONDBLCLK ? WM_LBUTTONUP : WM_RBUTTONUP, 0,
                            nativePoint);
        }
        return true;
    }
    if (message == WM_LBUTTONDOWN || message == WM_RBUTTONDOWN || message == WM_RBUTTONDBLCLK) {
        CancelInteraction();
        ++state.gesture;
        SetFocus(hwnd);
        state.rightButton = message != WM_LBUTTONDOWN;
        POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        state.downView = state.currentView = ToView(point);
        state.pressedItem = HitTestPaintedItem(point);
        state.modifiers = wParam & (MK_CONTROL | MK_SHIFT);
        state.selectionBefore.clear();
        for (int i = 0; i < ListView_GetItemCount(hwnd); ++i) {
            state.selectionBefore.push_back(IsItemSelected(i));
        }
        state.pressedSelected = state.pressedItem >= 0 && IsItemSelected(state.pressedItem);
        ClickSelection(state.pressedItem, state.modifiers, state.rightButton);
        state.pressed = true;
        SetCapture(hwnd);
        InvalidateRect(hwnd, nullptr, FALSE);
        return true;
    }
    if (message == WM_MOUSEMOVE && state.pressed) {
        POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        state.currentView = ToView(point);
        if (!state.moving && !state.marquee &&
            (abs(state.currentView.x - state.downView.x) >= GetSystemMetrics(SM_CXDRAG) ||
             abs(state.currentView.y - state.downView.y) >= GetSystemMetrics(SM_CYDRAG))) {
            if (state.pressedItem < 0) {
                state.marquee = true;
            } else {
                try {
                    BuildDragBitmap();
                    state.moving = true;
                } catch (...) {
                    CancelInteraction();
                    return true;
                }
            }
        }
        if (state.marquee) {
            UpdateMarquee();
        }
        if (state.moving) {
            if (!state.nativeDragPending) {
                state.nativeDragPending =
                    PostMessageW(hwnd, NativeDragMessage(), state.gesture, 0) != FALSE;
            }
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return true;
    }
    if ((message == WM_LBUTTONUP || message == WM_RBUTTONUP) && state.pressed) {
        POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        state.currentView = ToView(point);
        if (state.moving) {
            FinishMove();
        } else if (state.marquee) {
            UpdateMarquee();
            CancelInteraction();
            QueueSynchronization();
        } else {
            int index = state.pressedItem;
            bool right = state.rightButton;
            if (index >= 0 && !right) {
                if ((state.modifiers & MK_CONTROL) && !(state.modifiers & MK_SHIFT) &&
                    state.pressedSelected) {
                    SetItemSelected(index, false);
                } else if (!state.modifiers) {
                    ClearSelection();
                    SetItemSelected(index, true);
                }
            }
            CancelInteraction();
            if (right) {
                ClientToScreen(hwnd, &point);
                QueueContext(point);
                QueueSynchronization();
                return true;
            }
            NMITEMACTIVATE notification{};
            notification.hdr.hwndFrom = hwnd;
            notification.hdr.idFrom = GetDlgCtrlID(hwnd);
            notification.hdr.code = right ? NM_RCLICK : NM_CLICK;
            notification.uKeyFlags = (state.modifiers & MK_CONTROL ? LVKF_CONTROL : 0) |
                                     (state.modifiers & MK_SHIFT ? LVKF_SHIFT : 0);
            notification.iItem = index;
            notification.ptAction = point;
            SendMessageW(state.shellViewWindow, WM_NOTIFY, notification.hdr.idFrom,
                         reinterpret_cast<LPARAM>(&notification));
            QueueSynchronization();
        }
        return true;
    }
    return false;
}
