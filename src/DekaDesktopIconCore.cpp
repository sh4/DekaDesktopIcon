// SPDX-License-Identifier: MIT
// Copyright (c) 2026 sh4
#include "Imaging.h"
#include <atomic>
#include <mutex>
#include <windowsx.h>
#include <shellapi.h>

using namespace deka;

namespace {
HMODULE moduleHandle = nullptr;
HMODULE callbackReference = nullptr;
HANDLE mapping = nullptr;
SharedCommand* shared = nullptr;

struct State {
    // The shell owns both windows. All mutations run on their UI thread.
    HWND listView = nullptr;
    HWND shellViewWindow = nullptr;
    bool active = false;
    bool testHost = false;
    bool refreshing = false;
    bool baseGeometry = false;
    bool painting = false;
    bool syncPending = false;
    bool layoutPending = false;
    bool snapPending = false;

    // Context requests are queued until the pointer capture has been released.
    bool contextPending = false;
    bool contextActive = false;
    bool menuTest = false;
    bool menuSeen = false;
    POINT contextPoint{};
    UINT64 contextRequests = 0;
    UINT64 contextMenus = 0;
    HWND contextPopup = nullptr;
    UINT64 contextTick = 0;
    bool menuDismissed = false;
    bool contextItem = false;
    std::wstring menuStyle;
    std::vector<std::wstring> menuTitles;
    // Pointer positions stay in view coordinates until they are drawn.
    bool pressed = false;
    bool moving = false;
    bool marquee = false;
    bool externalDrag = false;
    bool committing = false;
    bool rightButton = false;
    bool positionCommitSucceeded = true;
    int pressedItem = -1;
    int selectionAnchor = -1;
    bool pressedSelected = false;
    WPARAM modifiers = 0;
    POINT downView{};
    POINT currentView{};
    std::vector<bool> selectionBefore;
    std::vector<Item> dragItems;
    std::vector<Item> pendingMoves;
    std::unique_ptr<Gdiplus::Bitmap> dragBitmap;
    POINT dragHotspot{};
    UINT64 moveCommits = 0;
    UINT64 nativeDrags = 0;
    POINT lastMoveDelta{};
    // Original shell resources remain shell-owned and are restored on removal.
    int iconSize = 1024;
    LONG_PTR originalStyle = 0;
    DWORD originalExtendedStyle = 0;
    DWORD originalView = 0;
    DWORD originalSpacing = 0;
    HIMAGELIST originalImages = nullptr;
    HIMAGELIST customImages = nullptr;
    std::wstring profile;
    std::vector<Item> originalItems;
    std::vector<Item> items;

    // The image worker publishes immutable pictures under this mutex.
    std::map<std::wstring, std::shared_ptr<Picture>> pictures;
    std::mutex mutex;
    std::atomic<ULONG> generation{0};
    ULONG_PTR gdiplus = 0;
    UINT64 deadline = 0;
} state;

void CancelInteraction();
void SnapItems();

bool IsAutoArrangeEnabled() {
    return (GetWindowLongPtrW(state.listView, GWL_STYLE) & LVS_AUTOARRANGE) != 0;
}

bool IsSnapToGridEnabled() {
    return (ListView_GetExtendedListViewStyle(state.listView) & LVS_EX_SNAPTOGRID) != 0;
}

UINT SyncMessage() {
    return RegisterWindowMessageW(L"DekaDesktopIcon.Sync.v1.20261017");
}

void QueueSynchronization(bool layout = false) {
    if (!state.active || state.refreshing) {
        return;
    }
    state.layoutPending = state.layoutPending || layout;
    if (!state.syncPending) {
        state.syncPending = PostMessageW(state.listView, SyncMessage(), 0, 0) != FALSE;
    }
}

void SetMessage(const wchar_t* message) {
    if (shared) {
        wcsncpy_s(shared->message, message, _TRUNCATE);
    }
}

void EnsureImageList() {
    if (!state.active || !IsWindow(state.listView)) {
        return;
    }
    bool refreshing = state.refreshing;
    state.refreshing = true;
    try {
        int width = 0, height = 0;
        Require(ImageList_GetIconSize(state.customImages, &width, &height) != FALSE,
                "Custom image list unavailable");
        bool resized = width != state.iconSize || height != state.iconSize;
        if (resized) {
            // Explorer can resize the attached list in place, without LVM_SETIMAGELIST.
            Require(ImageList_SetIconSize(state.customImages, state.iconSize, state.iconSize) !=
                            FALSE &&
                        ImageList_SetImageCount(state.customImages, 1) != FALSE,
                    "Cannot restore custom image dimensions");
        }
        auto current = ListView_GetImageList(state.listView, LVSIL_NORMAL);
        if (current != state.customImages) {
            state.originalImages = current;
        }
        if (resized || current != state.customImages) {
            ListView_SetImageList(state.listView, state.customImages, LVSIL_NORMAL);
        }
        state.refreshing = refreshing;
    } catch (...) {
        state.refreshing = refreshing;
        throw;
    }
}

void Status() {
    if (!shared) {
        return;
    }
    EnsureImageList();
    shared->active = state.active;
    shared->size = state.active ? state.iconSize : 0;
    shared->subclassInstalled = state.active;
    shared->leaseDeadline = state.deadline;
    shared->itemCount = state.listView && IsWindow(state.listView)
                            ? static_cast<DWORD>(ListView_GetItemCount(state.listView))
                            : 0;
    shared->selectedCount =
        state.listView && IsWindow(state.listView) ? ListView_GetSelectedCount(state.listView) : 0;
    int w = 0, h = 0;
    if (state.listView && IsWindow(state.listView)) {
        ImageList_GetIconSize(ListView_GetImageList(state.listView, LVSIL_NORMAL), &w, &h);
    }
    shared->imageWidth = w;
    shared->imageHeight = h;
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        shared->cachedCount = static_cast<DWORD>(state.pictures.size());
    }
}

void ReadOverrides(std::vector<Item>& items) {
    UINT count = GetPrivateProfileIntW(L"Images", L"Count", 0, state.profile.c_str());
    Require(count <= MaxItems, "Too many image overrides");
    std::map<std::wstring, std::wstring> overrides;
    for (UINT i = 0; i < count; ++i) {
        auto section = L"Image" + std::to_wstring(i);
        auto key = IniString(state.profile, section, L"Target"),
             image = IniString(state.profile, section, L"Path");
        if (!key.empty() && !image.empty()) {
            overrides.emplace(std::move(key), std::move(image));
        }
    }
    for (auto& item : items) {
        auto found = overrides.find(item.key);
        if (found != overrides.end()) {
            item.overrideImage = found->second;
        }
    }
}

struct Work {
    ULONG generation;
    HWND list;
    std::vector<Item> items;
};

DWORD WINAPI ImageWorker(void* argument) {
    std::unique_ptr<Work> work(static_cast<Work*>(argument));
    HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    for (const auto& item : work->items) {
        if (state.generation.load() != work->generation) {
            break;
        }
        std::shared_ptr<Picture> picture;
        try {
            picture = LoadPicture(item);
        } catch (...) {
            try {
                auto fallback = item;
                fallback.overrideImage.clear();
                picture = LoadPicture(fallback);
            } catch (...) {
            }
        }
        if (state.generation.load() != work->generation) {
            break;
        }
        if (picture) {
            std::lock_guard<std::mutex> lock(state.mutex);
            if (state.generation.load() == work->generation) {
                state.pictures[item.key] = std::move(picture);
            }
        }
        if (IsWindow(work->list)) {
            InvalidateRect(work->list, nullptr, FALSE);
        }
    }
    if (SUCCEEDED(com)) {
        CoUninitialize();
    }
    return 0;
}

bool RefreshItems(bool force = false) {
    auto items = ReadDesktopItems(state.listView, state.testHost);
    bool same = !force && items.size() == state.items.size();
    if (same) {
        for (size_t i = 0; i < items.size(); ++i) {
            if (items[i].key != state.items[i].key || items[i].name != state.items[i].name) {
                same = false;
                break;
            }
        }
    }
    if (same) {
        return false;
    }
    ReadOverrides(items);
    state.items = std::move(items);
    ULONG generation = ++state.generation;
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        state.pictures.clear();
    }
    auto work = std::make_unique<Work>();
    work->generation = generation;
    work->list = state.listView;
    work->items = state.items;
    HANDLE worker = CreateThread(nullptr, 0, ImageWorker, work.get(), 0, nullptr);
    Require(worker != nullptr, "Image worker creation failed");
    work.release();
    CloseHandle(worker);
    return true;
}

void ArrangeDesktopItems() {
    RECT client{};
    GetClientRect(state.listView, &client);
    int spacingX = state.iconSize + 48, spacingY = state.iconSize + 68;
    ListView_SetIconSpacing(state.listView, spacingX, spacingY);
    if (IsAutoArrangeEnabled()) {
        ListView_Arrange(state.listView, LVA_DEFAULT);
        return;
    }
    int rows = std::max(1L, (client.bottom - 20) / spacingY);
    for (int i = 0; i < ListView_GetItemCount(state.listView); ++i) {
        ListView_SetItemPosition32(state.listView, i, 16 + (i / rows) * spacingX,
                                   16 + (i % rows) * spacingY);
    }
    if (IsSnapToGridEnabled()) {
        SnapItems();
    }
}

void SynchronizeDesktop() {
    // Called from a posted message/timer, after Explorer finishes its sort/refresh.
    // Never call shell COM while inside a synchronous ListView notification.
    state.syncPending = false;
    if (state.pressed || state.externalDrag || state.committing || state.contextActive) {
        return;
    }
    bool layout = state.layoutPending;
    state.layoutPending = false;
    state.refreshing = true;
    try {
        EnsureImageList();
        LONG_PTR style = GetWindowLongPtrW(state.listView, GWL_STYLE);
        LONG_PTR wanted = (style | LVS_SHAREIMAGELISTS) & ~LVS_TYPEMASK;
        if (style != wanted) {
            SetWindowLongPtrW(state.listView, GWL_STYLE, wanted);
        }
        DWORD extended = ListView_GetExtendedListViewStyle(state.listView);
        DWORD wantedExtended = extended | LVS_EX_DOUBLEBUFFER;
        if (extended != wantedExtended) {
            ListView_SetExtendedListViewStyle(state.listView, wantedExtended);
        }
        DWORD spacing = static_cast<DWORD>(ListView_GetItemSpacing(state.listView, FALSE));
        if (LOWORD(spacing) != state.iconSize + 48 || HIWORD(spacing) != state.iconSize + 68) {
            ListView_SetIconSpacing(state.listView, state.iconSize + 48, state.iconSize + 68);
        }
        if (RefreshItems()) {
            layout = true;
        }
        if (layout) {
            ArrangeDesktopItems();
            InvalidateRect(state.listView, nullptr, FALSE);
        } else if (state.snapPending && IsSnapToGridEnabled()) {
            SnapItems();
        }
        state.snapPending = false;
        Status();
        state.refreshing = false;
    } catch (...) {
        state.refreshing = false;
        throw;
    }
}

LRESULT CALLBACK ParentProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
LRESULT CALLBACK ListProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);

void Restore() {
    if (!state.active) {
        Status();
        return;
    }
    CancelInteraction();
    state.pendingMoves.clear();
    if (state.contextActive) {
        EndMenu();
    }
    state.contextPending = false;
    state.active = false;
    ++state.generation;
    state.deadline = 0;
    state.syncPending = state.layoutPending = state.refreshing = false;
    KillTimer(state.listView, TimerId);
    if (IsWindow(state.shellViewWindow)) {
        RemoveWindowSubclass(state.shellViewWindow, ParentProc, ParentSubclassId);
    }
    if (IsWindow(state.listView)) {
        RemoveWindowSubclass(state.listView, ListProc, ListSubclassId);
        SendMessageW(state.listView, WM_SETREDRAW, FALSE, 0);
        auto alignment = GetWindowLongPtrW(state.listView, GWL_STYLE) & LVS_AUTOARRANGE;
        DWORD alignmentMask = LVS_EX_SNAPTOGRID | LVS_EX_AUTOAUTOARRANGE;
        DWORD alignmentEx = ListView_GetExtendedListViewStyle(state.listView) & alignmentMask;
        auto restoreStyle = (state.originalStyle & ~LVS_AUTOARRANGE) | alignment;
        DWORD restoreEx = (state.originalExtendedStyle & ~alignmentMask) | alignmentEx;
        SetWindowLongPtrW(state.listView, GWL_STYLE, restoreStyle & ~LVS_AUTOARRANGE);
        ListView_SetImageList(state.listView, state.originalImages, LVSIL_NORMAL);
        ListView_SetExtendedListViewStyle(state.listView, restoreEx & ~alignmentMask);
        ListView_SetIconSpacing(state.listView, LOWORD(state.originalSpacing),
                                HIWORD(state.originalSpacing));
        std::vector<Item> current;
        bool snapshotOk = true;
        try {
            current = ReadDesktopItems(state.listView, state.testHost);
        } catch (...) {
            snapshotOk = false;
        }
        shared->restoreMatched = shared->restoreMissing = shared->restorePositionFailures = 0;
        for (const auto& old : state.originalItems) {
            auto found = std::find_if(current.begin(), current.end(), [&](const Item& item) {
                return item.key == old.key;
            });
            if (found == current.end()) {
                shared->restoreMissing++;
                continue;
            }
            int index = static_cast<int>(found - current.begin());
            ListView_SetItemPosition32(state.listView, index, old.position.x, old.position.y);
            POINT actual{};
            ListView_GetItemPosition(state.listView, index, &actual);
            shared->restoreMatched++;
            if (actual.x != old.position.x || actual.y != old.position.y) {
                shared->restorePositionFailures++;
            }
        }
        ListView_SetExtendedListViewStyle(state.listView, restoreEx);
        SetWindowLongPtrW(state.listView, GWL_STYLE, restoreStyle);
        if (alignment) {
            ListView_Arrange(state.listView, LVA_DEFAULT);
        } else if (alignmentEx & LVS_EX_SNAPTOGRID) {
            ListView_Arrange(state.listView, LVA_SNAPTOGRID);
        }
        SendMessageW(state.listView, WM_SETREDRAW, TRUE, 0);
        RedrawWindow(state.listView, nullptr, nullptr,
                     RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
        if (!snapshotOk) {
            shared->error = ERROR_PARTIAL_COPY;
            SetMessage(L"描画は解除しましたが、項目情報を取得できず配置を復元できませんでした。");
        }
    }
    if (state.customImages) {
        ImageList_Destroy(state.customImages);
        state.customImages = nullptr;
    }
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        state.pictures.clear();
    }
    if (!shared->error) {
        SetMessage(L"解除しました。元の画像リスト・間隔・配置を復元しました。");
    }
    Status();
}

void Apply(HWND list, const SharedCommand& request) {
    Require(request.requestedSize >= 64 && request.requestedSize <= 1024 &&
                request.requestedSize % 16 == 0,
            "Size must be 64..1024 in steps of 16");
    Require(request.leaseSeconds <= 3600, "Lease exceeds 3600 seconds");
    Require(GetWindowClassName(list) == WC_LISTVIEWW &&
                GetWindowThreadProcessId(list, nullptr) == GetCurrentThreadId(),
            "ListView thread mismatch");
    bool test = request.testHost != 0;
    Require(GetWindowClassName(GetParent(list)) ==
                (test ? L"DekaDesktopIcon.TestHost" : L"SHELLDLL_DefView"),
            "Unsupported desktop parent");
    if (state.active) {
        Restore();
    }
    state.listView = list;
    state.shellViewWindow = GetParent(list);
    state.testHost = test;
    state.profile = request.profile;
    Require(!state.profile.empty(), "Profile path is empty");
    int directory = SHCreateDirectoryExW(nullptr, ParentPath(state.profile).c_str(), nullptr);
    Require(directory == ERROR_SUCCESS || directory == ERROR_ALREADY_EXISTS ||
                directory == ERROR_FILE_EXISTS,
            "Cannot create shared settings directory");
    state.originalItems = ReadDesktopItems(list, test);
    state.originalStyle = GetWindowLongPtrW(list, GWL_STYLE);
    state.originalExtendedStyle = ListView_GetExtendedListViewStyle(list);
    state.originalView = ListView_GetView(list);
    Require(state.originalView == LV_VIEW_ICON,
            "Switch the native desktop to icon view before applying");
    state.originalSpacing = static_cast<DWORD>(ListView_GetItemSpacing(list, FALSE));
    state.originalImages = ListView_GetImageList(list, LVSIL_NORMAL);
    int originalW = 0, originalH = 0;
    Require(ImageList_GetIconSize(state.originalImages, &originalW, &originalH) != FALSE,
            "Original image list unavailable");
    shared->originalImageWidth = originalW;
    shared->originalImageHeight = originalH;
    shared->restoreMatched = shared->restoreMissing = shared->restorePositionFailures = 0;
    LVITEMW diagnosticItem{};
    diagnosticItem.mask = LVIF_IMAGE;
    diagnosticItem.iItem = 0;
    ListView_GetItem(list, &diagnosticItem);
    HMODULE controlsModule = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&ImageList_Create), &controlsModule);
    WriteUtf16(state.profile + L".diagnostic.ini",
               L"[ListView]\r\nStyle=" + std::to_wstring(state.originalStyle) +
                   L"\r\nExtendedStyle=" + std::to_wstring(state.originalExtendedStyle) +
                   L"\r\nView=" + std::to_wstring(state.originalView) + L"\r\nFirstImageIndex=" +
                   std::to_wstring(diagnosticItem.iImage) + L"\r\nComctl=" +
                   ModulePath(controlsModule) + L"\r\n");
    WriteRollback(state.profile, state.originalItems, state.originalStyle, state.originalSpacing,
                  originalW, originalH);
    if (!state.gdiplus) {
        Gdiplus::GdiplusStartupInput input;
        Require(Gdiplus::GdiplusStartup(&state.gdiplus, &input, nullptr) == Gdiplus::Ok,
                "GDI+ startup failed");
    }
    if (!callbackReference) {
        Require(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                                   reinterpret_cast<LPCWSTR>(&Apply), &callbackReference) != FALSE,
                "Module reference unavailable");
    }
    state.iconSize = static_cast<int>(request.requestedSize);
    state.customImages =
        ImageList_Create(state.iconSize, state.iconSize, ILC_COLOR32 | ILC_MASK, 1, 1);
    Require(state.customImages != nullptr, "1024px image list allocation failed");
    Require(ImageList_SetImageCount(state.customImages, 1) != FALSE,
            "Image list allocation failed");
    if (!SetWindowSubclass(state.shellViewWindow, ParentProc, ParentSubclassId, 0)) {
        ImageList_Destroy(state.customImages);
        state.customImages = nullptr;
        throw std::runtime_error("Parent subclass failed");
    }
    if (!SetWindowSubclass(list, ListProc, ListSubclassId, 0)) {
        RemoveWindowSubclass(state.shellViewWindow, ParentProc, ParentSubclassId);
        ImageList_Destroy(state.customImages);
        state.customImages = nullptr;
        throw std::runtime_error("List subclass failed");
    }
    state.active = true;
    shared->customPaintCount = 0;
    shared->lastDrawWidth = shared->lastDrawHeight = 0;
    state.syncPending = state.layoutPending = false;
    state.snapPending = state.contextPending = state.contextActive = state.menuTest =
        state.menuSeen = false;
    state.contextRequests = state.contextMenus = 0;
    state.menuTitles.clear();
    state.contextTick = 0;
    state.contextPopup = nullptr;
    state.menuStyle.clear();
    wcscpy_s(shared->ownerModule, ModulePath(moduleHandle).c_str());
    state.selectionAnchor = ListView_GetSelectionMark(list);
    state.moveCommits = state.nativeDrags = 0;
    state.deadline =
        request.leaseSeconds ? GetTickCount64() + UINT64(request.leaseSeconds) * 1000 : 0;
    try {
        SendMessageW(list, WM_SETREDRAW, FALSE, 0);
        SetWindowLongPtrW(list, GWL_STYLE,
                          (state.originalStyle | LVS_SHAREIMAGELISTS) & ~LVS_TYPEMASK);
        ListView_SetExtendedListViewStyle(list, state.originalExtendedStyle | LVS_EX_DOUBLEBUFFER);
        ListView_SetImageList(list, state.customImages, LVSIL_NORMAL);
        ArrangeDesktopItems();
        RefreshItems(true);
        Require(SetTimer(list, TimerId, 1000, nullptr) != 0, "Rollback timer creation failed");
        SendMessageW(list, WM_SETREDRAW, TRUE, 0);
        RedrawWindow(list, nullptr, nullptr,
                     RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
        Status();
        Require(shared->imageWidth == request.requestedSize,
                "ListView rejected requested image size");
        SetMessage(L"Explorer 内のデスクトップ描画を差し替えました。");
    } catch (...) {
        Restore();
        throw;
    }
}

bool GetNativeIconRect(int index, RECT& rect) {
    bool old = state.baseGeometry;
    state.baseGeometry = true;
    rect.left = LVIR_ICON;
    bool result = SendMessageW(state.listView, LVM_GETITEMRECT, index,
                               reinterpret_cast<LPARAM>(&rect)) != FALSE;
    state.baseGeometry = old;
    return result;
}

bool GetPaintedItemRect(int index, int kind, RECT& rect) {
    if (!GetNativeIconRect(index, rect)) {
        return false;
    }
    rect.right = rect.left + state.iconSize;
    rect.bottom = rect.top + state.iconSize;
    if (kind == LVIR_LABEL) {
        rect.top = rect.bottom + 4;
        rect.bottom = rect.top + 48;
    } else if (kind == LVIR_BOUNDS || kind == LVIR_SELECTBOUNDS) {
        rect.bottom += 52;
    }
    return true;
}

int HitTestPaintedItem(POINT point, UINT* flags = nullptr) {
    for (int i = ListView_GetItemCount(state.listView) - 1; i >= 0; --i) {
        RECT rect{};
        if (!GetPaintedItemRect(i, LVIR_BOUNDS, rect)) {
            continue;
        }
        if (PtInRect(&rect, point)) {
            if (flags) {
                *flags = point.y < rect.top + state.iconSize ? LVHT_ONITEMICON : LVHT_ONITEMLABEL;
            }
            return i;
        }
    }
    if (flags) {
        *flags = LVHT_NOWHERE;
    }
    return -1;
}

void SnapItems() {
    if (IsAutoArrangeEnabled()) {
        ListView_Arrange(state.listView, LVA_DEFAULT);
        return;
    }
    int count = ListView_GetItemCount(state.listView);
    if (!count) {
        return;
    }
    RECT first{}, client{};
    POINT position{}, origin{};
    if (!GetNativeIconRect(0, first) || !ListView_GetItemPosition(state.listView, 0, &position)) {
        return;
    }
    ListView_GetOrigin(state.listView, &origin);
    GetClientRect(state.listView, &client);
    // The native snap routine clamps two 1024px columns into the same viewport
    // cell. Snap painted bounds to the configured grid and extend the virtual
    // desktop horizontally, keeping stationary items ahead of a moved selection.
    LONG offsetX = position.x - first.left - origin.x, offsetY = position.y - first.top - origin.y;
    LONG sx = state.iconSize + 48, sy = state.iconSize + 68,
         rows = std::max(1L, (client.bottom - 20) / sy);
    std::vector<std::pair<LONG, LONG>> occupied;
    for (int selected = 0; selected <= 1; ++selected) {
        for (int i = 0; i < count; ++i) {
            bool itemSelected =
                (ListView_GetItemState(state.listView, i, LVIS_SELECTED) & LVIS_SELECTED) != 0;
            if (itemSelected != (selected != 0)) {
                continue;
            }
            RECT icon{};
            if (!GetNativeIconRect(i, icon)) {
                continue;
            }
            LONG col = (std::max(0L, icon.left + origin.x) + sx / 2) / sx;
            LONG row = std::min(rows - 1, (std::max(0L, icon.top + origin.y) + sy / 2) / sy);
            auto cell = std::make_pair(col, row);
            while (std::find(occupied.begin(), occupied.end(), cell) != occupied.end()) {
                if (++cell.second >= rows) {
                    cell.second = 0;
                    ++cell.first;
                }
            }
            occupied.push_back(cell);
            ListView_SetItemPosition32(state.listView, i, offsetX + cell.first * sx,
                                       offsetY + cell.second * sy);
        }
    }
}

#include "Interaction.h"
#include "../tests/RegressionChecks.h"

void DrawItem(NMLVCUSTOMDRAW* draw) {
    int index = static_cast<int>(draw->nmcd.dwItemSpec);
    if (index < 0 || index >= static_cast<int>(state.items.size())) {
        return;
    }
    RECT icon{};
    if (!GetNativeIconRect(index, icon)) {
        return;
    }
    shared->nativeIconWidth = icon.right - icon.left;
    shared->nativeIconHeight = icon.bottom - icon.top;
    LVHITTESTINFO hit{};
    hit.pt = POINT{icon.left + state.iconSize / 2, icon.top + state.iconSize / 2};
    shared->nativeHitAtCenter = ListView_HitTest(state.listView, &hit);
    RECT label{};
    ListView_GetItemRect(state.listView, index, &label, LVIR_LABEL);
    // Enforce physical 1024px even if shell callback reports a smaller drawing rectangle.
    icon.right = icon.left + state.iconSize;
    icon.bottom = icon.top + state.iconSize;
    label.left = icon.left;
    label.right = icon.right;
    label.top = icon.bottom + 4;
    label.bottom = label.top + 48;
    Gdiplus::Graphics graphics(draw->nmcd.hdc);
    graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
    UINT nativeState = ListView_GetItemState(state.listView, index, LVIS_SELECTED | LVIS_FOCUSED);
    if (nativeState & LVIS_SELECTED) {
        Gdiplus::SolidBrush highlight(Gdiplus::Color(65, 70, 150, 225));
        graphics.FillRectangle(&highlight, icon.left - 4, icon.top - 4, state.iconSize + 8,
                               state.iconSize + 60);
        Gdiplus::Pen border(Gdiplus::Color(165, 190, 222, 255), 1);
        graphics.DrawRectangle(&border, icon.left - 4, icon.top - 4, state.iconSize + 7,
                               state.iconSize + 59);
    }
    std::shared_ptr<Picture> picture;
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        auto found = state.pictures.find(state.items[index].key);
        if (found != state.pictures.end()) {
            picture = found->second;
        }
    }
    if (picture && picture->bitmap) {
        double scale = double(state.iconSize) /
                       std::max(picture->bitmap->GetWidth(), picture->bitmap->GetHeight());
        int w = static_cast<int>(picture->bitmap->GetWidth() * scale),
            h = static_cast<int>(picture->bitmap->GetHeight() * scale);
        graphics.DrawImage(picture->bitmap.get(),
                           Gdiplus::Rect(icon.left + (state.iconSize - w) / 2,
                                         icon.top + (state.iconSize - h) / 2, w, h));
        shared->sourceWidth = picture->sourceWidth;
        shared->sourceHeight = picture->sourceHeight;
    } else {
        Gdiplus::SolidBrush loading(Gdiplus::Color(100, 90, 160, 190));
        graphics.FillRectangle(&loading, icon.left + state.iconSize / 4,
                               icon.top + state.iconSize / 4, state.iconSize / 2,
                               state.iconSize / 2);
    }
    if (state.items[index].link) {
        Gdiplus::SolidBrush white(Gdiplus::Color(230, 250, 250, 250));
        graphics.FillRectangle(&white, icon.left + 4, icon.bottom - 32, 28, 28);
        Gdiplus::Pen arrow(Gdiplus::Color(255, 40, 85, 150), 3);
        graphics.DrawLine(&arrow, int(icon.left + 10), int(icon.bottom - 10), int(icon.left + 25),
                          int(icon.bottom - 25));
        graphics.DrawLine(&arrow, int(icon.left + 14), int(icon.bottom - 25), int(icon.left + 25),
                          int(icon.bottom - 25));
        graphics.DrawLine(&arrow, int(icon.left + 25), int(icon.bottom - 25), int(icon.left + 25),
                          int(icon.bottom - 14));
    }
    graphics.Flush();
    int saved = SaveDC(draw->nmcd.hdc);
    SetBkMode(draw->nmcd.hdc, TRANSPARENT);
    HFONT font = reinterpret_cast<HFONT>(SendMessageW(state.listView, WM_GETFONT, 0, 0));
    if (font) {
        SelectObject(draw->nmcd.hdc, font);
    }
    auto name = state.items[index].name;
    UINT flags = DT_CENTER | DT_WORDBREAK | DT_END_ELLIPSIS | DT_NOPREFIX;
    RECT shadow = label;
    OffsetRect(&shadow, 1, 1);
    SetTextColor(draw->nmcd.hdc, RGB(0, 0, 0));
    DrawTextW(draw->nmcd.hdc, name.c_str(), -1, &shadow, flags);
    SetTextColor(draw->nmcd.hdc, RGB(255, 255, 255));
    DrawTextW(draw->nmcd.hdc, name.c_str(), -1, &label, flags);
    RestoreDC(draw->nmcd.hdc, saved);
    shared->customPaintCount++;
    shared->lastDrawWidth = state.iconSize;
    shared->lastDrawHeight = state.iconSize;
}

LRESULT CALLBACK ParentProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR,
                            DWORD_PTR) {
    ObserveContextMenu(message, wParam, lParam);
    if (message == WM_NOTIFY && state.active) {
        auto header = reinterpret_cast<NMHDR*>(lParam);
        if (header && header->hwndFrom == state.listView && header->code == LVN_GETDISPINFOW) {
            // Explorer indexes its shared system image list. Our geometry list has a single slot.
            LRESULT result = DefSubclassProc(hwnd, message, wParam, lParam);
            auto info = reinterpret_cast<NMLVDISPINFOW*>(lParam);
            if (info->item.mask & LVIF_IMAGE) {
                info->item.iImage = 0;
                info->item.mask &= ~LVIF_DI_SETITEM;
            }
            return result;
        }
        if (header && header->hwndFrom == state.listView && header->code == NM_CUSTOMDRAW) {
            auto draw = reinterpret_cast<NMLVCUSTOMDRAW*>(lParam);
            if (draw->nmcd.dwDrawStage == CDDS_PREPAINT) {
                try {
                    EnsureImageList();
                } catch (...) {
                    PostMessageW(state.listView, CommandMessage(),
                                 static_cast<WPARAM>(Command::Restore), 0);
                }
                return CDRF_NOTIFYITEMDRAW | CDRF_NOTIFYPOSTPAINT;
            }
            if (draw->nmcd.dwDrawStage == CDDS_POSTPAINT) {
                DrawInteraction(draw->nmcd.hdc);
                return CDRF_DODEFAULT;
            }
            if (draw->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
                try {
                    DrawItem(draw);
                    return CDRF_SKIPDEFAULT;
                } catch (...) {
                    PostMessageW(state.listView, CommandMessage(),
                                 static_cast<WPARAM>(Command::Restore), 0);
                }
            }
        }
    }
    if (state.active && message == WM_COMMAND) {
        LRESULT result = DefSubclassProc(hwnd, message, wParam, lParam);
        QueueSynchronization();
        return result;
    }
    return DefSubclassProc(hwnd, message, wParam, lParam);
}

LRESULT CALLBACK ListProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR,
                          DWORD_PTR) {
    ObserveContextMenu(message, wParam, lParam);
    if (state.active && !state.baseGeometry && message == LVM_GETITEMRECT && lParam) {
        auto rect = reinterpret_cast<RECT*>(lParam);
        int kind = rect->left;
        if (kind == LVIR_ICON || kind == LVIR_LABEL || kind == LVIR_BOUNDS ||
            kind == LVIR_SELECTBOUNDS) {
            return GetPaintedItemRect(static_cast<int>(wParam), kind, *rect);
        }
    }
    if (state.active && message == LVM_HITTEST && lParam) {
        auto hit = reinterpret_cast<LVHITTESTINFO*>(lParam);
        hit->iItem = HitTestPaintedItem(hit->pt, &hit->flags);
        hit->iSubItem = 0;
        return hit->iItem;
    }
    LRESULT interactionResult = 0;
    if (state.active && HandleInteraction(hwnd, message, wParam, lParam, interactionResult)) {
        return interactionResult;
    }
    if (state.active && message == WM_PAINT && !state.painting) {
        // Erase the previous translucent marquee/drag preview before drawing the next frame.
        // Double buffering keeps the full background repaint from flickering.
        state.painting = true;
        InvalidateRect(hwnd, nullptr, TRUE);
        LRESULT result = DefSubclassProc(hwnd, message, wParam, lParam);
        state.painting = false;
        return result;
    }
    if (state.active && !state.refreshing && message == LVM_SETIMAGELIST &&
        wParam == LVSIL_NORMAL && reinterpret_cast<HIMAGELIST>(lParam) != state.customImages) {
        // Keep the custom list attached. The shell still owns its logical image list:
        // return that previous shell handle so a caller may safely destroy it.
        // In particular, never hand our customImages back to Explorer for destruction.
        HIMAGELIST previous = state.originalImages;
        state.originalImages = reinterpret_cast<HIMAGELIST>(lParam);
        QueueSynchronization();
        return reinterpret_cast<LRESULT>(previous);
    }
    if (state.active && !state.refreshing && message == LVM_SETICONSPACING) {
        LRESULT result = DefSubclassProc(hwnd, message, wParam,
                                         MAKELPARAM(state.iconSize + 48, state.iconSize + 68));
        QueueSynchronization();
        return result;
    }
    if (state.active && message == LVM_ARRANGE && wParam == LVA_SNAPTOGRID) {
        SnapItems();
        QueueSynchronization();
        return TRUE;
    }
    if (state.active && message == WM_STYLECHANGING && wParam == GWL_STYLE && lParam) {
        auto style = reinterpret_cast<STYLESTRUCT*>(lParam);
        style->styleNew = (style->styleNew | LVS_SHAREIMAGELISTS) & ~LVS_TYPEMASK;
    }
    if (state.active && message == WM_STYLECHANGED && wParam == static_cast<WPARAM>(GWL_STYLE) &&
        lParam) {
        auto style = reinterpret_cast<STYLESTRUCT*>(lParam);
        LRESULT result = DefSubclassProc(hwnd, message, wParam, lParam);
        if ((style->styleNew ^ style->styleOld) & LVS_AUTOARRANGE) {
            QueueSynchronization((style->styleNew & LVS_AUTOARRANGE) != 0);
        }
        return result;
    }
    if (state.active && message == LVM_SETEXTENDEDLISTVIEWSTYLE) {
        DWORD old = ListView_GetExtendedListViewStyle(hwnd);
        LRESULT result = DefSubclassProc(hwnd, message, wParam ? wParam | LVS_EX_DOUBLEBUFFER : 0,
                                         lParam | LVS_EX_DOUBLEBUFFER);
        DWORD updated = ListView_GetExtendedListViewStyle(hwnd);
        if (!(old & LVS_EX_SNAPTOGRID) && (updated & LVS_EX_SNAPTOGRID)) {
            state.snapPending = true;
        }
        QueueSynchronization();
        return result;
    }
    if (state.active && !state.refreshing &&
        (message == LVM_SORTITEMS || message == LVM_SORTITEMSEX || message == LVM_ARRANGE ||
         message == LVM_SETITEMCOUNT || message == LVM_INSERTITEMW || message == LVM_DELETEITEM ||
         message == LVM_DELETEALLITEMS || (message == WM_SETREDRAW && wParam))) {
        LRESULT result = DefSubclassProc(hwnd, message, wParam, lParam);
        QueueSynchronization(!state.committing && message != WM_SETREDRAW &&
                             message != LVM_SETITEMCOUNT && message != LVM_INSERTITEMW &&
                             message != LVM_DELETEITEM && message != LVM_DELETEALLITEMS);
        return result;
    }
    if (message == SyncMessage()) {
        if (state.active) {
            try {
                SynchronizeDesktop();
            } catch (...) {
                try {
                    Restore();
                } catch (...) {
                }
            }
        }
        return 0;
    }
    if (message == CommandMessage() && wParam == static_cast<WPARAM>(Command::Restore)) {
        try {
            Restore();
        } catch (...) {
        }
        return 0;
    }
    if (message == WM_TIMER && wParam == TimerId && state.active) {
        try {
            if (state.deadline && GetTickCount64() >= state.deadline && !state.externalDrag) {
                Restore();
                SetMessage(L"時間制限により自動解除しました。");
                return 0;
            }
            SynchronizeDesktop();
        } catch (...) {
            try {
                Restore();
            } catch (...) {
            }
        }
        return 0;
    }
    if (message == WM_NCDESTROY) {
        CancelInteraction();
        state.active = false;
        ++state.generation;
        KillTimer(hwnd, TimerId);
        if (IsWindow(state.shellViewWindow)) {
            RemoveWindowSubclass(state.shellViewWindow, ParentProc, ParentSubclassId);
        }
        RemoveWindowSubclass(hwnd, ListProc, ListSubclassId);
        if (shared) {
            shared->active = shared->subclassInstalled = 0;
            SetMessage(L"デスクトップが作り直されました。再適用してください。");
        }
    }
    return DefSubclassProc(hwnd, message, wParam, lParam);
}

void Capture(const std::wstring& path) {
    Require(state.active && !path.empty(), "Capture needs an active patch and output path");
    RECT rect{};
    GetClientRect(state.listView, &rect);
    int w = std::max(state.iconSize + 64, int(std::min(4096L, rect.right))),
        h = std::max(state.iconSize + 96, int(std::min(4096L, rect.bottom)));
    Gdiplus::Bitmap bitmap(w, h, PixelFormat32bppARGB);
    Gdiplus::Graphics graphics(&bitmap);
    graphics.Clear(Gdiplus::Color(255, 26, 38, 48));
    HDC dc = graphics.GetHDC();
    SendMessageW(state.listView, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(dc),
                 PRF_CLIENT | PRF_ERASEBKGND);
    graphics.ReleaseHDC(dc);
    auto png = PngEncoder();
    Require(bitmap.Save(path.c_str(), &png, nullptr) == Gdiplus::Ok, "PNG capture failed");
    Status();
}

void HandleCommand(HWND hwnd) {
    if (!shared) {
        mapping = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE,
                                   MappingName(GetCurrentProcessId()).c_str());
        if (!mapping) {
            return;
        }
        shared = static_cast<SharedCommand*>(
            MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SharedCommand)));
        if (!shared) {
            CloseHandle(mapping);
            mapping = nullptr;
            return;
        }
    }
    if (shared->magic != Magic || shared->version != Version ||
        shared->bytes != sizeof(SharedCommand) || shared->targetPid != GetCurrentProcessId() ||
        shared->listHwnd != reinterpret_cast<UINT64>(hwnd)) {
        return;
    }
    shared->error = 0;
    try {
        switch (static_cast<Command>(shared->command)) {
        case Command::Apply: {
            SharedCommand request = *shared;
            Apply(hwnd, request);
            break;
        }
        case Command::Restore:
            Restore();
            break;
        case Command::Query:
            if (!state.listView) {
                state.listView = hwnd;
            }
            Status();
            break;
        case Command::Capture:
            Capture(shared->capture);
            break;
        case Command::InteractionReport:
            InteractionReport(shared->capture);
            Status();
            break;
        case Command::CaptureDrag:
            CaptureDrag(shared->capture);
            Status();
            break;
        case Command::ValidateInteraction:
            ValidateInteraction(shared->capture);
            Status();
            break;
        case Command::ValidateShellBehavior:
            ValidateShellBehavior(shared->capture);
            Status();
            break;
        case Command::ShowContext: {
            Require(state.active, "Show context requires an active patch");
            POINT p{-1, -1};
            if (shared->requestedSize == 1) {
                ClearSelection();
                SetItemSelected(0, true);
                FocusItem(0);
                RECT r{};
                GetPaintedItemRect(0, LVIR_ICON, r);
                p = POINT{r.left + state.iconSize / 2, r.top + state.iconSize - 24};
            } else if (shared->requestedSize == 0) {
                ClearSelection();
                RECT client{};
                GetClientRect(state.listView, &client);
                for (LONG y = client.bottom - 4; y >= 0 && p.x < 0; y -= 16) {
                    for (LONG x = client.right - 4; x >= 0; x -= 16) {
                        if (HitTestPaintedItem(POINT{x, y}) < 0) {
                            p = POINT{x, y};
                            break;
                        }
                    }
                }
                Require(p.x >= 0, "No empty desktop point available");
            }
            ClientToScreen(state.listView, &p);
            if (shared->requestedSize == 2) {
                p = POINT{-1, -1};
            }
            QueueContext(p);
            break;
        }
        default:
            throw std::runtime_error("Unsupported command");
        }
    } catch (const std::exception& ex) {
        shared->error = ERROR_FUNCTION_FAILED;
        MultiByteToWideChar(CP_UTF8, 0, ex.what(), -1, shared->message, 512);
    } catch (...) {
        shared->error = ERROR_UNHANDLED_EXCEPTION;
        SetMessage(L"パッチの処理でエラーが発生しました。");
    }
    InterlockedExchange(&shared->ack, 1);
}
} // namespace

extern "C" __declspec(dllexport) LRESULT CALLBACK DesktopHook(int code, WPARAM wParam,
                                                              LPARAM lParam) {
    if (code >= 0 && wParam == PM_REMOVE) {
        // Run outside SendMessage: Explorer's STA cannot make COM call-outs from input-synchronous
        // hooks.
        auto message = reinterpret_cast<MSG*>(lParam);
        if (message && message->message == CommandMessage()) {
            HandleCommand(message->hwnd);
        }
    }
    return CallNextHookEx(nullptr, code, wParam, lParam);
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        moduleHandle = instance;
        DisableThreadLibraryCalls(instance);
    }
    if (reason == DLL_PROCESS_DETACH) {
        if (shared) {
            UnmapViewOfFile(shared);
        }
        if (mapping) {
            CloseHandle(mapping);
        }
    }
    return TRUE;
}
