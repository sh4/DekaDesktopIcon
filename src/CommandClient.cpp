// SPDX-License-Identifier: MIT
// Copyright (c) 2026 sh4
#include "CommandClient.h"
#include <shellapi.h>

namespace deka {
namespace {
void EnsurePreviousPatchIsInactive(const DesktopTarget& desktop) {
    // The pre-rename build uses another mapping name. Do not stack subclasses
    // over that active patch just because DekaDesktopIcon has a new identity.
    auto name = L"Local\\ExplorerIconPatch-v2-" + std::to_wstring(desktop.pid);
    HANDLE mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, name.c_str());
    if (!mapping) {
        Require(GetLastError() == ERROR_FILE_NOT_FOUND, "Cannot inspect previous patch state");
        return;
    }
    auto previous = static_cast<const SharedCommand*>(
        MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, sizeof(SharedCommand)));
    bool active = previous && previous->magic == 0x50494945 && previous->version == 2 &&
                  previous->bytes == sizeof(SharedCommand) && previous->targetPid == desktop.pid &&
                  previous->active;
    bool readable = previous != nullptr;
    if (previous) {
        UnmapViewOfFile(previous);
    }
    CloseHandle(mapping);
    Require(readable, "Cannot read previous patch state");
    Require(!active, "The previous tool is active. Restore it using its original folder before "
                     "using DekaDesktopIcon");
}
} // namespace

void ValidateTarget(const DesktopTarget& desktop) {
    Require(desktop.list && desktop.parent && desktop.thread && desktop.pid,
            "Native desktop SysListView32 not found");
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, desktop.pid);
    Require(process != nullptr, "Cannot inspect target process");
    wchar_t name[32768]{};
    DWORD length = 32768;
    BOOL ok = QueryFullProcessImageNameW(process, 0, name, &length);
    CloseHandle(process);
    Require(ok != FALSE, "Cannot read target image path");
    std::wstring expected;
    if (desktop.test) {
        expected = ParentPath(ModulePath()) + L"\\DekaDesktopIconTestHost.exe";
    } else {
        wchar_t windows[32768]{};
        GetWindowsDirectoryW(windows, 32768);
        expected = std::wstring(windows) + L"\\explorer.exe";
    }
    Require(_wcsicmp(expected.c_str(), name) == 0,
            "Target is not this session's Windows Explorer or bundled test host");
    DWORD ourSession = 0, targetSession = 0;
    ProcessIdToSessionId(GetCurrentProcessId(), &ourSession);
    ProcessIdToSessionId(desktop.pid, &targetSession);
    Require(ourSession == targetSession, "Target session mismatch");
}

SharedCommand SendCommand(const DesktopTarget& desktop, Command command, DWORD size, DWORD lease,
                          const std::wstring& profile, const std::wstring& capture) {
    ValidateTarget(desktop);
    EnsurePreviousPatchIsInactive(desktop);
    HANDLE mutex =
        CreateMutexW(nullptr, FALSE, (MappingName(desktop.pid) + L"-controller").c_str());
    Require(mutex != nullptr, "Controller mutex unavailable");
    DWORD wait = WaitForSingleObject(mutex, 10000);
    if (wait != WAIT_OBJECT_0 && wait != WAIT_ABANDONED) {
        CloseHandle(mutex);
        throw std::runtime_error("Another controller is busy");
    }
    HANDLE map = nullptr;
    SharedCommand* memory = nullptr;
    HMODULE dll = nullptr;
    HHOOK hook = nullptr;
    try {
        map = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                                 sizeof(SharedCommand), MappingName(desktop.pid).c_str());
        Require(map != nullptr, "Cannot create command mapping");
        DWORD existed = GetLastError();
        memory = static_cast<SharedCommand*>(
            MapViewOfFile(map, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SharedCommand)));
        Require(memory != nullptr, "Cannot map command block");
        if (existed != ERROR_ALREADY_EXISTS) {
            ZeroMemory(memory, sizeof(*memory));
            memory->magic = Magic;
            memory->version = Version;
            memory->bytes = sizeof(SharedCommand);
            memory->targetPid = desktop.pid;
        }
        Require(memory->magic == Magic && memory->version == Version &&
                    memory->bytes == sizeof(SharedCommand),
                "Patch protocol mismatch");
        std::wstring dllPath = ParentPath(ModulePath()) + L"\\DekaDesktopIconCore.dll";
        Require(dllPath.size() < 1024, "Patch DLL path is too long");
        Require(!memory->active || _wcsicmp(memory->ownerModule, dllPath.c_str()) == 0,
                "Another copy is active. Restore using the controller from that same folder first");
        memory->listHwnd = reinterpret_cast<UINT64>(desktop.list);
        memory->command = static_cast<DWORD>(command);
        memory->requestedSize = size;
        memory->leaseSeconds = lease;
        memory->testHost = desktop.test;
        Require(profile.size() < 1024 && capture.size() < 1024, "Configuration path is too long");
        wcscpy_s(memory->profile, profile.c_str());
        wcscpy_s(memory->capture, capture.c_str());
        InterlockedExchange(&memory->ack, 0);
        dll = LoadLibraryExW(dllPath.c_str(), nullptr,
                             LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
        Require(dll != nullptr, "Cannot load DekaDesktopIconCore.dll next to controller");
        auto proc = reinterpret_cast<HOOKPROC>(GetProcAddress(dll, "DesktopHook"));
        Require(proc != nullptr, "Hook export missing");
        hook = SetWindowsHookExW(WH_GETMESSAGE, proc, dll, desktop.thread);
        Require(hook != nullptr, "Target-thread DLL hook failed");
        Require(PostMessageW(desktop.list, CommandMessage(), 0, 0) != FALSE,
                "Cannot post Explorer command");
        UINT64 deadline = GetTickCount64() + 12000;
        while (memory->ack != 1 && GetTickCount64() < deadline) {
            MsgWaitForMultipleObjectsEx(0, nullptr, 10, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
            MSG pending{};
            while (PeekMessageW(&pending, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&pending);
                DispatchMessageW(&pending);
            }
        }
        Require(memory->ack == 1,
                "Explorer did not acknowledge command; a timed patch will still auto-restore");
        SharedCommand response = *memory;
        UnhookWindowsHookEx(hook);
        hook = nullptr;
        FreeLibrary(dll);
        dll = nullptr;
        UnmapViewOfFile(memory);
        memory = nullptr;
        CloseHandle(map);
        map = nullptr;
        ReleaseMutex(mutex);
        CloseHandle(mutex);
        return response;
    } catch (...) {
        if (hook) {
            UnhookWindowsHookEx(hook);
        }
        if (dll) {
            FreeLibrary(dll);
        }
        if (memory) {
            UnmapViewOfFile(memory);
        }
        if (map) {
            CloseHandle(map);
        }
        ReleaseMutex(mutex);
        CloseHandle(mutex);
        throw;
    }
}

DWORD RecoverLayout(const std::wstring& profile) {
    std::wstring backup = profile + L".rollback.ini";
    Require(GetFileAttributesW(backup.c_str()) != INVALID_FILE_ATTRIBUTES,
            "Saved original layout was not found");
    UINT count = GetPrivateProfileIntW(L"Original", L"Count", MaxItems + 1, backup.c_str());
    Require(count <= MaxItems, "Saved layout is invalid");
    auto desktop = FindDesktop();
    auto items = ReadDesktopItems(desktop.list, false);
    auto view = GetDesktopFolderView();
    Require(view != nullptr, "Desktop view unavailable");
    DWORD recovered = 0;
    for (UINT i = 0; i < count; ++i) {
        auto section = L"Item" + std::to_wstring(i);
        auto key = IniString(backup, section, L"Key");
        auto found = std::find_if(items.begin(), items.end(), [&](const Item& item) {
            return item.key == key;
        });
        if (found == items.end()) {
            continue;
        }
        POINT point{static_cast<LONG>(std::stol(IniString(backup, section, L"X"))),
                    static_cast<LONG>(std::stol(IniString(backup, section, L"Y")))};
        PITEMID_CHILD pidl = nullptr;
        Require(SUCCEEDED(view->Item(static_cast<int>(found - items.begin()), &pidl)),
                "Cannot resolve recovery item");
        PCUITEMID_CHILD child = pidl;
        HRESULT result =
            view->SelectAndPositionItems(1, &child, &point, SVSI_POSITIONITEM | SVSI_NOTAKEFOCUS);
        CoTaskMemFree(pidl);
        Require(SUCCEEDED(result), "Cannot restore saved item position");
        recovered++;
    }
    return recovered;
}

} // namespace deka
