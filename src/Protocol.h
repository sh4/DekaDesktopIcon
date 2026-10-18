// SPDX-License-Identifier: MIT
// Copyright (c) 2026 sh4
#pragma once
#include "Win32Support.h"

namespace deka {
constexpr DWORD Magic = 0x414B4544; // "DEKA" in the command mapping.
constexpr DWORD Version = 3;
constexpr UINT_PTR ParentSubclassId = 0x44444901;
constexpr UINT_PTR ListSubclassId = 0x44444902;
constexpr UINT_PTR TimerId = 0x44444903;
constexpr UINT TestDropTargetMessage = WM_APP + 0x445;
constexpr int MaxItems = 512;
enum class Command : DWORD {
    Apply = 1,
    Restore = 2,
    Query = 3,
    Capture = 4,
    InteractionReport = 5,
    CaptureDrag = 6,
    ValidateInteraction = 7,
    ValidateShellBehavior = 8,
    ShowContext = 9,
    ValidateDropFeedback = 10
};

struct SharedCommand {
    DWORD magic, version, bytes, targetPid;
    UINT64 listHwnd;
    DWORD command, requestedSize, leaseSeconds, testHost;
    wchar_t profile[1024];
    wchar_t capture[1024];
    wchar_t ownerModule[1024];
    volatile LONG ack;
    DWORD error, active, size, originalImageWidth, originalImageHeight;
    DWORD imageWidth, imageHeight, itemCount, cachedCount;
    UINT64 customPaintCount;
    DWORD lastDrawWidth, lastDrawHeight, sourceWidth, sourceHeight;
    DWORD nativeIconWidth, nativeIconHeight;
    LONG nativeHitAtCenter;
    DWORD restoreMatched, restoreMissing, restorePositionFailures;
    DWORD subclassInstalled, selectedCount;
    UINT64 leaseDeadline;
    wchar_t message[512];
};

inline std::wstring MappingName(DWORD pid) {
    return L"Local\\DekaDesktopIcon-v3-" + std::to_wstring(pid);
}

inline UINT CommandMessage() {
    return RegisterWindowMessageW(L"DekaDesktopIcon.Command.v3.20261017");
}

} // namespace deka
