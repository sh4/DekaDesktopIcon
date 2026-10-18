// SPDX-License-Identifier: MIT
// Copyright (c) 2026 sh4
// Included by the core inside its implementation namespace, after painted hit testing.
HWND DropImageWindow(IDataObject* data) {
    if (!data) {
        return nullptr;
    }
    FORMATETC format{static_cast<CLIPFORMAT>(RegisterClipboardFormatW(L"DragWindow")), nullptr,
                     DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
    STGMEDIUM medium{};
    if (FAILED(data->GetData(&format, &medium))) {
        return nullptr;
    }
    HWND window = nullptr;
    auto value = static_cast<DWORD*>(GlobalLock(medium.hGlobal));
    if (value && GlobalSize(medium.hGlobal) >= sizeof(DWORD)) {
        // DragWindow carries a DWORD handle even on 64-bit Windows.
        window = static_cast<HWND>(ULongToHandle(*value));
    }
    if (value) {
        GlobalUnlock(medium.hGlobal);
    }
    ReleaseStgMedium(&medium);
    return window;
}

bool UpdateDropImage(IDataObject* data) {
    HWND window = DropImageWindow(data);
    // DDWM_UPDATEWINDOW: a DROPDESCRIPTION change alone does not repaint the image.
    return IsWindow(window) && PostMessageW(window, WM_USER + 3, 0, 0);
}

int HitTestNativeItem(POINT point) {
    bool previous = state.baseGeometry;
    state.baseGeometry = true;
    LVHITTESTINFO hit{};
    hit.pt = point;
    int index = ListView_HitTest(state.listView, &hit);
    state.baseGeometry = previous;
    return index;
}

bool IsDragSourceItem(int index) {
    if (!state.externalDrag || index < 0 || index >= static_cast<int>(state.items.size())) {
        return false;
    }
    const auto& key = state.items[index].key;
    return std::any_of(state.dragItems.begin(), state.dragItems.end(), [&](const Item& item) {
        return item.key == key;
    });
}

POINTL NativeDropPoint(POINTL screen) {
    if (!state.active) {
        return screen;
    }
    POINT client{screen.x, screen.y};
    ScreenToClient(state.listView, &client);
    int target = HitTestPaintedItem(client);
    if (target >= 0 && !IsDragSourceItem(target)) {
        RECT native{};
        if (GetNativeIconRect(target, native)) {
            RECT viewport{}, visible{};
            GetClientRect(state.listView, &viewport);
            if (IntersectRect(&visible, &native, &viewport)) {
                // A scrolled or partly clipped item can have its native center
                // outside the client area even while its painted icon is visible.
                native = visible;
            }
            POINT mapped{(native.left + native.right) / 2, (native.top + native.bottom) / 2};
            bool found = HitTestNativeItem(mapped) == target;
            // The native icon/label can exceed the patched spacing and overlap a
            // different item. Its rectangle's center is then not a reliable hit.
            for (LONG y = native.top + 1; !found && y < native.bottom; y += 8) {
                mapped = POINT{(native.left + native.right) / 2, y};
                found = HitTestNativeItem(mapped) == target;
            }
            for (LONG y = native.top + 1; !found && y < native.bottom; y += 8) {
                for (LONG x = native.left + 1; x < native.right; x += 8) {
                    mapped = POINT{x, y};
                    if (HitTestNativeItem(mapped) == target) {
                        found = true;
                        break;
                    }
                }
            }
            if (found) {
                ClientToScreen(state.listView, &mapped);
                return POINTL{mapped.x, mapped.y};
            }
        }
    }
    if (HitTestNativeItem(client) >= 0) {
        // Source items are background during a drag originating in this view.
        // Native labels can also extend beyond the painted bounds at small sizes.
        // Neither may become a folder target, including on the final Drop call.
        RECT bounds{};
        GetClientRect(state.listView, &bounds);
        POINT empty{bounds.right / 2, bounds.bottom / 2};
        bool found = HitTestNativeItem(empty) < 0;
        for (LONG y = 32; !found && y < bounds.bottom - 32; y += 32) {
            for (LONG x = 32; x < bounds.right - 32; x += 32) {
                empty = POINT{x, y};
                if (HitTestNativeItem(empty) < 0) {
                    found = true;
                    break;
                }
            }
        }
        if (!found) {
            // A densely filled viewport may have no native background point.
            // Outside its client area the native hit test cannot select an item.
            empty = POINT{-1, -1};
        }
        ClientToScreen(state.listView, &empty);
        return POINTL{empty.x, empty.y};
    }
    return screen;
}

class PaintedDropTarget final : public IDropTarget {
    ULONG references = 1;
    ComPtr<IDropTarget> native;
    ComPtr<IDropTargetHelper> images;
    ComPtr<IDataObject> activeData;
    bool entered = false;

  public:
    explicit PaintedDropTarget(IDropTarget* original) : native(original) {
        Require(SUCCEEDED(CoCreateInstance(CLSID_DragDropHelper, nullptr, CLSCTX_INPROC_SERVER,
                                           IID_PPV_ARGS(&images))),
                "Shell drop image helper unavailable");
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) {
            return E_POINTER;
        }
        *object = nullptr;
        if (iid != IID_IUnknown && iid != IID_IDropTarget) {
            return E_NOINTERFACE;
        }
        *object = static_cast<IDropTarget*>(this);
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
        if (!effect) {
            return E_POINTER;
        }
        activeData = data;
        HRESULT hr = native->DragEnter(data, keys, NativeDropPoint(screen), effect);
        entered = SUCCEEDED(hr);
        if (images && SUCCEEDED(hr)) {
            POINT point{screen.x, screen.y};
            images->DragEnter(state.listView, data, &point, *effect);
        }
        UpdateDropImage(data);
        InvalidateRect(state.listView, nullptr, FALSE);
        return hr;
    }

    HRESULT STDMETHODCALLTYPE DragOver(DWORD keys, POINTL screen, DWORD* effect) override {
        if (!effect) {
            return E_POINTER;
        }
        HRESULT hr = native->DragOver(keys, NativeDropPoint(screen), effect);
        if (images && SUCCEEDED(hr)) {
            // The Shell hit test uses the mapped point; its image and operation
            // description must continue to follow the real pointer.
            POINT point{screen.x, screen.y};
            images->DragOver(&point, *effect);
        }
        UpdateDropImage(activeData.Get());
        InvalidateRect(state.listView, nullptr, FALSE);
        return hr;
    }

    HRESULT STDMETHODCALLTYPE DragLeave() override {
        if (!entered) {
            return S_OK;
        }
        entered = false;
        HRESULT hr = native->DragLeave();
        if (images) {
            images->DragLeave();
        }
        activeData.Reset();
        ListView_SetItemState(state.listView, -1, 0, LVIS_DROPHILITED);
        InvalidateRect(state.listView, nullptr, FALSE);
        return hr;
    }

    HRESULT STDMETHODCALLTYPE Drop(IDataObject* data, DWORD keys, POINTL screen,
                                   DWORD* effect) override {
        if (!effect) {
            return E_POINTER;
        }
        entered = false;
        HRESULT hr = native->Drop(data, keys, NativeDropPoint(screen), effect);
        if (images) {
            POINT point{screen.x, screen.y};
            images->Drop(data, &point, *effect);
        }
        activeData.Reset();
        ListView_SetItemState(state.listView, -1, 0, LVIS_DROPHILITED);
        QueueSynchronization();
        InvalidateRect(state.listView, nullptr, FALSE);
        return hr;
    }
};

void InstallDropTarget() {
    if (state.testHost) {
        state.originalDropTarget.Attach(reinterpret_cast<IDropTarget*>(
            SendMessageW(state.shellViewWindow, TestDropTargetMessage, 0, 0)));
        if (!state.originalDropTarget) {
            return;
        }
    } else {
        auto folder = GetDesktopFolderView();
        ComPtr<IShellView> view;
        Require(folder && SUCCEEDED(folder.As(&view)) &&
                    SUCCEEDED(view.As(&state.originalDropTarget)),
                "Desktop drop target unavailable");
    }
    ComPtr<IDropTarget> proxy;
    proxy.Attach(new PaintedDropTarget(state.originalDropTarget.Get()));
    HRESULT hr = RevokeDragDrop(state.listView);
    Require(SUCCEEDED(hr) || hr == DRAGDROP_E_NOTREGISTERED, "Cannot replace desktop drop target");
    state.originalDropRegistered = SUCCEEDED(hr);
    state.dropTarget = std::move(proxy);
    // Apply's rollback also restores the native registration if this step fails.
    Require(SUCCEEDED(RegisterDragDrop(state.listView, state.dropTarget.Get())),
            "Cannot register painted desktop drop target");
}

void RemoveDropTarget(bool restoreNative) {
    if (state.dropTarget) {
        state.dropTarget->DragLeave();
        RevokeDragDrop(state.listView);
        if (restoreNative && state.originalDropRegistered && IsWindow(state.listView)) {
            if (FAILED(RegisterDragDrop(state.listView, state.originalDropTarget.Get())) &&
                shared) {
                shared->error = ERROR_FUNCTION_FAILED;
                SetMessage(L"描画を解除しましたが、Explorer のドロップ先の復元に失敗しました。");
            }
        }
    }
    state.dropTarget.Reset();
    state.originalDropTarget.Reset();
    state.originalDropRegistered = false;
}
