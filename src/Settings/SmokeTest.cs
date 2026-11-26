// SPDX-License-Identifier: MIT
// Copyright (c) 2026 sh4
using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;
using System.Runtime.InteropServices.WindowsRuntime;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Media.Imaging;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using Windows.Graphics.Imaging;

namespace DekaDesktopIcon.Settings;

/// <summary>Writes the --smoke-test report. The smoke test never changes the desktop.</summary>
internal static class SmokeTest
{
    private static readonly JsonSerializerOptions Options = new() { WriteIndented = true };

    public static async Task RenderAsync(UIElement element, string path)
    {
        var panel = element as Panel;
        var background = panel?.Background;
        if (panel is not null)
        {
            // RenderTargetBitmap excludes the compositor's Mica backdrop.
            panel.Background = new SolidColorBrush(panel.ActualTheme == ElementTheme.Dark
                ? Windows.UI.Color.FromArgb(255, 32, 32, 32)
                : Windows.UI.Color.FromArgb(255, 243, 243, 243));
        }
        try
        {
            var bitmap = new RenderTargetBitmap();
            await bitmap.RenderAsync(element);
            var pixels = await bitmap.GetPixelsAsync();
            using var stream = File.Create(path);
            using var random = stream.AsRandomAccessStream();
            var encoder = await BitmapEncoder.CreateAsync(BitmapEncoder.PngEncoderId, random);
            encoder.SetPixelData(BitmapPixelFormat.Bgra8, BitmapAlphaMode.Premultiplied,
                                 (uint)bitmap.PixelWidth, (uint)bitmap.PixelHeight, 96, 96,
                                 pixels.ToArray());
            await encoder.FlushAsync();
        }
        finally
        {
            if (panel is not null)
            {
                panel.Background = background;
            }
        }
    }

    public static void Write(string path, JsonObject report)
    {
        var directory = Path.GetDirectoryName(path);
        if (!string.IsNullOrEmpty(directory))
        {
            Directory.CreateDirectory(directory);
        }
        File.WriteAllText(path, report.ToJsonString(Options), new UTF8Encoding(false));
    }

    public static void WriteFailure(string path, Exception exception)
    {
        try
        {
            Write(path, new JsonObject
            {
                ["ok"] = false,
                ["failure"] = exception.GetType().FullName,
                ["message"] = exception.Message,
            });
        }
        catch (IOException)
        {
        }
        catch (UnauthorizedAccessException)
        {
        }
    }
}
