// SPDX-License-Identifier: MIT
// Copyright (c) 2026 sh4
using System.Runtime.InteropServices;

namespace DekaDesktopIcon.Settings;

internal static class NativeMethods
{
    [DllImport("user32.dll")]
    public static extern uint GetDpiForWindow(IntPtr hwnd);

    [DllImport("shell32.dll", CharSet = CharSet.Unicode)]
    public static extern uint ExtractIconExW(string file, int index, out IntPtr large, out IntPtr small,
                                             uint count);

    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool DestroyIcon(IntPtr icon);
}
