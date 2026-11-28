// SPDX-License-Identifier: MIT
// Copyright (c) 2026 sh4
#pragma once
#include "Win32Support.h"
#include <cstddef>
#include <type_traits>

namespace deka {
constexpr DWORD Magic = 0x414B4544; // "DEKA" in the command mapping.
constexpr DWORD Version = 5;
constexpr LONG CommandComplete = 1, CommandDraining = 2;
constexpr WPARAM CommandDrainParam = 0x44524E31;
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
    FileIdentity ownerFile;
    UINT64 ownerBase;
    DWORD callbacksRetained;
};

static_assert(std::is_standard_layout_v<SharedCommand> && std::is_trivial_v<SharedCommand>,
              "The shared-memory protocol must remain a plain, fixed-layout record");

// v2/v3 used the same prefix without a stable owner file identity.
constexpr DWORD LegacyCommandBytes = static_cast<DWORD>(offsetof(SharedCommand, ownerFile));
constexpr DWORD Version4CommandBytes = static_cast<DWORD>(offsetof(SharedCommand, ownerBase));

inline std::wstring MappingName(DWORD pid) {
    return L"Local\\DekaDesktopIcon-v5-" + std::to_wstring(pid);
}

inline UINT CommandMessage() {
    return RegisterWindowMessageW(L"DekaDesktopIcon.Command.v5.20261128");
}

} // namespace deka
