// SPDX-License-Identifier: MIT
// Copyright (c) 2026 sh4
using System.ComponentModel;
using System.Diagnostics;
using System.Text;
using System.Text.Json;

namespace DekaDesktopIcon.Settings;

internal sealed class NativeBridgeException(string message, Exception? inner = null)
    : Exception(message, inner);

/// <summary>
/// Runs the parent controller (<c>..\DekaDesktopIcon.exe</c>) with a temporary JSON report file.
/// Never sends <c>--exit</c>; the resident keeps running after the settings window closes.
/// </summary>
internal sealed class NativeBridge(string executablePath, bool readOnly, string? profile = null,
                                   bool testHost = false)
{
    private static readonly TimeSpan CommandTimeout = TimeSpan.FromSeconds(15);

    public string ExecutablePath { get; } = executablePath;

    /// <summary>When set (smoke test), commands that change the desktop are refused.</summary>
    public bool ReadOnly { get; } = readOnly;

    public static string DefaultExecutablePath() =>
        Path.GetFullPath(Path.Combine(AppContext.BaseDirectory, "..", "DekaDesktopIcon.exe"));

    public async Task<ResidentState> GetResidentStateAsync() =>
        ResidentState.From(await RunAsync(["--resident-state"], mutating: false, checkError: false));

    public async Task<NativeStatus> GetStatusAsync() =>
        NativeStatus.From(await RunAsync(["--status"], mutating: false));

    public async Task<IReadOnlyList<DesktopItem>> GetItemsAsync() =>
        DesktopItem.ListFrom(await RunAsync(["--items"], mutating: false));

    public async Task<NativeStatus> EnableAsync(string size, bool trial) =>
        NativeStatus.From(await RunAsync(
            ["--enable", "--size", size, "--lease", trial ? "30" : "0"], mutating: true));

    public async Task<NativeStatus> DisableAsync() =>
        NativeStatus.From(await RunAsync(["--disable"], mutating: true));

    public async Task<string> SetImageAsync(string key, string imagePath)
    {
        var root = await RunAsync(["--set-image", key, "--image", imagePath], mutating: true);
        return root.GetString("message");
    }

    public async Task<NativeStatus> RecoverLayoutAsync() =>
        NativeStatus.From(await RunAsync(["--recover-layout"], mutating: true));

    private async Task<JsonElement> RunAsync(IReadOnlyList<string> arguments, bool mutating,
                                             bool checkError = true)
    {
        if (mutating && ReadOnly)
        {
            throw new NativeBridgeException("スモーク テスト中はデスクトップを変更する操作を実行しません。");
        }
        if (!File.Exists(ExecutablePath))
        {
            throw new NativeBridgeException($"DekaDesktopIcon.exe が見つかりません: {ExecutablePath}");
        }

        var report = Path.Combine(Path.GetTempPath(),
                                  $"DekaDesktopIcon.Settings-{Environment.ProcessId}-{Guid.NewGuid():N}.json");
        var info = new ProcessStartInfo(ExecutablePath)
        {
            UseShellExecute = false,
            CreateNoWindow = true,
            WorkingDirectory = Path.GetDirectoryName(ExecutablePath) ?? AppContext.BaseDirectory,
        };
        foreach (var argument in arguments)
        {
            info.ArgumentList.Add(argument);
        }
        if (profile is not null)
        {
            info.ArgumentList.Add("--profile");
            info.ArgumentList.Add(profile);
        }
        if (testHost)
        {
            info.ArgumentList.Add("--test-host");
        }
        info.ArgumentList.Add("--report");
        info.ArgumentList.Add(report);

        try
        {
            using var process = new Process { StartInfo = info };
            try
            {
                process.Start();
            }
            catch (Win32Exception ex)
            {
                throw new NativeBridgeException($"DekaDesktopIcon.exe を起動できません: {ex.Message}", ex);
            }

            using var timeout = new CancellationTokenSource(CommandTimeout);
            try
            {
                await process.WaitForExitAsync(timeout.Token).ConfigureAwait(true);
            }
            catch (OperationCanceledException)
            {
                try
                {
                    process.Kill();
                }
                catch (InvalidOperationException)
                {
                }
                catch (Win32Exception)
                {
                }
                throw new NativeBridgeException(
                    $"DekaDesktopIcon.exe が {CommandTimeout.TotalSeconds:0} 秒以内に応答しませんでした。");
            }

            var root = await ReadReportAsync(report, process.ExitCode).ConfigureAwait(true);
            if (checkError)
            {
                var error = root.GetInt("error");
                if (error != 0)
                {
                    var message = root.GetString("message");
                    throw new NativeBridgeException(message.Length > 0 ? message : $"エラー コード {error}");
                }
            }
            return root;
        }
        finally
        {
            try
            {
                File.Delete(report);
            }
            catch (IOException)
            {
            }
            catch (UnauthorizedAccessException)
            {
            }
        }
    }

    private static async Task<JsonElement> ReadReportAsync(string path, int exitCode)
    {
        byte[] bytes;
        try
        {
            await using var stream = new FileStream(path, FileMode.Open, FileAccess.Read,
                                                    FileShare.ReadWrite | FileShare.Delete);
            using var buffer = new MemoryStream();
            await stream.CopyToAsync(buffer).ConfigureAwait(true);
            bytes = buffer.ToArray();
        }
        catch (Exception ex) when (ex is FileNotFoundException or DirectoryNotFoundException or IOException)
        {
            throw new NativeBridgeException($"結果を受け取れませんでした（終了コード {exitCode}）。", ex);
        }
        if (bytes.Length == 0)
        {
            throw new NativeBridgeException($"結果が空でした（終了コード {exitCode}）。");
        }

        string text;
        if (bytes is [0xFF, 0xFE, ..])
        {
            text = Encoding.Unicode.GetString(bytes, 2, bytes.Length - 2);
        }
        else if (bytes is [0xEF, 0xBB, 0xBF, ..])
        {
            text = Encoding.UTF8.GetString(bytes, 3, bytes.Length - 3);
        }
        else
        {
            text = Encoding.UTF8.GetString(bytes);
        }

        try
        {
            using var document = JsonDocument.Parse(text, new JsonDocumentOptions
            {
                AllowTrailingCommas = true,
                CommentHandling = JsonCommentHandling.Skip,
            });
            return document.RootElement.Clone();
        }
        catch (JsonException ex)
        {
            throw new NativeBridgeException($"結果の形式が正しくありません（終了コード {exitCode}）。", ex);
        }
    }
}
