// SPDX-License-Identifier: MIT
// Copyright (c) 2026 sh4
// A UI-only mock target: actual OLE input and Shell images, no filesystem drops.
bool dragPreviewHost = false;
ComPtr<IDropTarget> previewTarget;

class PreviewDropTarget final : public IDropTarget {
    ULONG references = 1;
    ComPtr<IDataObject> data;

  public:
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

    HRESULT STDMETHODCALLTYPE DragEnter(IDataObject* object, DWORD keys, POINTL screen,
                                        DWORD* effect) override {
        data = object;
        return DragOver(keys, screen, effect);
    }

    HRESULT STDMETHODCALLTYPE DragOver(DWORD keys, POINTL screen, DWORD* effect) override {
        POINT point{screen.x, screen.y};
        ScreenToClient(list, &point);
        LVHITTESTINFO hit{};
        hit.pt = point;
        int index = ListView_HitTest(list, &hit);
        ListView_SetItemState(list, -1, 0, LVIS_DROPHILITED);
        if (index >= 0) {
            ListView_SetItemState(list, index, LVIS_DROPHILITED, LVIS_DROPHILITED);
        }
        *effect = (keys & MK_CONTROL) ? DROPEFFECT_COPY : DROPEFFECT_MOVE;
        DROPDESCRIPTION description{};
        description.type = (keys & MK_CONTROL) ? DROPIMAGE_COPY : DROPIMAGE_MOVE;
        wcscpy_s(description.szMessage, (keys & MK_CONTROL) ? L"%1 にコピー" : L"%1 に移動");
        if (index >= 0) {
            ListView_GetItemText(list, index, 0, description.szInsert, MAX_PATH);
        } else {
            wcscpy_s(description.szInsert, L"テスト背景");
        }
        FORMATETC format{static_cast<CLIPFORMAT>(RegisterClipboardFormatW(CFSTR_DROPDESCRIPTION)),
                         nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
        STGMEDIUM medium{};
        medium.tymed = TYMED_HGLOBAL;
        medium.hGlobal = GlobalAlloc(GMEM_MOVEABLE, sizeof(description));
        auto memory = GlobalLock(medium.hGlobal);
        if (!memory) {
            ReleaseStgMedium(&medium);
            return E_OUTOFMEMORY;
        }
        memcpy(memory, &description, sizeof(description));
        GlobalUnlock(medium.hGlobal);
        HRESULT hr = data->SetData(&format, &medium, TRUE);
        if (FAILED(hr)) {
            ReleaseStgMedium(&medium);
        }
        return hr;
    }

    HRESULT STDMETHODCALLTYPE DragLeave() override {
        ListView_SetItemState(list, -1, 0, LVIS_DROPHILITED);
        data.Reset();
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Drop(IDataObject*, DWORD, POINTL, DWORD* effect) override {
        HWND parent = GetParent(list);
        SetWindowTextW(parent, L"DekaDesktopIcon TestHost — Drop preview (Escape to finish)");
        constexpr UINT_PTR previewTimer = 0x44444915;
        SetTimer(parent, previewTimer, 30000, nullptr);
        MSG message{};
        int received = 1;
        while ((received = GetMessageW(&message, nullptr, 0, 0)) > 0) {
            if ((message.message == WM_KEYDOWN && message.wParam == VK_ESCAPE) ||
                (message.hwnd == parent && message.message == WM_TIMER &&
                 message.wParam == previewTimer)) {
                break;
            }
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        if (!received) {
            PostQuitMessage(static_cast<int>(message.wParam));
        }
        KillTimer(parent, previewTimer);
        *effect = DROPEFFECT_NONE;
        DragLeave();
        SetWindowTextW(parent, L"DekaDesktopIcon TestHost");
        return S_OK;
    }
};

void EnableDragPreview() {
    if (previewTarget || FAILED(OleInitialize(nullptr))) {
        return;
    }
    dragPreviewHost = true;
    previewTarget.Attach(new PreviewDropTarget);
    if (FAILED(RegisterDragDrop(list, previewTarget.Get()))) {
        previewTarget.Reset();
        OleUninitialize();
        return;
    }
    SetWindowTheme(list, L"Explorer", nullptr);
    SetWindowTextW(GetParent(list), L"DekaDesktopIcon TestHost — Drag feedback preview");
}
