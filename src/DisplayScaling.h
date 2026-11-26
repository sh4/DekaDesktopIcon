// SPDX-License-Identifier: MIT
// Copyright (c) 2026 sh4
#pragma once
#include "Win32Support.h"
#include <limits>

namespace deka {
constexpr int MinIconSize = 64;
constexpr int MaxManualIconSize = 1024;
constexpr int MaxAutomaticIconSize = 4096;
constexpr int IconSizeStep = 16;
constexpr int IconSpacingMarginX = 48;
constexpr int IconSpacingMarginY = 68;
constexpr int DesktopGridMarginX = 16;
constexpr int DesktopGridMarginY = 20;

struct DesktopGridCapacity {
    int columns = 0;
    int rows = 0;
    int total = 0;
};

inline bool IsUsableDisplaySize(SIZE size) {
    return size.cx > 0 && size.cy > 0;
}

inline bool IsSmallerDisplaySize(SIZE candidate, SIZE current) {
    const std::int64_t candidateArea = static_cast<std::int64_t>(candidate.cx) * candidate.cy;
    const std::int64_t currentArea = static_cast<std::int64_t>(current.cx) * current.cy;
    if (candidateArea != currentArea) {
        return candidateArea < currentArea;
    }
    const LONG candidateMinEdge = std::min(candidate.cx, candidate.cy);
    const LONG currentMinEdge = std::min(current.cx, current.cy);
    if (candidateMinEdge != currentMinEdge) {
        return candidateMinEdge < currentMinEdge;
    }
    if (candidate.cx != current.cx) {
        return candidate.cx < current.cx;
    }
    return candidate.cy < current.cy;
}

inline SIZE ChooseSmallestDisplaySize(const std::vector<SIZE>& displays) {
    SIZE smallest{};
    bool found = false;
    for (const auto& candidate : displays) {
        if (!IsUsableDisplaySize(candidate)) {
            continue;
        }
        if (!found || IsSmallerDisplaySize(candidate, smallest)) {
            smallest = candidate;
            found = true;
        }
    }
    return smallest;
}

namespace detail {
inline BOOL CALLBACK CollectMonitorWorkArea(HMONITOR monitor, HDC, LPRECT, LPARAM context) {
    auto* workAreas = reinterpret_cast<std::vector<SIZE>*>(context);
    MONITORINFO info{sizeof(info)};
    if (GetMonitorInfoW(monitor, &info)) {
        const RECT& work = info.rcWork;
        workAreas->push_back(SIZE{work.right - work.left, work.bottom - work.top});
    }
    return TRUE;
}
} // namespace detail

inline SIZE ReadDisplayViewSize(HWND list) {
    RECT rect{};
    // Include scrollbars: showing one must not look like a display resize.
    if (IsIconic(GetParent(list)) || !GetWindowRect(list, &rect)) {
        return {};
    }
    return SIZE{rect.right - rect.left, rect.bottom - rect.top};
}

inline SIZE ReadDisplaySize(HWND list, bool testHost) {
    if (testHost) {
        // The independent ListView acts as the display in regression tests.
        return ReadDisplayViewSize(list);
    }
    // Enumerate active physical monitor work areas (excluding taskbars and docked bars)
    // and select the smallest display by area so multi-monitor desktops never clip.
    std::vector<SIZE> workAreas;
    if (!EnumDisplayMonitors(nullptr, nullptr, detail::CollectMonitorWorkArea,
                             reinterpret_cast<LPARAM>(&workAreas))) {
        return {};
    }
    return ChooseSmallestDisplaySize(workAreas);
}

inline DesktopGridCapacity MeasureDesktopGridCapacity(SIZE display, int iconSize) {
    if (!IsUsableDisplaySize(display) || iconSize <= 0) {
        return {};
    }
    const std::int64_t usableWidth = static_cast<std::int64_t>(display.cx) - DesktopGridMarginX;
    const std::int64_t usableHeight = static_cast<std::int64_t>(display.cy) - DesktopGridMarginY;
    if (usableWidth <= 0 || usableHeight <= 0) {
        return {};
    }
    const std::int64_t spacingX = static_cast<std::int64_t>(iconSize) + IconSpacingMarginX;
    const std::int64_t spacingY = static_cast<std::int64_t>(iconSize) + IconSpacingMarginY;
    const std::int64_t columns = usableWidth / spacingX;
    const std::int64_t rows = usableHeight / spacingY;
    if (columns <= 0 || rows <= 0) {
        return {};
    }
    const std::int64_t maxInt = static_cast<std::int64_t>(std::numeric_limits<int>::max());
    const int clampedColumns = static_cast<int>(std::min(columns, maxInt));
    const int clampedRows = static_cast<int>(std::min(rows, maxInt));
    const std::int64_t total = static_cast<std::int64_t>(clampedColumns) * clampedRows;
    const int clampedTotal = static_cast<int>(std::min(total, maxInt));
    return DesktopGridCapacity{clampedColumns, clampedRows, clampedTotal};
}

inline int CalculateAutomaticIconSize(SIZE display) {
    // Choose the largest 16px-aligned icon size in [64, 4096] that fits at least 2 icons
    // in the desktop grid (spacing = size + 48 horizontally, size + 68 vertically,
    // origin/margins = 16 horizontally, 20 vertically — matching ArrangeDesktopItems and
    // SnapItems).
    //
    // Because both columns(size) and rows(size) are monotonically non-increasing in size,
    // scanning downward from 4096 to 64 in 16px steps selects:
    // - Exact capacity 2 (2x1 landscape or 1x2 portrait) on standard aspect ratios
    //   (16:9, 16:10, 3:2, 4:3, 21:9, and portrait equivalents), including 8K (3776px).
    // - Capacity 3 (3x1 or 1x3) on super-ultrawide/tall displays (such as 32:9) where
    //   a 2-icon size would overflow the shorter axis.
    // - Clamped bounds of 64px on very small/invalid displays and 4096px above 8K.
    if (!IsUsableDisplaySize(display)) {
        return MinIconSize;
    }
    for (int size = MaxAutomaticIconSize; size >= MinIconSize; size -= IconSizeStep) {
        if (MeasureDesktopGridCapacity(display, size).total >= 2) {
            return size;
        }
    }
    return MinIconSize;
}

inline int ScaleIconSize(int requestedSize, SIZE reference, SIZE current) {
    if (!IsUsableDisplaySize(reference) || !IsUsableDisplaySize(current)) {
        return requestedSize;
    }
    // Always scale from the application baseline; repeated RDP reconnects must not drift.
    double scale = std::min(double(current.cx) / reference.cx, double(current.cy) / reference.cy);
    double pixels =
        std::clamp(requestedSize * scale, double(MinIconSize), double(MaxManualIconSize));
    return (static_cast<int>(pixels + IconSizeStep / 2) / IconSizeStep) * IconSizeStep;
}
} // namespace deka
