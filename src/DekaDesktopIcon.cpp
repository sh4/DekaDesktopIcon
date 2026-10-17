// SPDX-License-Identifier: MIT
// Copyright (c) 2026 sh4
#include "CommandClient.h"
#include <commdlg.h>
#include <shellapi.h>

#include <sstream>

using namespace deka;

namespace {
std::wstring Utf16(const std::string& value) {
    int n = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, nullptr, 0);
    std::wstring result(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, result.data(), n);
    if (!result.empty()) {
        result.pop_back();
    }
    return result;
}

std::string Utf8(const std::wstring& value) {
    int n = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr,
                                0, nullptr, nullptr);
    std::string result(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), n,
                        nullptr, nullptr);
    return result;
}

std::string Json(const std::wstring& value) {
    std::string result = "\"";
    for (char c : Utf8(value)) {
        if (c == '\\' || c == '"') {
            result += '\\';
            result += c;
        } else if (c == '\n') {
            result += "\\n";
        } else if (c == '\r') {
            result += "\\r";
        } else if (static_cast<unsigned char>(c) < 32) {
            result += ' ';
        } else {
            result += c;
        }
    }
    return result + '"';
}

std::string Report(const DesktopTarget& desktop, const SharedCommand& status) {
    std::ostringstream s;
    s << "{\n  \"pid\": " << desktop.pid << ",\n  \"thread\": " << desktop.thread
      << ",\n  \"targetClass\": " << Json(GetWindowClassName(desktop.list))
      << ",\n  \"parentClass\": " << Json(GetWindowClassName(desktop.parent))
      << ",\n  \"testHost\": " << (desktop.test ? "true" : "false")
      << ",\n  \"error\": " << status.error
      << ",\n  \"active\": " << (status.active ? "true" : "false")
      << ",\n  \"size\": " << status.size
      << ",\n  \"originalImageWidth\": " << status.originalImageWidth
      << ",\n  \"originalImageHeight\": " << status.originalImageHeight
      << ",\n  \"imageWidth\": " << status.imageWidth
      << ",\n  \"imageHeight\": " << status.imageHeight
      << ",\n  \"itemCount\": " << status.itemCount
      << ",\n  \"cachedCount\": " << status.cachedCount
      << ",\n  \"customPaintCount\": " << status.customPaintCount
      << ",\n  \"lastDrawWidth\": " << status.lastDrawWidth
      << ",\n  \"lastDrawHeight\": " << status.lastDrawHeight
      << ",\n  \"sourceWidth\": " << status.sourceWidth
      << ",\n  \"sourceHeight\": " << status.sourceHeight
      << ",\n  \"nativeIconWidth\": " << status.nativeIconWidth
      << ",\n  \"nativeIconHeight\": " << status.nativeIconHeight
      << ",\n  \"nativeHitAtCenter\": " << status.nativeHitAtCenter
      << ",\n  \"restoreMatched\": " << status.restoreMatched
      << ",\n  \"restoreMissing\": " << status.restoreMissing
      << ",\n  \"restorePositionFailures\": " << status.restorePositionFailures
      << ",\n  \"subclassInstalled\": " << status.subclassInstalled
      << ",\n  \"selectedCount\": " << status.selectedCount
      << ",\n  \"leaseDeadline\": " << status.leaseDeadline
      << ",\n  \"patchModule\": " << Json(status.ownerModule)
      << ",\n  \"profile\": " << Json(status.profile)
      << ",\n  \"message\": " << Json(status.message) << "\n}\n";
    return s.str();
}

void SaveReport(const std::wstring& path, const std::string& text) {
    if (path.empty()) {
        return;
    }
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    Require(file != INVALID_HANDLE_VALUE, "Cannot create report file");
    DWORD written = 0;
    BOOL ok = WriteFile(file, text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
    CloseHandle(file);
    Require(ok && written == text.size(), "Cannot write report");
}

HWND gui = nullptr, sizeCombo = nullptr, itemCombo = nullptr, imageEdit = nullptr,
     statusEdit = nullptr, timedCheck = nullptr;
std::vector<Item> guiItems;
std::wstring profilePath;
HFONT guiFont = nullptr;
constexpr int ApplyId = 101, RestoreId = 102, StatusId = 103, BrowseId = 104, SaveImageId = 105,
              ItemId = 106, RecoverId = 107;

void Text(HWND handle, const std::wstring& text) {
    SetWindowTextW(handle, text.c_str());
}

std::wstring GetText(HWND handle) {
    int n = GetWindowTextLengthW(handle);
    std::wstring text(n + 1, 0);
    GetWindowTextW(handle, text.data(), n + 1);
    text.resize(n);
    return text;
}

void ShowStatus(const SharedCommand& s) {
    std::wstring text = s.message;
    text += L"\r\n\r\nExplorer 内パッチ: " + std::wstring(s.active ? L"適用中" : L"解除済み");
    text += L"\r\nネイティブ画像リスト: " + std::to_wstring(s.imageWidth) + L" × " +
            std::to_wstring(s.imageHeight) + L" px";
    text += L"\r\n項目: " + std::to_wstring(s.itemCount) + L" / 画像キャッシュ: " +
            std::to_wstring(s.cachedCount) + L" / 独自描画: " + std::to_wstring(s.customPaintCount);
    text += L"\r\n最後の描画領域: " + std::to_wstring(s.lastDrawWidth) + L" × " +
            std::to_wstring(s.lastDrawHeight) + L" px";
    if (s.leaseDeadline && s.active) {
        text += L"\r\n時間制限付き。30 秒経過すると自動解除します。";
    }
    if (!s.active && s.restoreMatched) {
        text += L"\r\n配置復元: " + std::to_wstring(s.restoreMatched) +
                L" 件 / 復元位置の不一致: " + std::to_wstring(s.restorePositionFailures);
    }
    if (s.error) {
        text += L"\r\nエラー: " + std::to_wstring(s.error);
    }
    Text(statusEdit, text);
}

void WriteProfileImage() {
    int selected = static_cast<int>(SendMessageW(itemCombo, CB_GETCURSEL, 0, 0));
    Require(selected >= 0 && selected < static_cast<int>(guiItems.size()), "Select a desktop item");
    std::wstring image = GetText(imageEdit);
    Require(image.empty() || GetFileAttributesW(image.c_str()) != INVALID_FILE_ATTRIBUTES,
            "Image file does not exist");
    UINT count = GetPrivateProfileIntW(L"Images", L"Count", 0, profilePath.c_str());
    Require(count <= MaxItems, "Profile item limit exceeded");
    std::vector<std::pair<std::wstring, std::wstring>> overrides;
    for (UINT i = 0; i < count; ++i) {
        auto section = L"Image" + std::to_wstring(i);
        auto target = IniString(profilePath, section, L"Target");
        if (target != guiItems[selected].key) {
            overrides.emplace_back(target, IniString(profilePath, section, L"Path"));
        }
    }
    if (!image.empty()) {
        overrides.emplace_back(guiItems[selected].key, image);
    }
    std::wstring text = L"[Images]\r\nCount=" + std::to_wstring(overrides.size()) + L"\r\n";
    for (size_t i = 0; i < overrides.size(); ++i) {
        text += L"[Image" + std::to_wstring(i) + L"]\r\nTarget=" + overrides[i].first +
                L"\r\nPath=" + overrides[i].second + L"\r\n";
    }
    if (GetFileAttributesW(profilePath.c_str()) != INVALID_FILE_ATTRIBUTES) {
        CopyFileW(profilePath.c_str(), (profilePath + L".bak").c_str(), FALSE);
    }
    WriteUtf16(profilePath, text);
    Text(statusEdit, L"画像の割り当てを保存しました。「適用」で Explorer "
                     L"に反映します。\r\n画像パスを空にして保存すると標準画像に戻ります。");
}

HWND Control(const wchar_t* kind, const wchar_t* text, DWORD style, int x, int y, int width,
             int height, int id = 0) {
    HWND control =
        CreateWindowExW((wcscmp(kind, L"EDIT") == 0 ? WS_EX_CLIENTEDGE : 0), kind, text,
                        WS_CHILD | WS_VISIBLE | style, x, y, width, height, gui,
                        reinterpret_cast<HMENU>(INT_PTR(id)), GetModuleHandleW(nullptr), nullptr);
    SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(guiFont), TRUE);
    return control;
}

LRESULT CALLBACK GuiProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_CREATE) {
        gui = hwnd;
        guiFont = CreateFontW(-18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                              OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                              DEFAULT_PITCH, L"Yu Gothic UI");
        Control(L"STATIC", L"DekaDesktopIcon", 0, 24, 18, 680, 32);
        Control(L"STATIC",
                L"Explorer 内のネイティブ項目の描画を変更します。通常の起動・右クリックは Explorer "
                L"が処理します。",
                0, 24, 55, 680, 52);
        Control(L"STATIC", L"画像の表示領域（物理 px）", 0, 24, 115, 260, 28);
        sizeCombo = Control(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 300, 110,
                            170, 220);
        for (int size : {256, 512, 768, 1024}) {
            SendMessageW(sizeCombo, CB_ADDSTRING, 0,
                         reinterpret_cast<LPARAM>(std::to_wstring(size).c_str()));
        }
        SendMessageW(sizeCombo, CB_SETCURSEL, 3, 0);
        timedCheck = Control(L"BUTTON", L"30 秒後に自動解除して試す", BS_AUTOCHECKBOX | WS_TABSTOP,
                             24, 155, 410, 30);
        SendMessageW(timedCheck, BM_SETCHECK, BST_CHECKED, 0);
        Control(L"BUTTON", L"Explorer に適用", BS_DEFPUSHBUTTON | WS_TABSTOP, 24, 202, 210, 42,
                ApplyId);
        Control(L"BUTTON", L"解除・元の配置へ復元", WS_TABSTOP, 248, 202, 246, 42, RestoreId);
        Control(L"BUTTON", L"状態を更新", WS_TABSTOP, 510, 202, 180, 42, StatusId);
        Control(L"STATIC", L"高解像度画像を割り当てるデスクトップ項目", 0, 24, 268, 650, 28);
        itemCombo = Control(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 24, 301,
                            666, 260, ItemId);
        try {
            auto desktop = FindDesktop();
            if (desktop.list) {
                guiItems = ReadDesktopItems(desktop.list, false);
            }
        } catch (...) {
        }
        for (const auto& item : guiItems) {
            SendMessageW(itemCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(item.name.c_str()));
        }
        if (!guiItems.empty()) {
            SendMessageW(itemCombo, CB_SETCURSEL, 0, 0);
        }
        imageEdit = Control(L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP, 24, 345, 514, 30);
        Control(L"BUTTON", L"画像を選択…", WS_TABSTOP, 550, 341, 140, 38, BrowseId);
        Control(L"BUTTON", L"画像の割り当てを保存", WS_TABSTOP, 24, 390, 260, 38, SaveImageId);
        Control(L"STATIC", L"鮮明な 1024 px 描画には、1024 px 以上の PNG を指定してください。", 0,
                300, 391, 390, 48);
        Control(L"BUTTON", L"保存済みの元の配置を復旧", WS_TABSTOP, 24, 450, 300, 34, RecoverId);
        statusEdit =
            Control(L"EDIT",
                    L"「適用」で Explorer に DLL "
                    L"を読み込みます。\r\n解除すると元の画像リスト・間隔・配置に戻ります。",
                    ES_MULTILINE | ES_READONLY | WS_VSCROLL, 24, 497, 666, 143);
        Control(L"STATIC",
                L"非公式の実行時パッチです。システムファイルは書き換えません。Explorer "
                L"の再起動で解除されます。",
                0, 24, 653, 666, 48);
        return 0;
    }
    if (message == WM_COMMAND) {
        try {
            int id = LOWORD(wParam);
            if (id == ItemId && HIWORD(wParam) == CBN_SELCHANGE) {
                int selected = static_cast<int>(SendMessageW(itemCombo, CB_GETCURSEL, 0, 0));
                std::wstring image;
                if (selected >= 0 && selected < static_cast<int>(guiItems.size())) {
                    UINT count =
                        std::min<UINT>(MaxItems, GetPrivateProfileIntW(L"Images", L"Count", 0,
                                                                       profilePath.c_str()));
                    for (UINT i = 0; i < count; ++i) {
                        auto section = L"Image" + std::to_wstring(i);
                        if (IniString(profilePath, section, L"Target") == guiItems[selected].key) {
                            image = IniString(profilePath, section, L"Path");
                        }
                    }
                }
                Text(imageEdit, image);
                return 0;
            }
            if (id == BrowseId) {
                wchar_t path[32768]{};
                OPENFILENAMEW open{sizeof(open)};
                open.hwndOwner = hwnd;
                open.lpstrFile = path;
                open.nMaxFile = 32768;
                open.lpstrFilter =
                    L"画像 "
                    L"(PNG/JPEG/BMP/"
                    L"ICO)\0*.png;*.jpg;*.jpeg;*.bmp;*.ico\0すべてのファイル\0*.*\0\0";
                open.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
                if (GetOpenFileNameW(&open)) {
                    Text(imageEdit, path);
                }
            } else if (id == SaveImageId) {
                WriteProfileImage();
            } else if (id == RecoverId) {
                auto current = SendCommand(FindDesktop(), Command::Query, 1024, 0, profilePath);
                Require(!current.active,
                        "Restore the active patch before recovering saved positions");
                DWORD count = RecoverLayout(profilePath);
                Text(statusEdit,
                     L"保存済みの元の配置を " + std::to_wstring(count) + L" 件復旧しました。");
            } else if (id == ApplyId || id == RestoreId || id == StatusId) {
                int choice = static_cast<int>(SendMessageW(sizeCombo, CB_GETCURSEL, 0, 0));
                int sizes[] = {256, 512, 768, 1024};
                Require(choice >= 0 && choice < 4, "Select a size");
                DWORD lease = SendMessageW(timedCheck, BM_GETCHECK, 0, 0) == BST_CHECKED ? 30 : 0;
                auto result = SendCommand(FindDesktop(),
                                          id == ApplyId     ? Command::Apply
                                          : id == RestoreId ? Command::Restore
                                                            : Command::Query,
                                          sizes[choice], lease, profilePath);
                ShowStatus(result);
            }
        } catch (const std::exception& ex) {
            Text(statusEdit, Utf16(ex.what()));
        }
        return 0;
    }
    if (message == WM_DESTROY) {
        if (guiFont) {
            DeleteObject(guiFont);
        }
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

int RunGui(HINSTANCE instance) {
    profilePath = DefaultProfile();
    WNDCLASSW wc{};
    wc.lpfnWndProc = GuiProc;
    wc.hInstance = instance;
    wc.lpszClassName = L"DekaDesktopIcon.Controller";
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassW(&wc);
    HWND hwnd =
        CreateWindowExW(0, wc.lpszClassName, L"DekaDesktopIcon",
                        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, CW_USEDEFAULT,
                        CW_USEDEFAULT, 740, 750, nullptr, nullptr, instance, nullptr);
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(hwnd, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    return 0;
}
} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    INITCOMMONCONTROLSEX cc{sizeof(cc), ICC_LISTVIEW_CLASSES};
    InitCommonControlsEx(&cc);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    int exitCode = 0;
    std::wstring report;
    try {
        if (argc == 1) {
            exitCode = RunGui(instance);
        } else {
            Command command = Command::Query;
            bool test = false, probe = false, listItems = false, recover = false;
            DWORD size = 1024, lease = 0;
            std::wstring profile = DefaultProfile(), capture;
            for (int i = 1; i < argc; ++i) {
                std::wstring arg = argv[i];
                auto value = [&]() -> std::wstring {
                    Require(i + 1 < argc, "Missing argument value");
                    return argv[++i];
                };
                if (arg == L"--apply") {
                    command = Command::Apply;
                } else if (arg == L"--show-context-menu") {
                    command = Command::ShowContext;
                    auto kind = value();
                    Require(kind == L"background" || kind == L"item" || kind == L"keyboard",
                            "Context target must be background, item, or keyboard");
                    size = kind == L"item" ? 1 : kind == L"keyboard" ? 2 : 0;
                } else if (arg == L"--restore") {
                    command = Command::Restore;
                } else if (arg == L"--status") {
                    command = Command::Query;
                } else if (arg == L"--capture") {
                    command = Command::Capture;
                    capture = value();
                } else if (arg == L"--capture-drag") {
                    command = Command::CaptureDrag;
                    capture = value();
                } else if (arg == L"--interaction-report") {
                    command = Command::InteractionReport;
                    capture = value();
                } else if (arg == L"--validate-interaction") {
                    command = Command::ValidateInteraction;
                    capture = value();
                } else if (arg == L"--validate-shell-behavior") {
                    command = Command::ValidateShellBehavior;
                    capture = value();
                } else if (arg == L"--size") {
                    size = static_cast<DWORD>(std::stoul(value()));
                } else if (arg == L"--lease") {
                    lease = static_cast<DWORD>(std::stoul(value()));
                } else if (arg == L"--report") {
                    report = value();
                } else if (arg == L"--profile") {
                    profile = value();
                } else if (arg == L"--test-host") {
                    test = true;
                } else if (arg == L"--probe") {
                    probe = true;
                } else if (arg == L"--items") {
                    listItems = true;
                } else if (arg == L"--recover-layout") {
                    recover = true;
                } else {
                    throw std::runtime_error("Unknown option");
                }
            }
            DesktopTarget desktop = FindDesktop(test);
            ValidateTarget(desktop);
            if (listItems) {
                Require(!test, "External listing of the test host is unsupported");
                auto items = ReadDesktopItems(desktop.list, false);
                std::string output = "{\"items\": [";
                for (size_t i = 0; i < items.size(); ++i) {
                    if (i) {
                        output += ',';
                    }
                    output +=
                        "{\"key\":" + Json(items[i].key) + ",\"name\":" + Json(items[i].name) + "}";
                }
                SaveReport(report, output + "]}\n");
                LocalFree(argv);
                if (SUCCEEDED(com)) {
                    CoUninitialize();
                }
                return 0;
            }
            SharedCommand status{};
            if (probe) {
                status.itemCount = ListView_GetItemCount(desktop.list);
                wcscpy_s(status.message, L"読み取りのみ。DLL は読み込んでいません。");
            } else {
                status = SendCommand(desktop, command, size, lease, profile, capture);
            }
            if (recover) {
                Require(!test && !status.active,
                        "Restore the active desktop patch before layout recovery");
                status.restoreMatched = RecoverLayout(profile);
                wcscpy_s(status.message, L"保存済みの配置を復旧しました。");
            }
            SaveReport(report, Report(desktop, status));
            exitCode = status.error ? 1 : 0;
        }
    } catch (const std::exception& ex) {
        exitCode = 2;
        if (!report.empty()) {
            SaveReport(report, "{\"error\": 2, \"message\": " + Json(Utf16(ex.what())) + "}\n");
        } else {
            MessageBoxW(nullptr, Utf16(ex.what()).c_str(), L"DekaDesktopIcon",
                        MB_OK | MB_ICONERROR);
        }
    }
    LocalFree(argv);
    if (SUCCEEDED(com)) {
        CoUninitialize();
    }
    return exitCode;
}
