// SPDX-License-Identifier: MIT
// Copyright (c) 2026 sh4
using System.Diagnostics;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Markup;
using Microsoft.UI.Xaml.XamlTypeInfo;

namespace DekaDesktopIcon.Settings;

/// <summary>
/// Code-only WinUI 3 application. Supplies the WinUI controls metadata provider itself because no
/// XAML pages are compiled.
/// </summary>
public sealed partial class App : Application, IXamlMetadataProvider
{
    private readonly XamlControlsXamlMetaDataProvider controlsProvider = new();
    private readonly CommandLineOptions options;
    private MainWindow? window;
    private Process? owner;

    internal App(CommandLineOptions options)
    {
        this.options = options;
        UnhandledException += (_, e) =>
        {
            Debug.WriteLine(e.Exception);
            if (options.IsSmokeTest)
            {
                SmokeTest.WriteFailure(options.SmokeTestReport!, e.Exception);
                Program.ExitCode = 3;
                e.Handled = true;
                Exit();
            }
        };
    }

    protected override void OnLaunched(LaunchActivatedEventArgs args)
    {
        Resources.MergedDictionaries.Add(new XamlControlsResources());
        AppStyles.Merge(Resources);

        window = new MainWindow(options);
        window.Closed += (_, _) =>
        {
            owner?.Dispose();
            owner = null;
        };
        window.Activate();
        WatchOwner();
    }

    private void WatchOwner()
    {
        if (options.OwnerPid is not int pid)
        {
            return;
        }
        try
        {
            owner = Process.GetProcessById(pid);
            owner.EnableRaisingEvents = true;
            owner.Exited += (_, _) => window?.DispatcherQueue.TryEnqueue(() => window?.Close());
            if (owner.HasExited)
            {
                window?.Close();
            }
        }
        catch (Exception ex) when (ex is ArgumentException or InvalidOperationException or
                                   System.ComponentModel.Win32Exception)
        {
            // Owner already gone (or inaccessible): the settings window has no reason to stay.
            window?.Close();
        }
    }

    public IXamlType GetXamlType(Type type) => controlsProvider.GetXamlType(type);

    public IXamlType GetXamlType(string fullName) => controlsProvider.GetXamlType(fullName);

    public XmlnsDefinition[] GetXmlnsDefinitions() => controlsProvider.GetXmlnsDefinitions();
}
