// SPDX-License-Identifier: MIT
// Copyright (c) 2026 sh4
using System.Globalization;
using System.Text.Json;

namespace DekaDesktopIcon.Settings;

/// <summary>Result of <c>--resident-state</c>.</summary>
internal sealed record ResidentState(
    bool Running,
    bool Enabled,
    bool Automatic,
    int RequestedSize,
    int TrialSeconds,
    string Profile,
    int DisplayWidth,
    int DisplayHeight,
    int RecommendedSize,
    string LastError,
    string Message)
{
    public static ResidentState From(JsonElement e) => new(
        Running: e.GetBool("running"),
        Enabled: e.GetBool("enabled"),
        Automatic: e.GetBool("automatic") || e.GetInt("requestedSize") == 0,
        RequestedSize: e.GetInt("requestedSize"),
        TrialSeconds: e.GetInt("trialSeconds"),
        Profile: e.GetString("profile"),
        DisplayWidth: e.GetInt("displayWidth"),
        DisplayHeight: e.GetInt("displayHeight"),
        RecommendedSize: e.GetInt("recommendedSize"),
        LastError: NormalizeError(e.GetString("lastError")),
        Message: e.GetString("message"));

    private static string NormalizeError(string value) =>
        value is "" or "0" or "null" ? "" : value;
}

/// <summary>Usual status report written by --status/--enable/--disable/--recover-layout.</summary>
internal sealed record NativeStatus(
    int Error,
    bool Active,
    int Size,
    int ItemCount,
    int CachedCount,
    int CustomPaintCount,
    int ImageWidth,
    int ImageHeight,
    long LeaseDeadline,
    int RestoreMatched,
    int RestorePositionFailures,
    string Profile,
    string Message)
{
    public static NativeStatus From(JsonElement e) => new(
        Error: e.GetInt("error"),
        Active: e.GetBool("active"),
        Size: e.GetInt("size"),
        ItemCount: e.GetInt("itemCount"),
        CachedCount: e.GetInt("cachedCount"),
        CustomPaintCount: e.GetInt("customPaintCount"),
        ImageWidth: e.GetInt("imageWidth"),
        ImageHeight: e.GetInt("imageHeight"),
        LeaseDeadline: e.GetLong("leaseDeadline"),
        RestoreMatched: e.GetInt("restoreMatched"),
        RestorePositionFailures: e.GetInt("restorePositionFailures"),
        Profile: e.GetString("profile"),
        Message: e.GetString("message"));
}

/// <summary>Desktop item from --items.</summary>
internal sealed record DesktopItem(string Key, string Name, string Image)
{
    public static IReadOnlyList<DesktopItem> ListFrom(JsonElement root)
    {
        var list = new List<DesktopItem>();
        if (root.ValueKind == JsonValueKind.Object &&
            root.TryGetProperty("items", out var items) && items.ValueKind == JsonValueKind.Array)
        {
            foreach (var item in items.EnumerateArray())
            {
                var key = item.GetString("key");
                if (key.Length == 0)
                {
                    continue;
                }
                var name = item.GetString("name");
                list.Add(new DesktopItem(key, name.Length > 0 ? name : key, item.GetString("image")));
            }
        }
        return list;
    }
}

internal static class JsonExtensions
{
    public static string GetString(this JsonElement e, string name)
    {
        if (e.ValueKind != JsonValueKind.Object || !e.TryGetProperty(name, out var v))
        {
            return "";
        }
        return v.ValueKind switch
        {
            JsonValueKind.String => v.GetString() ?? "",
            JsonValueKind.Number => v.GetRawText(),
            JsonValueKind.True => "true",
            JsonValueKind.False => "false",
            _ => "",
        };
    }

    public static long GetLong(this JsonElement e, string name)
    {
        if (e.ValueKind != JsonValueKind.Object || !e.TryGetProperty(name, out var v))
        {
            return 0;
        }
        return v.ValueKind switch
        {
            JsonValueKind.Number when v.TryGetInt64(out var n) => n,
            JsonValueKind.Number when v.TryGetDouble(out var d) => (long)d,
            JsonValueKind.True => 1,
            JsonValueKind.String when long.TryParse(v.GetString(), NumberStyles.Integer,
                CultureInfo.InvariantCulture, out var s) => s,
            _ => 0,
        };
    }

    public static int GetInt(this JsonElement e, string name) =>
        (int)Math.Clamp(e.GetLong(name), int.MinValue, int.MaxValue);

    public static bool GetBool(this JsonElement e, string name)
    {
        if (e.ValueKind != JsonValueKind.Object || !e.TryGetProperty(name, out var v))
        {
            return false;
        }
        return v.ValueKind switch
        {
            JsonValueKind.True => true,
            JsonValueKind.Number => v.TryGetDouble(out var d) && d != 0,
            JsonValueKind.String => v.GetString() is "true" or "1",
            _ => false,
        };
    }
}
