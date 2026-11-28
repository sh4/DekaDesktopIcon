// SPDX-License-Identifier: MIT
// Copyright (c) 2026 sh4
#include "CommandClient.h"
#include "DisplayScaling.h"
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
      << ",\n  \"patchFileVolume\": " << status.ownerFile.volume
      << ",\n  \"patchFileIndexHigh\": " << status.ownerFile.indexHigh
      << ",\n  \"patchFileIndexLow\": " << status.ownerFile.indexLow
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

#include "ResidentController.h"
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
            exitCode = RunResident(instance, DefaultProfile(), false);
        } else {
            Command command = Command::Query;
            bool test = false, probe = false, listItems = false, recover = false;
            bool runResident = false, showSettings = false, residentState = false;
            bool enable = false, disable = false, quit = false, setImage = false;
            bool sizeSpecified = false;
            DWORD size = 1024, lease = 0;
            std::wstring profile = DefaultProfile(), capture, imageKey, imagePath;
            for (int i = 1; i < argc; ++i) {
                std::wstring arg = argv[i];
                auto value = [&]() -> std::wstring {
                    Require(i + 1 < argc, "Missing argument value");
                    return argv[++i];
                };
                if (arg == L"--apply") {
                    command = Command::Apply;
                } else if (arg == L"--resident") {
                    runResident = true;
                } else if (arg == L"--settings") {
                    showSettings = true;
                } else if (arg == L"--resident-state") {
                    residentState = true;
                } else if (arg == L"--enable") {
                    enable = true;
                } else if (arg == L"--disable") {
                    disable = true;
                } else if (arg == L"--exit") {
                    quit = true;
                } else if (arg == L"--set-image") {
                    setImage = true;
                    imageKey = value();
                } else if (arg == L"--image") {
                    imagePath = value();
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
                } else if (arg == L"--validate-drop-feedback") {
                    command = Command::ValidateDropFeedback;
                    capture = value();
                } else if (arg == L"--validate-shell-behavior") {
                    command = Command::ValidateShellBehavior;
                    capture = value();
                } else if (arg == L"--size") {
                    auto choice = value();
                    size = choice == L"auto" ? 0 : static_cast<DWORD>(std::stoul(choice));
                    sizeSpecified = true;
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
            profile = AbsolutePath(profile);
            if (enable && !sizeSpecified) {
                size = 0;
            }
            if (runResident) {
                exitCode = RunResident(instance, profile, test);
            } else if (showSettings) {
                ResidentRequest request;
                request.action = ResidentAction::Settings;
                Require(RequestResident(EnsureResident(profile, test), request),
                        "Cannot open settings");
            } else if (residentState) {
                SaveReport(report, ResidentReport(profile, test));
            } else if (enable || disable || quit) {
                Require(ValidRequestedSize(size) && lease <= 3600,
                        "Invalid size or trial duration");
                HWND window = enable ? EnsureResident(profile, test) : FindResident(test);
                Require(window != nullptr, "Resident controller is not running");
                ResidentRequest request;
                request.action = enable ? ResidentAction::Enable
                                 : quit ? ResidentAction::Exit
                                        : ResidentAction::Disable;
                request.size = size;
                request.lease = lease;
                // Exit destroys the resident and its status handle. Keep the
                // result mapping alive until this caller has copied the reply.
                auto desktop = FindDesktop(test);
                HANDLE reply = desktop.pid ? OpenFileMappingW(FILE_MAP_READ, FALSE,
                                                              MappingName(desktop.pid).c_str())
                                           : nullptr;
                bool accepted = RequestResident(window, request);
                desktop = FindDesktop(test);
                auto status = ReadResidentPatch(desktop);
                if (reply) {
                    CloseHandle(reply);
                }
                if (!accepted) {
                    status.error = static_cast<DWORD>(InspectResident(window, 3));
                    if (!status.error) {
                        status.error = ERROR_GEN_FAILURE;
                    }
                    GetWindowTextW(window, status.message, 512);
                    if (!status.message[0]) {
                        wcscpy_s(status.message, L"常駐アプリが操作を完了できませんでした。");
                    }
                }
                SaveReport(report, Report(desktop, status));
                exitCode = accepted ? 0 : 1;
            } else if (setImage) {
                WriteProfileImage(profile, imageKey, imagePath);
                SaveReport(report,
                           "{\"error\":0,\"message\":\"画像の割り当てを保存しました。\"}\n");
            } else {
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
                        output += "{\"key\":" + Json(items[i].key) +
                                  ",\"name\":" + Json(items[i].name) +
                                  ",\"image\":" + Json(ProfileImage(profile, items[i].key)) + "}";
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
