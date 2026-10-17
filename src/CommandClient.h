// SPDX-License-Identifier: MIT
// Copyright (c) 2026 sh4
#pragma once
#include "ShellDesktop.h"

namespace deka {
void ValidateTarget(const DesktopTarget& desktop);
SharedCommand SendCommand(const DesktopTarget& desktop, Command command, DWORD size, DWORD lease,
                          const std::wstring& profile, const std::wstring& capture = L"");
DWORD RecoverLayout(const std::wstring& profile);
} // namespace deka
