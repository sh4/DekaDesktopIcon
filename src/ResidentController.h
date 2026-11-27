// SPDX-License-Identifier: MIT
// Copyright (c) 2026 sh4
#pragma once

// Included inside the controller's anonymous namespace, after JSON/report helpers.
constexpr UINT TrayCallback = WM_APP + 101;
constexpr UINT ResidentStart = WM_APP + 102;
constexpr UINT ResidentInspect = WM_APP + 103;
constexpr UINT_PTR ResidentTimer = 101;
constexpr ULONG_PTR ResidentMagic = 0x44445231;
constexpr int SettingsId = 101, EnableId = 102, DisableId = 103, ExitId = 104;

enum class ResidentAction : DWORD { Enable = 1, Disable = 2, Exit = 3, Settings = 4 };

struct ResidentRequest {
    DWORD bytes = sizeof(ResidentRequest);
    ResidentAction action = ResidentAction::Enable;
    DWORD size = 0, lease = 0;
};

struct ResidentState {
    HWND window = nullptr;
    HICON icon = nullptr;
    HANDLE settingsProcess = nullptr;
    DesktopTarget desktop{};
    SharedCommand status{};
    std::wstring profile, message;
    DWORD size = 0, lease = 0, lastError = 0;
    bool test = false, enabled = true, busy = false, trayAdded = false, initialized = false;
    UINT taskbarCreated = 0;
} resident;

const wchar_t* ResidentClass(bool test) {
    return test ? L"DekaDesktopIcon.Resident.Test.v1" : L"DekaDesktopIcon.Resident.v1";
}

std::wstring AbsolutePath(const std::wstring& path) {
    wchar_t full[32768]{};
    DWORD length = GetFullPathNameW(path.c_str(), 32768, full, nullptr);
    Require(length && length < 32768, "Invalid profile path");
    return full;
}

bool ValidRequestedSize(DWORD size) {
    return size == 0 || (size >= 64 && size <= 1024 && size % 16 == 0);
}

std::wstring ResidentProfile(const std::wstring& profile) {
    return profile + L".resident.ini";
}

void SaveResidentPreferences() {
    std::wstring text = L"[Resident]\r\nSize=" + std::to_wstring(resident.size) + L"\r\nLease=" +
                        std::to_wstring(resident.lease) + L"\r\n";
    WriteUtf16(ResidentProfile(resident.profile), text);
}

HICON CreateTrayIcon() {
    HDC screen = GetDC(nullptr);
    HDC dc = CreateCompatibleDC(screen);
    HBITMAP color = CreateCompatibleBitmap(screen, 32, 32);
    HBITMAP mask = CreateBitmap(32, 32, 1, 1, nullptr);
    auto old = SelectObject(dc, color);
    RECT bounds{0, 0, 32, 32};
    HBRUSH background = CreateSolidBrush(RGB(0, 103, 192));
    FillRect(dc, &bounds, background);
    DeleteObject(background);
    HBRUSH white = CreateSolidBrush(RGB(255, 255, 255));
    for (const auto& rect :
         {RECT{6, 6, 17, 17}, RECT{20, 6, 26, 12}, RECT{20, 15, 26, 21}, RECT{6, 20, 17, 26}}) {
        FillRect(dc, &rect, white);
    }
    DeleteObject(white);
    SelectObject(dc, mask);
    PatBlt(dc, 0, 0, 32, 32, BLACKNESS);
    SelectObject(dc, old);
    ICONINFO info{TRUE, 0, 0, mask, color};
    HICON icon = CreateIconIndirect(&info);
    DeleteObject(mask);
    DeleteObject(color);
    DeleteDC(dc);
    ReleaseDC(nullptr, screen);
    return icon;
}

void UpdateTray(const std::wstring& notification = L"") {
    NOTIFYICONDATAW data{sizeof(data)};
    data.hWnd = resident.window;
    data.uID = 1;
    data.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_SHOWTIP;
    data.uCallbackMessage = TrayCallback;
    data.hIcon = resident.icon;
    std::wstring tip = L"DekaDesktopIcon — ";
    tip += resident.lastError       ? L"適用できませんでした"
           : resident.status.active ? L"拡大中"
                                    : L"無効";
    if (resident.status.active) {
        tip += L" (" + std::to_wstring(resident.status.size) + L" px)";
    }
    wcsncpy_s(data.szTip, tip.c_str(), _TRUNCATE);
    if (!notification.empty()) {
        data.uFlags |= NIF_INFO;
        wcscpy_s(data.szInfoTitle, L"DekaDesktopIcon");
        wcsncpy_s(data.szInfo, notification.c_str(), _TRUNCATE);
        data.dwInfoFlags = NIIF_ERROR;
    }
    if (Shell_NotifyIconW(resident.trayAdded ? NIM_MODIFY : NIM_ADD, &data)) {
        resident.trayAdded = true;
        data.uVersion = NOTIFYICON_VERSION_4;
        Shell_NotifyIconW(NIM_SETVERSION, &data);
    }
}

SharedCommand ReadResidentPatch(const DesktopTarget& desktop) {
    SharedCommand status{};
    if (!desktop.pid) {
        return status;
    }
    HANDLE mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, MappingName(desktop.pid).c_str());
    if (!mapping) {
        return status;
    }
    auto memory = static_cast<const SharedCommand*>(
        MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, sizeof(SharedCommand)));
    if (memory && memory->magic == Magic && memory->version == Version &&
        memory->bytes == sizeof(SharedCommand) && memory->targetPid == desktop.pid) {
        status = *memory;
    }
    if (memory) {
        UnmapViewOfFile(memory);
    }
    CloseHandle(mapping);
    return status;
}

bool ChangeEnlargement(bool enable, bool save, bool notify) {
    if (resident.busy) {
        return false;
    }
    resident.busy = true;
    bool success = false;
    try {
        auto desktop = FindDesktop(resident.test);
        // With Explorer gone there is no live patch to restore; quitting must still work.
        auto status = !enable && !desktop.list
                          ? SharedCommand{}
                          : SendCommand(desktop, enable ? Command::Apply : Command::Restore,
                                        resident.size, resident.lease, resident.profile);
        resident.desktop = desktop;
        resident.status = status;
        resident.lastError = status.error;
        resident.message = status.message;
        if (!status.error) {
            resident.enabled = enable;
            if (save) {
                SaveResidentPreferences();
            }
            success = true;
        }
    } catch (const std::exception& ex) {
        resident.lastError = ERROR_GEN_FAILURE;
        resident.message = Utf16(ex.what());
    }
    resident.busy = false;
    SetWindowTextW(resident.window, resident.message.c_str());
    UpdateTray(!success && notify ? resident.message : L"");
    return success;
}

std::wstring QuoteArgument(const std::wstring& value) {
    std::wstring result = L"\"";
    size_t slashes = 0;
    for (wchar_t c : value) {
        if (c == L'\\') {
            ++slashes;
        } else {
            result.append(slashes * (c == L'"' ? 2 : 1), L'\\');
            slashes = 0;
            if (c == L'"') {
                result += L'\\';
            }
            result += c;
        }
    }
    result.append(slashes * 2, L'\\');
    return result + L'"';
}

void OpenSettings() {
    if (resident.settingsProcess &&
        WaitForSingleObject(resident.settingsProcess, 0) == WAIT_TIMEOUT) {
        struct Search {
            DWORD pid;
            HWND window;
        } search{GetProcessId(resident.settingsProcess), nullptr};

        EnumWindows(
            [](HWND window, LPARAM context) -> BOOL {
                auto& search = *reinterpret_cast<Search*>(context);
                DWORD pid = 0;
                GetWindowThreadProcessId(window, &pid);
                if (pid == search.pid && IsWindowVisible(window) && !GetWindow(window, GW_OWNER)) {
                    search.window = window;
                    return FALSE;
                }
                return TRUE;
            },
            reinterpret_cast<LPARAM>(&search));
        if (search.window) {
            if (IsIconic(search.window)) {
                ShowWindow(search.window, SW_RESTORE);
            }
            SetForegroundWindow(search.window);
        }
        return;
    }
    if (resident.settingsProcess) {
        CloseHandle(resident.settingsProcess);
        resident.settingsProcess = nullptr;
    }
    std::wstring path = ParentPath(ApplicationPath()) + L"\\Settings\\DekaDesktopIcon.Settings.exe";
    std::wstring args = QuoteArgument(path) + L" --owner-pid " +
                        std::to_wstring(GetCurrentProcessId()) + L" --profile " +
                        QuoteArgument(resident.profile);
    if (resident.test) {
        args += L" --test-host";
    }
    STARTUPINFOW startup{sizeof(startup)};
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_SHOWNORMAL;
    PROCESS_INFORMATION process{};
    if (CreateProcessW(path.c_str(), args.data(), nullptr, nullptr, FALSE, 0, nullptr,
                       ParentPath(path).c_str(), &startup, &process)) {
        CloseHandle(process.hThread);
        resident.settingsProcess = process.hProcess;
    } else {
        resident.lastError = GetLastError();
        resident.message =
            L"設定画面を起動できません。Settings フォルダーを含むアプリ一式を配置してください。";
        UpdateTray(resident.message);
    }
}

void ShowTrayMenu(POINT point) {
    HMENU menu = CreatePopupMenu();
    if (!menu) {
        return;
    }
    try {
        resident.status = ReadResidentPatch(FindDesktop(resident.test));
    } catch (...) {
        DestroyMenu(menu);
        return;
    }
    AppendMenuW(menu, MF_STRING, SettingsId, L"設定を開く");
    SetMenuDefaultItem(menu, SettingsId, FALSE);
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | (resident.status.active ? MF_CHECKED : 0), EnableId,
                L"拡大を有効にする");
    AppendMenuW(menu, MF_STRING | (!resident.status.active ? MF_CHECKED : 0), DisableId,
                L"拡大を無効にする");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, ExitId, L"終了");
    SetForegroundWindow(resident.window);
    int selected = TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, point.x, point.y,
                                    resident.window, nullptr);
    DestroyMenu(menu);
    PostMessageW(resident.window, WM_NULL, 0, 0);
    if (selected) {
        PostMessageW(resident.window, WM_COMMAND, selected, 0);
    }
}

void PollResident() {
    if (resident.busy) {
        return;
    }
    if (!resident.trayAdded) {
        UpdateTray();
    }
    DesktopTarget desktop{};
    try {
        desktop = FindDesktop(resident.test);
    } catch (...) {
        return;
    }
    if (!desktop.list) {
        return;
    }
    if (resident.enabled &&
        (desktop.pid != resident.desktop.pid || desktop.list != resident.desktop.list)) {
        // Explorer restart/RDP reconnection: establish a fresh baseline for the new desktop.
        ChangeEnlargement(true, false, false);
    } else {
        auto status = ReadResidentPatch(desktop);
        if (resident.status.active && !status.active) {
            // A trial or an explicit external restore must stay disabled.
            resident.enabled = false;
        }
        if (status.active != resident.status.active || status.size != resident.status.size) {
            resident.status = status;
            UpdateTray();
        }
    }
}

LRESULT CALLBACK ResidentProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == resident.taskbarCreated && resident.taskbarCreated) {
        resident.trayAdded = false;
        UpdateTray();
        return 0;
    }
    if (message == ResidentInspect) {
        if (wParam == 1) {
            return resident.size;
        }
        if (wParam == 2) {
            return resident.lease;
        }
        if (wParam == 3) {
            return resident.lastError;
        }
        if (wParam == 4) {
            return resident.initialized && !resident.busy;
        }
        return resident.enabled;
    }
    if (message == WM_COPYDATA && lParam) {
        auto data = reinterpret_cast<const COPYDATASTRUCT*>(lParam);
        if (data->dwData != ResidentMagic || data->cbData != sizeof(ResidentRequest) ||
            !data->lpData) {
            return FALSE;
        }
        auto request = *static_cast<const ResidentRequest*>(data->lpData);
        if (request.bytes != sizeof(request) || resident.busy) {
            return FALSE;
        }
        if (request.action == ResidentAction::Enable) {
            if (!ValidRequestedSize(request.size) || request.lease > 3600) {
                return FALSE;
            }
            auto oldSize = resident.size, oldLease = resident.lease;
            resident.size = request.size;
            resident.lease = request.lease;
            bool changed = ChangeEnlargement(true, true, false);
            if (!changed) {
                resident.size = oldSize;
                resident.lease = oldLease;
            }
            return changed;
        }
        if (request.action == ResidentAction::Disable) {
            return ChangeEnlargement(false, false, false);
        }
        if (request.action == ResidentAction::Settings) {
            OpenSettings();
            return TRUE;
        }
        if (request.action == ResidentAction::Exit) {
            if (!ChangeEnlargement(false, false, false)) {
                return FALSE;
            }
            DestroyWindow(window);
            return TRUE;
        }
        return FALSE;
    }
    if (message == ResidentStart) {
        ChangeEnlargement(true, false, true);
        resident.initialized = true;
        return 0;
    }
    if (message == TrayCallback) {
        UINT event = LOWORD(lParam);
        if (event == NIN_SELECT || event == NIN_KEYSELECT || event == WM_LBUTTONUP) {
            OpenSettings();
        } else if (event == WM_CONTEXTMENU || event == WM_RBUTTONUP) {
            POINT point{};
            GetCursorPos(&point);
            if (event == WM_CONTEXTMENU && lParam != WM_CONTEXTMENU) {
                point =
                    POINT{static_cast<short>(LOWORD(wParam)), static_cast<short>(HIWORD(wParam))};
            }
            ShowTrayMenu(point);
        }
        return 0;
    }
    if (message == WM_TIMER && wParam == ResidentTimer) {
        PollResident();
        return 0;
    }
    if (message == WM_COMMAND) {
        switch (LOWORD(wParam)) {
        case SettingsId:
            OpenSettings();
            break;
        case EnableId:
            ChangeEnlargement(true, true, true);
            break;
        case DisableId:
            ChangeEnlargement(false, false, true);
            break;
        case ExitId:
            if (ChangeEnlargement(false, false, true)) {
                DestroyWindow(window);
            }
            break;
        }
        return 0;
    }
    if (message == WM_CLOSE) {
        if (ChangeEnlargement(false, false, true)) {
            DestroyWindow(window);
        }
        return 0;
    }
    if (message == WM_QUERYENDSESSION) {
        ChangeEnlargement(false, false, false);
        return TRUE;
    }
    if (message == WM_DESTROY) {
        KillTimer(window, ResidentTimer);
        NOTIFYICONDATAW data{sizeof(data)};
        data.hWnd = window;
        data.uID = 1;
        Shell_NotifyIconW(NIM_DELETE, &data);
        if (resident.icon) {
            DestroyIcon(resident.icon);
        }
        if (resident.settingsProcess) {
            CloseHandle(resident.settingsProcess);
            resident.settingsProcess = nullptr;
        }
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

HWND FindResident(bool test) {
    return FindWindowW(ResidentClass(test), nullptr);
}

int RunResident(HINSTANCE instance, const std::wstring& profile, bool test) {
    auto name = std::wstring(L"Local\\") + ResidentClass(test);
    HANDLE singleton = CreateMutexW(nullptr, FALSE, name.c_str());
    Require(singleton != nullptr, "Resident mutex unavailable");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(singleton);
        return 0;
    }
    resident.profile = AbsolutePath(profile);
    resident.test = test;
    auto settings = ResidentProfile(resident.profile);
    resident.size = GetPrivateProfileIntW(L"Resident", L"Size", 0, settings.c_str());
    resident.lease = GetPrivateProfileIntW(L"Resident", L"Lease", 0, settings.c_str());
    if (!ValidRequestedSize(resident.size)) {
        resident.size = 0;
    }
    if (resident.lease > 3600) {
        resident.lease = 0;
    }
    resident.taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    resident.icon = CreateTrayIcon();
    WNDCLASSW wc{};
    wc.lpfnWndProc = ResidentProc;
    wc.hInstance = instance;
    wc.lpszClassName = ResidentClass(test);
    RegisterClassW(&wc);
    resident.window =
        CreateWindowExW(WS_EX_TOOLWINDOW, wc.lpszClassName, L"DekaDesktopIcon", WS_OVERLAPPED, 0, 0,
                        0, 0, nullptr, nullptr, instance, nullptr);
    if (!resident.window) {
        CloseHandle(singleton);
        throw std::runtime_error("Cannot create resident controller");
    }
    UpdateTray();
    SetTimer(resident.window, ResidentTimer, 2000, nullptr);
    PostMessageW(resident.window, ResidentStart, 0, 0);
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    CloseHandle(singleton);
    return 0;
}

LRESULT InspectResident(HWND window, WPARAM field) {
    DWORD_PTR value = 0;
    Require(SendMessageTimeoutW(window, ResidentInspect, field, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK,
                                15000, &value) != 0,
            "Resident is not responding");
    return static_cast<LRESULT>(value);
}

bool RequestResident(HWND window, const ResidentRequest& request) {
    // The hidden window exists before the posted startup apply finishes. Wait for that
    // operation (and any current tray action) before sending the next request.
    UINT64 deadline = GetTickCount64() + 14000;
    while (!InspectResident(window, 4)) {
        Require(GetTickCount64() < deadline, "Resident initialization is still in progress");
        Sleep(20);
    }
    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    AllowSetForegroundWindow(pid);
    COPYDATASTRUCT data{ResidentMagic, sizeof(request), const_cast<ResidentRequest*>(&request)};
    DWORD_PTR result = 0;
    Require(SendMessageTimeoutW(window, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&data),
                                SMTO_ABORTIFHUNG | SMTO_BLOCK, 15000, &result) != 0,
            "Resident is not responding");
    return result != 0;
}

HWND EnsureResident(const std::wstring& profile, bool test) {
    if (auto window = FindResident(test)) {
        return window;
    }
    auto path = ApplicationPath();
    auto args = QuoteArgument(path) + L" --resident --profile " + QuoteArgument(profile);
    if (test) {
        args += L" --test-host";
    }
    STARTUPINFOW startup{sizeof(startup)};
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION process{};
    Require(CreateProcessW(path.c_str(), args.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                           nullptr, nullptr, &startup, &process) != FALSE,
            "Cannot start resident controller");
    CloseHandle(process.hThread);
    WaitForInputIdle(process.hProcess, 5000);
    CloseHandle(process.hProcess);
    HWND window = FindResident(test);
    Require(window != nullptr, "Resident did not start");
    return window;
}

std::string ResidentReport(const std::wstring& profile, bool test) {
    HWND window = FindResident(test);
    auto desktop = FindDesktop(test);
    auto status = ReadResidentPatch(desktop);
    SIZE display = ReadDisplaySize(desktop.list, test);
    DWORD size = window ? static_cast<DWORD>(InspectResident(window, 1)) : 0;
    DWORD lease = window ? static_cast<DWORD>(InspectResident(window, 2)) : 0;
    if (status.active && status.leaseDeadline) {
        auto now = GetTickCount64();
        lease = status.leaseDeadline > now
                    ? static_cast<DWORD>((status.leaseDeadline - now + 999) / 1000)
                    : 0;
    } else if (!status.active) {
        lease = 0;
    }
    DWORD error = window ? static_cast<DWORD>(InspectResident(window, 3)) : 0;
    wchar_t message[512]{};
    if (window && error) {
        GetWindowTextW(window, message, 512);
    } else {
        wcscpy_s(message, status.message);
    }
    std::ostringstream text;
    text << "{\"running\":" << (window ? "true" : "false")
         << ",\"enabled\":" << (status.active ? "true" : "false")
         << ",\"automatic\":" << (size == 0 ? "true" : "false") << ",\"requestedSize\":" << size
         << ",\"trialSeconds\":" << lease << ",\"profile\":" << Json(profile)
         << ",\"displayWidth\":" << display.cx << ",\"displayHeight\":" << display.cy
         << ",\"recommendedSize\":" << CalculateAutomaticIconSize(display)
         << ",\"lastError\":" << error << ",\"message\":" << Json(message) << "}\n";
    return text.str();
}

std::wstring ProfileImage(const std::wstring& profile, const std::wstring& key) {
    UINT count =
        std::min<UINT>(MaxItems, GetPrivateProfileIntW(L"Images", L"Count", 0, profile.c_str()));
    for (UINT i = 0; i < count; ++i) {
        auto section = L"Image" + std::to_wstring(i);
        if (IniString(profile, section, L"Target") == key) {
            return IniString(profile, section, L"Path");
        }
    }
    return L"";
}

void WriteProfileImage(const std::wstring& profile, const std::wstring& key,
                       const std::wstring& image) {
    Require(!key.empty() && key.find_first_of(L"\r\n") == key.npos &&
                image.find_first_of(L"\r\n") == image.npos,
            "Invalid image assignment");
    if (!image.empty()) {
        DWORD attributes = GetFileAttributesW(image.c_str());
        Require(attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY),
                "Image file does not exist");
    }
    UINT count = GetPrivateProfileIntW(L"Images", L"Count", 0, profile.c_str());
    Require(count <= MaxItems, "Profile item limit exceeded");
    std::vector<std::pair<std::wstring, std::wstring>> overrides;
    for (UINT i = 0; i < count; ++i) {
        auto section = L"Image" + std::to_wstring(i);
        auto target = IniString(profile, section, L"Target");
        if (target != key) {
            overrides.emplace_back(target, IniString(profile, section, L"Path"));
        }
    }
    if (!image.empty()) {
        overrides.emplace_back(key, AbsolutePath(image));
    }
    Require(overrides.size() <= MaxItems, "Profile item limit exceeded");
    std::wstring text = L"[Images]\r\nCount=" + std::to_wstring(overrides.size()) + L"\r\n";
    for (size_t i = 0; i < overrides.size(); ++i) {
        text += L"[Image" + std::to_wstring(i) + L"]\r\nTarget=" + overrides[i].first +
                L"\r\nPath=" + overrides[i].second + L"\r\n";
    }
    if (GetFileAttributesW(profile.c_str()) != INVALID_FILE_ATTRIBUTES) {
        Require(CopyFileW(profile.c_str(), (profile + L".bak").c_str(), FALSE) != FALSE,
                "Cannot back up image assignments");
    }
    auto temporary = profile + L".tmp-" + std::to_wstring(GetCurrentProcessId());
    WriteUtf16(temporary, text);
    BOOL moved = MoveFileExW(temporary.c_str(), profile.c_str(),
                             MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    if (!moved) {
        DeleteFileW(temporary.c_str());
    }
    Require(moved != FALSE, "Cannot save image assignments");
}
