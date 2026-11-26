// SPDX-License-Identifier: MIT
// Copyright (c) 2026 sh4
using System.Globalization;
using Microsoft.UI.Dispatching;
using Microsoft.UI.Xaml;

namespace DekaDesktopIcon.Settings;

internal sealed record CommandLineOptions(int? OwnerPid, string? SmokeTestReport, string NativePath,
                                           string? Profile, bool TestHost)
{
    public bool IsSmokeTest => SmokeTestReport is not null;

    public static CommandLineOptions Parse(IReadOnlyList<string> args)
    {
        int? owner = null;
        string? smoke = null;
        string? native = null;
        string? profile = null;
        bool testHost = false;
        for (var i = 0; i < args.Count; ++i)
        {
            var next = i + 1 < args.Count ? args[i + 1] : null;
            switch (args[i])
            {
                case "--owner-pid" when next is not null:
                    if (int.TryParse(next, NumberStyles.Integer, CultureInfo.InvariantCulture, out var pid) &&
                        pid > 0)
                    {
                        owner = pid;
                    }
                    ++i;
                    break;
                case "--smoke-test" when next is not null:
                    smoke = Path.GetFullPath(next);
                    ++i;
                    break;
                case "--native" when next is not null:
                    // Development override for the controller path.
                    native = Path.GetFullPath(next);
                    ++i;
                    break;
                case "--profile" when next is not null:
                    profile = Path.GetFullPath(next);
                    ++i;
                    break;
                case "--test-host":
                    testHost = true;
                    break;
            }
        }
        return new CommandLineOptions(owner, smoke, native ?? NativeBridge.DefaultExecutablePath(),
                                      profile, testHost);
    }
}

public static class Program
{
    internal static int ExitCode { get; set; }

    [STAThread]
    private static int Main(string[] args)
    {
        WinRT.ComWrappersSupport.InitializeComWrappers();
        var options = CommandLineOptions.Parse(args);
        Application.Start(callbackParams =>
        {
            var context = new DispatcherQueueSynchronizationContext(DispatcherQueue.GetForCurrentThread());
            SynchronizationContext.SetSynchronizationContext(context);
            var app = new App(options);
            GC.KeepAlive(app);
        });
        return ExitCode;
    }
}
