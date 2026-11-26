// SPDX-License-Identifier: MIT
// Copyright (c) 2026 sh4
// Development-only fake controller. Set DEKA_FAKE_ACTIVE=1 to report an active patch.
using System.Text;
using System.Text.Json;

var argv = args.ToList();
string? Value(string name)
{
    var i = argv.IndexOf(name);
    return i >= 0 && i + 1 < argv.Count ? argv[i + 1] : null;
}

var report = Value("--report");
if (report is null)
{
    return 2;
}
var active = Environment.GetEnvironmentVariable("DEKA_FAKE_ACTIVE") == "1";
var profile = Path.Combine(Path.GetTempPath(), "DekaDesktopIcon.fake.ini");
var status = new Dictionary<string, object?>
{
    ["error"] = 0, ["active"] = active, ["size"] = active ? 768 : 0, ["itemCount"] = 3,
    ["cachedCount"] = active ? 3 : 0, ["imageWidth"] = 768, ["imageHeight"] = 768,
    ["leaseDeadline"] = 0, ["restoreMatched"] = 0, ["profile"] = profile, ["message"] = "フェイク状態",
};

object result = argv.FirstOrDefault() switch
{
    "--resident-state" => new Dictionary<string, object?>
    {
        ["running"] = true, ["enabled"] = active, ["automatic"] = true, ["requestedSize"] = 0,
        ["trialSeconds"] = 0, ["profile"] = profile, ["displayWidth"] = 3840, ["displayHeight"] = 2160,
        ["recommendedSize"] = 768, ["lastError"] = 0, ["message"] = "",
    },
    "--items" => new Dictionary<string, object?>
    {
        ["items"] = new[]
        {
            new { key = "::{20D04FE0-3AEA-1069-A2D8-08002B30309D}", name = "PC", image = "" },
            new { key = "::{645FF040-5081-101B-9F08-00AA002F954E}", name = "ごみ箱", image = @"C:\Images\trash.png" },
            new { key = "C:\\Users\\Public\\Desktop\\メモ.txt", name = "メモ.txt", image = "" },
        },
    },
    "--status" or "--enable" or "--disable" or "--recover-layout" => status,
    "--set-image" => new Dictionary<string, object?> { ["error"] = 0, ["message"] = "保存しました" },
    _ => new Dictionary<string, object?> { ["error"] = 2, ["message"] = "unknown command" },
};

File.WriteAllText(report, JsonSerializer.Serialize(result), new UTF8Encoding(false));
return 0;
