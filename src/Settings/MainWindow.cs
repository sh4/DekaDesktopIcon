// SPDX-License-Identifier: MIT
// Copyright (c) 2026 sh4
using System.Text.Json.Nodes;
using Microsoft.UI;
using Microsoft.UI.Dispatching;
using Microsoft.UI.Windowing;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Automation;
using Microsoft.UI.Xaml.Automation.Peers;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using Microsoft.UI.Xaml.Media;
using Microsoft.UI.Xaml.Media.Imaging;
using Windows.Graphics;
using Windows.Storage.Pickers;
using Windows.UI;
using VirtualKey = Windows.System.VirtualKey;
using VirtualKeyModifiers = Windows.System.VirtualKeyModifiers;
using WinRT.Interop;

namespace DekaDesktopIcon.Settings;

public sealed partial class MainWindow : Window
{
    private const double CompactWidth = 640;
    private static readonly string[] SizeTags = ["auto", "256", "512", "768", "1024"];

    private readonly CommandLineOptions options;
    private readonly NativeBridge bridge;
    private readonly SemaphoreSlim gate = new(1, 1);
    private readonly DispatcherQueueTimer refreshTimer;
    private readonly DispatcherQueueTimer countdownTimer;
    private readonly List<SettingRow> rows = [];
    private readonly Dictionary<string, string> pendingImages = new(StringComparer.Ordinal);
    private readonly List<string> loadErrors = [];
    private IntPtr windowIcon;

    // Model.
    private ResidentState? state;
    private NativeStatus? status;
    private bool? isActive;
    private IReadOnlyList<DesktopItem> items = [];
    private string? editingKey;
    private DateTimeOffset? trialEndsAt;
    private bool busy;
    private bool loaded;
    private bool suppressEditorEvents;
    private int previewVersion;

    // Controls.
    private Grid root = null!;
    private Grid titleBar = null!;
    private ScrollViewer pageScroller = null!;
    private ProgressBar progress = null!;
    private InfoBar infoBar = null!;
    private Border badge = null!;
    private FontIcon badgeIcon = null!;
    private TextBlock badgeText = null!;
    private TextBlock headline = null!;
    private TextBlock detail = null!;
    private TextBlock trialText = null!;
    private TextBlock messageText = null!;
    private Button refreshButton = null!;
    private ComboBox sizeCombo = null!;
    private ComboBoxItem autoSizeItem = null!;
    private TextBlock displayText = null!;
    private ToggleSwitch trialSwitch = null!;
    private Button applyButton = null!;
    private Button disableButton = null!;
    private ComboBox itemCombo = null!;
    private TextBox imageBox = null!;
    private Button browseButton = null!;
    private Button saveImageButton = null!;
    private Button clearImageButton = null!;
    private Image preview = null!;
    private FontIcon previewPlaceholder = null!;
    private TextBlock unsavedText = null!;
    private Button recoverButton = null!;
    private TextBlock recoverHint = null!;
    private TextBlock profileText = null!;

    internal MainWindow(CommandLineOptions options)
    {
        this.options = options;
        bridge = new NativeBridge(options.NativePath, readOnly: options.IsSmokeTest,
                                  profile: options.Profile, testHost: options.TestHost);

        Title = "DekaDesktopIcon 設定";
        SystemBackdrop = new MicaBackdrop { Kind = Microsoft.UI.Composition.SystemBackdrops.MicaKind.Base };
        ExtendsContentIntoTitleBar = true;
        Content = BuildContent();
        SetTitleBar(titleBar);
        ConfigureAppWindow();

        refreshTimer = DispatcherQueue.CreateTimer();
        refreshTimer.Interval = TimeSpan.FromSeconds(5);
        refreshTimer.Tick += (_, _) => _ = RefreshAsync(background: true);
        countdownTimer = DispatcherQueue.CreateTimer();
        countdownTimer.Interval = TimeSpan.FromSeconds(1);
        countdownTimer.Tick += (_, _) => UpdateTrialCountdown();

        root.Loaded += OnLoaded;
        Closed += (_, _) =>
        {
            // Closing exits only the settings companion. The native resident keeps running.
            refreshTimer.Stop();
            countdownTimer.Stop();
            if (windowIcon != IntPtr.Zero)
            {
                NativeMethods.DestroyIcon(windowIcon);
                windowIcon = IntPtr.Zero;
            }
        };
    }

    // ───────────────────────────── UI construction ─────────────────────────────

    private Grid BuildContent()
    {
        root = new Grid { Language = "ja-JP", Background = new SolidColorBrush(Colors.Transparent) };
        root.RowDefinitions.Add(new RowDefinition { Height = new GridLength(48) });
        root.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
        root.RowDefinitions.Add(new RowDefinition { Height = new GridLength(1, GridUnitType.Star) });

        // Title bar.
        titleBar = new Grid { Background = new SolidColorBrush(Colors.Transparent) };
        var titleStack = new StackPanel
        {
            Orientation = Orientation.Horizontal,
            Spacing = 12,
            Margin = new Thickness(16, 0, 0, 0),
            VerticalAlignment = VerticalAlignment.Center,
        };
        titleStack.Children.Add(new FontIcon { Glyph = "", FontSize = 16 });
        titleStack.Children.Add(new TextBlock
        {
            Text = "DekaDesktopIcon 設定",
            Style = (Style)Application.Current.Resources["CaptionTextBlockStyle"],
            VerticalAlignment = VerticalAlignment.Center,
        });
        titleBar.Children.Add(titleStack);
        root.Children.Add(titleBar);

        progress = new ProgressBar
        {
            IsIndeterminate = true,
            Visibility = Visibility.Collapsed,
            VerticalAlignment = VerticalAlignment.Bottom,
        };
        AutomationProperties.SetName(progress, "処理中");
        root.Children.Add(progress);

        // Inline results stay pinned above the scroll area so they're always visible.
        infoBar = new InfoBar
        {
            IsOpen = false,
            IsClosable = true,
            Margin = new Thickness(0, 4, 0, 4),
            MaxWidth = 1000,
        };
        var infoHost = new Grid { Padding = new Thickness(36, 0, 36, 0), Children = { infoBar } };
        Grid.SetRow(infoHost, 1);
        root.Children.Add(infoHost);

        var page = new StackPanel
        {
            MaxWidth = 1000,
            Padding = new Thickness(36, 4, 36, 36),
            HorizontalAlignment = HorizontalAlignment.Stretch,
        };
        var pageTitle = new TextBlock
        {
            Text = "DekaDesktopIcon",
            Style = (Style)Application.Current.Resources["TitleTextBlockStyle"],
            Margin = new Thickness(2, 8, 0, 2),
        };
        AutomationProperties.SetHeadingLevel(pageTitle, AutomationHeadingLevel.Level1);
        page.Children.Add(pageTitle);
        var pageSubtitle = new TextBlock
        {
            Text = "デスクトップ アイコンを大きく、鮮明に表示します",
            Style = AppStyles.Get(AppStyles.Secondary),
            FontSize = 14,
            LineHeight = 20,
            Margin = new Thickness(2, 0, 0, 20),
        };
        page.Children.Add(pageSubtitle);

        page.Children.Add(BuildStatusCard());
        page.Children.Add(SettingRow.SectionHeader("表示"));
        page.Children.Add(BuildSizeCard());
        page.Children.Add(SettingRow.SectionHeader("項目ごとの画像"));
        page.Children.Add(BuildImageCard());
        page.Children.Add(SettingRow.SectionHeader("配置"));
        page.Children.Add(BuildLayoutCard());
        page.Children.Add(SettingRow.SectionHeader("詳細"));
        page.Children.Add(BuildProfileCard());

        pageScroller = new ScrollViewer
        {
            Content = page,
            HorizontalScrollMode = ScrollMode.Disabled,
            HorizontalScrollBarVisibility = ScrollBarVisibility.Disabled,
            VerticalScrollBarVisibility = ScrollBarVisibility.Auto,
            IsTabStop = false,
        };
        AutomationProperties.SetName(pageScroller, "設定");
        Grid.SetRow(pageScroller, 2);
        root.Children.Add(pageScroller);

        root.SizeChanged += (_, e) => ApplyCompact(e.NewSize.Width < CompactWidth);
        root.ActualThemeChanged += (_, _) => UpdateTitleBarColors();
        return root;
    }

    private Border BuildStatusCard()
    {
        badgeIcon = new FontIcon { Glyph = "", Style = AppStyles.Get(AppStyles.BadgeIconInactive) };
        AutomationProperties.SetAccessibilityView(badgeIcon, AccessibilityView.Raw);
        badgeText = new TextBlock { Text = "確認中", Style = AppStyles.Get(AppStyles.BadgeTextInactive) };
        badge = new Border
        {
            Style = AppStyles.Get(AppStyles.BadgeInactive),
            HorizontalAlignment = HorizontalAlignment.Left,
            Child = new StackPanel
            {
                Orientation = Orientation.Horizontal,
                Spacing = 6,
                Children = { badgeIcon, badgeText },
            },
        };
        headline = new TextBlock
        {
            Text = "状態を確認しています…",
            Style = (Style)Application.Current.Resources["SubtitleTextBlockStyle"],
            TextWrapping = TextWrapping.Wrap,
            Margin = new Thickness(0, 8, 0, 0),
        };
        AutomationProperties.SetLiveSetting(headline, AutomationLiveSetting.Polite);
        detail = new TextBlock
        {
            Style = (Style)Application.Current.Resources["BodyTextBlockStyle"],
            TextWrapping = TextWrapping.Wrap,
            Margin = new Thickness(0, 4, 0, 0),
        };
        trialText = new TextBlock
        {
            Style = AppStyles.Get(AppStyles.AccentText),
            Visibility = Visibility.Collapsed,
            Margin = new Thickness(0, 4, 0, 0),
        };
        messageText = new TextBlock
        {
            Style = AppStyles.Get(AppStyles.Secondary),
            Visibility = Visibility.Collapsed,
            Margin = new Thickness(0, 6, 0, 0),
            IsTextSelectionEnabled = true,
        };
        var body = new StackPanel { Children = { badge, headline, detail, trialText, messageText } };

        refreshButton = MakeButton("状態を更新", "", "R");
        refreshButton.KeyboardAccelerators.Add(new KeyboardAccelerator { Key = VirtualKey.F5 });
        ToolTipService.SetToolTip(refreshButton, "状態を更新 (F5)");
        refreshButton.Click += async (_, _) => await RefreshAsync(background: false);

        var row = new SettingRow("", body, refreshButton);
        row.Root.Padding = new Thickness(20, 20, 20, 20);
        rows.Add(row);
        return SettingRow.Card(row.Root);
    }

    private Border BuildSizeCard()
    {
        autoSizeItem = new ComboBoxItem { Content = "自動", Tag = SizeTags[0] };
        sizeCombo = new ComboBox { MinWidth = 220, AccessKey = "Z" };
        sizeCombo.Items.Add(autoSizeItem);
        foreach (var tag in SizeTags.Skip(1))
        {
            sizeCombo.Items.Add(new ComboBoxItem { Content = $"{tag} px", Tag = tag });
        }
        sizeCombo.SelectedIndex = 0;
        AutomationProperties.SetName(sizeCombo, "アイコンのサイズ");
        var sizeRow = new SettingRow("",
            SettingRow.TextBody("アイコンのサイズ", "ディスプレイ情報を取得しています…", out displayText),
            sizeCombo);
        AutomationProperties.SetHelpText(sizeCombo, "画像の基準サイズを物理ピクセルで選びます");

        trialSwitch = new ToggleSwitch { OnContent = "オン", OffContent = "オフ", AccessKey = "T", MinWidth = 0 };
        AutomationProperties.SetName(trialSwitch, "30 秒間だけ試す");
        var trialRow = new SettingRow("",
            SettingRow.TextBody("30 秒間だけ試す", "適用から 30 秒後に、自動で標準の表示に戻ります", out _),
            trialSwitch);

        applyButton = MakeButton("適用", "", "A", accent: true);
        applyButton.Click += async (_, _) => await ApplyAsync();
        disableButton = MakeButton("無効にする", "", "D");
        disableButton.Click += async (_, _) => await DisableAsync();
        var buttons = new StackPanel
        {
            Orientation = Orientation.Horizontal,
            Spacing = 8,
            Children = { applyButton, disableButton },
        };
        var applyRow = new SettingRow("",
            SettingRow.TextBody("Explorer に反映", "選んだサイズでデスクトップ アイコンを表示します", out _),
            buttons);

        rows.AddRange([sizeRow, trialRow, applyRow]);
        return SettingRow.Card(sizeRow.Root, trialRow.Root, applyRow.Root);
    }

    private Border BuildImageCard()
    {
        itemCombo = new ComboBox
        {
            MinWidth = 260,
            MaxWidth = 380,
            AccessKey = "I",
            PlaceholderText = "項目がありません",
        };
        AutomationProperties.SetName(itemCombo, "デスクトップ項目");
        itemCombo.SelectionChanged += (_, _) => OnItemSelectionChanged();
        var itemRow = new SettingRow("",
            SettingRow.TextBody("デスクトップ項目", "高解像度の画像を割り当てる項目を選びます", out _),
            itemCombo);

        // Editor.
        preview = new Image { Stretch = Stretch.Uniform, Margin = new Thickness(8) };
        AutomationProperties.SetName(preview, "画像のプレビュー");
        previewPlaceholder = new FontIcon { Glyph = "", FontSize = 28, Opacity = 0.6 };
        AutomationProperties.SetAccessibilityView(previewPlaceholder, AccessibilityView.Raw);
        var previewFrame = new Border
        {
            Style = AppStyles.Get(AppStyles.Preview),
            VerticalAlignment = VerticalAlignment.Top,
            Child = new Grid { Children = { previewPlaceholder, preview } },
        };

        imageBox = new TextBox
        {
            Header = "画像ファイル",
            PlaceholderText = "標準のアイコンを使用",
            IsSpellCheckEnabled = false,
            AccessKey = "F",
        };
        AutomationProperties.SetName(imageBox, "画像ファイルのパス");
        imageBox.TextChanged += (_, _) => OnImageTextChanged();
        browseButton = MakeButton("参照…", "", "B");
        browseButton.VerticalAlignment = VerticalAlignment.Bottom;
        browseButton.Click += async (_, _) => await BrowseAsync();
        var pathGrid = new Grid { ColumnSpacing = 8 };
        pathGrid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
        pathGrid.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
        Grid.SetColumn(browseButton, 1);
        pathGrid.Children.Add(imageBox);
        pathGrid.Children.Add(browseButton);

        var hint = new TextBlock
        {
            Text = "PNG・JPEG・BMP・ICO。縦横比を保って表示します。",
            Style = AppStyles.Get(AppStyles.Secondary),
        };
        unsavedText = new TextBlock
        {
            Style = AppStyles.Get(AppStyles.AccentText),
            FontSize = 12,
            Visibility = Visibility.Collapsed,
        };
        AutomationProperties.SetLiveSetting(unsavedText, AutomationLiveSetting.Polite);

        saveImageButton = MakeButton("保存", "", "S", accent: true);
        saveImageButton.KeyboardAccelerators.Add(new KeyboardAccelerator
        {
            Key = VirtualKey.S,
            Modifiers = VirtualKeyModifiers.Control,
        });
        ToolTipService.SetToolTip(saveImageButton, "画像の割り当てを保存 (Ctrl+S)");
        saveImageButton.Click += async (_, _) => await SaveImageAsync(clear: false);
        clearImageButton = MakeButton("割り当てを解除", "", "C");
        clearImageButton.Click += async (_, _) => await SaveImageAsync(clear: true);
        var editorButtons = new StackPanel
        {
            Orientation = Orientation.Horizontal,
            Spacing = 8,
            Children = { saveImageButton, clearImageButton },
        };

        var editorStack = new StackPanel
        {
            Spacing = 8,
            Children = { pathGrid, hint, unsavedText, editorButtons },
        };
        var editor = new Grid { Padding = new Thickness(16, 16, 16, 16), ColumnSpacing = 16 };
        editor.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
        editor.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
        Grid.SetColumn(editorStack, 1);
        editor.Children.Add(previewFrame);
        editor.Children.Add(editorStack);

        rows.Add(itemRow);
        return SettingRow.Card(itemRow.Root, editor);
    }

    private Border BuildLayoutCard()
    {
        recoverButton = MakeButton("配置を復旧", "", "L");
        recoverButton.Click += async (_, _) => await RecoverAsync();
        var body = SettingRow.TextBody("元の配置を復旧", "保存済みのアイコン配置に戻します", out _);
        recoverHint = new TextBlock
        {
            Text = "拡大を無効にすると実行できます",
            Style = AppStyles.Get(AppStyles.Secondary),
            Visibility = Visibility.Collapsed,
        };
        body.Children.Add(recoverHint);
        var row = new SettingRow("", body, recoverButton);
        rows.Add(row);
        return SettingRow.Card(row.Root);
    }

    private Border BuildProfileCard()
    {
        var body = SettingRow.TextBody("画像プロファイル", "", out profileText);
        profileText.Text = "—";
        profileText.Visibility = Visibility.Visible;
        profileText.IsTextSelectionEnabled = true;
        AutomationProperties.SetName(profileText, "画像プロファイルのパス");
        var row = new SettingRow("", body, null);
        rows.Add(row);
        return SettingRow.Card(row.Root);
    }

    private static Button MakeButton(string text, string glyph, string accessKey, bool accent = false)
    {
        var button = new Button
        {
            Content = new StackPanel
            {
                Orientation = Orientation.Horizontal,
                Spacing = 8,
                Children =
                {
                    new FontIcon { Glyph = glyph, FontSize = 14 },
                    new TextBlock { Text = text },
                },
            },
            AccessKey = accessKey,
            MinWidth = 96,
        };
        AutomationProperties.SetName(button, text);
        if (accent)
        {
            button.Style = (Style)Application.Current.Resources["AccentButtonStyle"];
        }
        return button;
    }

    private void ApplyCompact(bool compact)
    {
        foreach (var row in rows)
        {
            row.SetCompact(compact);
        }
    }

    private void ConfigureAppWindow()
    {
        var hwnd = WindowNative.GetWindowHandle(this);
        var scale = Math.Max(1.0, NativeMethods.GetDpiForWindow(hwnd) / 96.0);
        var width = (int)(880 * scale);
        var height = (int)(940 * scale);
        var area = DisplayArea.GetFromWindowId(AppWindow.Id, DisplayAreaFallback.Nearest);
        if (area is not null)
        {
            var work = area.WorkArea;
            width = Math.Min(width, work.Width);
            height = Math.Min(height, work.Height);
            AppWindow.MoveAndResize(new RectInt32(
                work.X + (work.Width - width) / 2, work.Y + (work.Height - height) / 2, width, height));
        }
        else
        {
            AppWindow.Resize(new SizeInt32(width, height));
        }
        if (AppWindow.Presenter is OverlappedPresenter presenter)
        {
            presenter.PreferredMinimumWidth = (int)(480 * scale);
            presenter.PreferredMinimumHeight = (int)(480 * scale);
        }
        AppWindow.TitleBar.PreferredHeightOption = TitleBarHeightOption.Tall;

        if (File.Exists(options.NativePath) &&
            NativeMethods.ExtractIconExW(options.NativePath, 0, out var large, out var small, 1) > 0)
        {
            if (small != IntPtr.Zero)
            {
                NativeMethods.DestroyIcon(small);
            }
            if (large != IntPtr.Zero)
            {
                windowIcon = large;
                AppWindow.SetIcon(Win32Interop.GetIconIdFromIcon(large));
            }
        }
        UpdateTitleBarColors();
    }

    private void UpdateTitleBarColors()
    {
        var bar = AppWindow.TitleBar;
        var dark = root.ActualTheme == ElementTheme.Dark;
        var foreground = dark ? Colors.White : Colors.Black;
        bar.ButtonBackgroundColor = Colors.Transparent;
        bar.ButtonInactiveBackgroundColor = Colors.Transparent;
        bar.ButtonForegroundColor = foreground;
        bar.ButtonHoverForegroundColor = foreground;
        bar.ButtonPressedForegroundColor = foreground;
        bar.ButtonHoverBackgroundColor = dark ? Color.FromArgb(0x15, 0xFF, 0xFF, 0xFF)
                                              : Color.FromArgb(0x0F, 0x00, 0x00, 0x00);
        bar.ButtonPressedBackgroundColor = dark ? Color.FromArgb(0x0B, 0xFF, 0xFF, 0xFF)
                                                : Color.FromArgb(0x0A, 0x00, 0x00, 0x00);
    }

    // ───────────────────────────── Operations ─────────────────────────────

    private async void OnLoaded(object sender, RoutedEventArgs e)
    {
        root.Loaded -= OnLoaded;
        ApplyCompact(root.ActualWidth > 0 && root.ActualWidth < CompactWidth);
        UpdateView();
        await LoadInitialAsync();
        sizeCombo.Focus(FocusState.Programmatic);
        pageScroller.ChangeView(null, 0, null, disableAnimation: true);
        if (options.IsSmokeTest)
        {
            await FinishSmokeTestAsync();
            return;
        }
        refreshTimer.Start();
        countdownTimer.Start();
    }

    /// <summary>
    /// Serializes native operations. Background refreshes skip if anything is running and do not
    /// disable controls (so keyboard focus is never stolen); user operations wait their turn.
    /// </summary>
    private async Task RunExclusiveAsync(Func<Task> body, bool background = false)
    {
        if (background)
        {
            if (!await gate.WaitAsync(0))
            {
                return;
            }
        }
        else
        {
            await gate.WaitAsync();
        }

        var focused = background ? null : FocusManager.GetFocusedElement(root.XamlRoot) as Control;
        if (!background)
        {
            busy = true;
            progress.Visibility = Visibility.Visible;
            UpdateEnabledStates();
        }
        try
        {
            await body();
        }
        catch (NativeBridgeException ex)
        {
            if (!background || infoBar.Message != ex.Message || !infoBar.IsOpen)
            {
                ShowInfo(InfoBarSeverity.Error, ex.Message);
            }
        }
        catch (Exception ex)
        {
            // Unexpected failures are reported inline too; the UI always returns to an enabled state.
            ShowInfo(InfoBarSeverity.Error, ex.Message);
        }
        finally
        {
            if (!background)
            {
                busy = false;
                progress.Visibility = Visibility.Collapsed;
            }
            gate.Release();
            UpdateView();
            if (focused is not null && focused.IsEnabled && focused.XamlRoot is not null)
            {
                focused.Focus(FocusState.Keyboard);
            }
        }
    }

    private Task LoadInitialAsync() => RunExclusiveAsync(async () =>
    {
        loadErrors.Clear();
        try
        {
            state = await bridge.GetResidentStateAsync();
            isActive = state.Enabled;
            ApplyStateToInputs(state);
            if (state.Enabled && state.TrialSeconds > 0)
            {
                trialEndsAt = DateTimeOffset.Now.AddSeconds(state.TrialSeconds);
            }
        }
        catch (NativeBridgeException ex)
        {
            loadErrors.Add(ex.Message);
        }
        try
        {
            SetItems(await bridge.GetItemsAsync());
        }
        catch (NativeBridgeException ex)
        {
            loadErrors.Add(ex.Message);
        }
        try
        {
            status = await bridge.GetStatusAsync();
            isActive = status.Active;
        }
        catch (NativeBridgeException ex)
        {
            loadErrors.Add(ex.Message);
        }
        loaded = true;
        var distinct = loadErrors.Distinct().ToList();
        if (distinct.Count > 0)
        {
            ShowInfo(InfoBarSeverity.Error, string.Join(Environment.NewLine, distinct));
        }
    });

    private Task RefreshAsync(bool background) => RunExclusiveAsync(async () =>
    {
        state = await bridge.GetResidentStateAsync();
        isActive = state.Enabled;
        status = await bridge.GetStatusAsync();
        isActive = status.Active;
        if (isActive != true)
        {
            trialEndsAt = null;
        }
        else if (trialEndsAt is null && state.TrialSeconds > 0)
        {
            trialEndsAt = DateTimeOffset.Now.AddSeconds(state.TrialSeconds);
        }
        if (!background && infoBar.IsOpen && infoBar.Severity == InfoBarSeverity.Error)
        {
            infoBar.IsOpen = false;
        }
    }, background);

    private Task ApplyAsync() => RunExclusiveAsync(async () =>
    {
        var size = (sizeCombo.SelectedItem as ComboBoxItem)?.Tag as string ?? "auto";
        var trial = trialSwitch.IsOn;
        status = await bridge.EnableAsync(size, trial);
        isActive = status.Active;
        trialEndsAt = trial && status.Active ? DateTimeOffset.Now.AddSeconds(30) : null;
        await TryRefreshStateAsync();
        var sizeText = status.Size > 0 ? $"{status.Size} px" : size == "auto" ? "自動" : $"{size} px";
        ShowInfo(InfoBarSeverity.Success, trial
            ? $"{sizeText} で 30 秒間表示します。"
            : $"{sizeText} で表示しています。");
    });

    private Task DisableAsync() => RunExclusiveAsync(async () =>
    {
        status = await bridge.DisableAsync();
        isActive = status.Active;
        trialEndsAt = null;
        await TryRefreshStateAsync();
        var restored = status.RestoreMatched > 0 ? $"配置 {status.RestoreMatched} 件を元に戻しました。" : "";
        ShowInfo(InfoBarSeverity.Success, $"標準のアイコン表示に戻しました。{restored}");
    });

    private async Task SaveImageAsync(bool clear)
    {
        if (editingKey is not { } key)
        {
            return;
        }
        if (clear)
        {
            SetEditorText("");
        }
        var path = NormalizePath(imageBox.Text);
        if (path.Length > 0 && !File.Exists(path))
        {
            ShowInfo(InfoBarSeverity.Error, $"画像ファイルが見つかりません: {path}");
            imageBox.Focus(FocusState.Programmatic);
            return;
        }
        await RunExclusiveAsync(async () =>
        {
            await bridge.SetImageAsync(key, path);
            pendingImages.Remove(key);
            items = items.Select(i => i.Key == key ? i with { Image = path } : i).ToList();
            try
            {
                SetItems(await bridge.GetItemsAsync());
            }
            catch (NativeBridgeException)
            {
                SetItems(items);
            }

            var message = path.Length == 0 ? "画像の割り当てを解除しました。" : "画像の割り当てを保存しました。";
            if (isActive == true)
            {
                ShowInfo(InfoBarSeverity.Success, $"{message}再適用すると表示に反映されます。",
                         "今すぐ再適用", () => _ = ApplyAsync());
            }
            else
            {
                ShowInfo(InfoBarSeverity.Success, $"{message}次に適用したときに反映されます。");
            }
        });
    }

    private async Task RecoverAsync()
    {
        await RunExclusiveAsync(async () =>
        {
            var current = await bridge.GetStatusAsync();
            status = current;
            isActive = current.Active;
            if (current.Active)
            {
                ShowInfo(InfoBarSeverity.Informational,
                         "拡大が有効なため、配置を復旧できません。無効にしてから実行してください。",
                         "無効にする", () => _ = DisableAsync());
                return;
            }
            status = await bridge.RecoverLayoutAsync();
            isActive = status.Active;
            ShowInfo(InfoBarSeverity.Success, status.RestoreMatched > 0
                ? $"保存済みの配置を {status.RestoreMatched} 件復旧しました。"
                : status.Message.Length > 0 ? status.Message : "保存済みの配置を復旧しました。");
        });
    }

    private async Task TryRefreshStateAsync()
    {
        try
        {
            state = await bridge.GetResidentStateAsync();
        }
        catch (NativeBridgeException)
        {
            // The command result already reflects the outcome; state is refreshed on the next tick.
        }
    }

    private async Task BrowseAsync()
    {
        if (editingKey is null)
        {
            return;
        }
        var picker = new FileOpenPicker
        {
            ViewMode = PickerViewMode.Thumbnail,
            SuggestedStartLocation = PickerLocationId.PicturesLibrary,
        };
        foreach (var extension in new[] { ".png", ".jpg", ".jpeg", ".bmp", ".ico" })
        {
            picker.FileTypeFilter.Add(extension);
        }
        InitializeWithWindow.Initialize(picker, WindowNative.GetWindowHandle(this));
        try
        {
            var file = await picker.PickSingleFileAsync();
            if (file is not null && !string.IsNullOrEmpty(file.Path))
            {
                SetEditorText(file.Path);
                OnImageTextChanged();
                saveImageButton.Focus(FocusState.Programmatic);
            }
        }
        catch (Exception ex) when (ex is System.Runtime.InteropServices.COMException or UnauthorizedAccessException)
        {
            ShowInfo(InfoBarSeverity.Error, $"ファイルを選択できませんでした: {ex.Message}");
        }
    }

    // ───────────────────────────── Model → view ─────────────────────────────

    private void ApplyStateToInputs(ResidentState s)
    {
        var tag = s.Automatic || s.RequestedSize == 0 ? "auto" : s.RequestedSize.ToString(System.Globalization.CultureInfo.InvariantCulture);
        var index = Array.IndexOf(SizeTags, tag);
        sizeCombo.SelectedIndex = index >= 0 ? index : 0;
        trialSwitch.IsOn = s.Enabled && s.TrialSeconds > 0;
    }

    private void SetItems(IReadOnlyList<DesktopItem> list)
    {
        suppressEditorEvents = true;
        try
        {
            var keep = editingKey;
            items = list;
            itemCombo.Items.Clear();
            foreach (var item in items)
            {
                var entry = new ComboBoxItem { Content = ItemLabel(item), Tag = item.Key };
                AutomationProperties.SetName(entry, ItemLabel(item));
                itemCombo.Items.Add(entry);
            }
            var index = keep is null ? -1 : items.ToList().FindIndex(i => i.Key == keep);
            itemCombo.SelectedIndex = index >= 0 ? index : items.Count > 0 ? 0 : -1;
        }
        finally
        {
            suppressEditorEvents = false;
        }
        LoadEditor((itemCombo.SelectedItem as ComboBoxItem)?.Tag as string);
    }

    private string ItemLabel(DesktopItem item) =>
        pendingImages.ContainsKey(item.Key) ? $"{item.Name}（未保存）"
        : item.Image.Length > 0 ? $"{item.Name}（画像あり）"
        : item.Name;

    private string SavedImage(string key) => items.FirstOrDefault(i => i.Key == key)?.Image ?? "";

    private void OnItemSelectionChanged()
    {
        if (suppressEditorEvents)
        {
            return;
        }
        LoadEditor((itemCombo.SelectedItem as ComboBoxItem)?.Tag as string);
    }

    private void LoadEditor(string? key)
    {
        editingKey = key;
        SetEditorText(key is null ? "" : pendingImages.TryGetValue(key, out var pending) ? pending : SavedImage(key));
        UpdateEditorState();
    }

    private void SetEditorText(string text)
    {
        suppressEditorEvents = true;
        imageBox.Text = text;
        suppressEditorEvents = false;
    }

    private void OnImageTextChanged()
    {
        if (suppressEditorEvents || editingKey is not { } key)
        {
            UpdateEditorState();
            return;
        }
        var text = imageBox.Text;
        if (NormalizePath(text) == SavedImage(key))
        {
            pendingImages.Remove(key);
        }
        else
        {
            pendingImages[key] = text;
        }
        if (itemCombo.SelectedItem is ComboBoxItem entry && items.FirstOrDefault(i => i.Key == key) is { } item)
        {
            entry.Content = ItemLabel(item);
            AutomationProperties.SetName(entry, ItemLabel(item));
        }
        UpdateEditorState();
    }

    private void UpdateEditorState()
    {
        var dirty = editingKey is not null && pendingImages.ContainsKey(editingKey);
        var others = pendingImages.Keys.Count(k => k != editingKey && items.Any(i => i.Key == k));
        unsavedText.Text = (dirty, others) switch
        {
            (true, 0) => "未保存の変更があります",
            (true, _) => $"未保存の変更があります（ほかに {others} 件）",
            (false, > 0) => $"ほかの項目に未保存の変更が {others} 件あります",
            _ => "",
        };
        unsavedText.Visibility = unsavedText.Text.Length > 0 ? Visibility.Visible : Visibility.Collapsed;
        _ = UpdatePreviewAsync(NormalizePath(imageBox.Text));
        UpdateEnabledStates();
    }

    private async Task UpdatePreviewAsync(string path)
    {
        var version = ++previewVersion;
        if (path.Length == 0 || !File.Exists(path))
        {
            preview.Source = null;
            previewPlaceholder.Visibility = Visibility.Visible;
            return;
        }
        try
        {
            var bitmap = new BitmapImage { DecodePixelWidth = 160, DecodePixelType = DecodePixelType.Logical };
            await using var stream = File.OpenRead(path);
            using var buffer = new MemoryStream();
            await stream.CopyToAsync(buffer);
            buffer.Position = 0;
            await bitmap.SetSourceAsync(buffer.AsRandomAccessStream());
            if (version == previewVersion)
            {
                preview.Source = bitmap;
                previewPlaceholder.Visibility = Visibility.Collapsed;
            }
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or
                                   System.Runtime.InteropServices.COMException or ArgumentException)
        {
            if (version == previewVersion)
            {
                preview.Source = null;
                previewPlaceholder.Visibility = Visibility.Visible;
            }
        }
    }

    private void UpdateView()
    {
        var running = state?.Running ?? true;
        var active = isActive == true;
        var unavailable = loaded && state is null && status is null;

        if (!loaded)
        {
            SetBadge(AppStyles.BadgeInactive, AppStyles.BadgeTextInactive, AppStyles.BadgeIconInactive,
                     "", "確認中");
            headline.Text = "状態を確認しています…";
        }
        else if (unavailable)
        {
            SetBadge(AppStyles.BadgeError, AppStyles.BadgeTextError, AppStyles.BadgeIconError,
                     "", "取得できません");
            headline.Text = "DekaDesktopIcon に接続できません";
        }
        else if (!running)
        {
            SetBadge(AppStyles.BadgeError, AppStyles.BadgeTextError, AppStyles.BadgeIconError,
                     "", "停止中");
            headline.Text = "常駐プロセスが動作していません";
        }
        else if (active)
        {
            SetBadge(AppStyles.BadgeActive, AppStyles.BadgeTextActive, AppStyles.BadgeIconActive,
                     "", "有効");
            headline.Text = "大きなアイコンを表示しています";
        }
        else
        {
            SetBadge(AppStyles.BadgeInactive, AppStyles.BadgeTextInactive, AppStyles.BadgeIconInactive,
                     "", "無効");
            headline.Text = "標準のアイコンを表示しています";
        }
        AutomationProperties.SetName(badge, $"状態: {badgeText.Text}");

        detail.Text = BuildDetail(active);
        detail.Visibility = detail.Text.Length > 0 ? Visibility.Visible : Visibility.Collapsed;

        var message = status?.Message is { Length: > 0 } m ? m : state?.Message ?? "";
        if (state?.LastError is { Length: > 0 } lastError)
        {
            message = message.Length > 0 ? $"{message}\n直近のエラー: {lastError}" : $"直近のエラー: {lastError}";
        }
        messageText.Text = message;
        messageText.Visibility = message.Length > 0 ? Visibility.Visible : Visibility.Collapsed;

        var recommended = state?.RecommendedSize ?? 0;
        autoSizeItem.Content = recommended > 0 ? $"自動（推奨 {recommended} px）" : "自動";
        displayText.Text = state is { DisplayWidth: > 0, DisplayHeight: > 0 }
            ? recommended > 0
                ? $"ディスプレイ {state.DisplayWidth} × {state.DisplayHeight} ・ 推奨 {recommended} px"
                : $"ディスプレイ {state.DisplayWidth} × {state.DisplayHeight}"
            : loaded ? "画像の基準サイズ（物理 px）" : "ディスプレイ情報を取得しています…";

        var profile = state?.Profile is { Length: > 0 } p ? p : status?.Profile ?? "";
        profileText.Text = profile.Length > 0 ? profile : "—";

        UpdateTrialCountdown();
        UpdateEnabledStates();
    }

    private string BuildDetail(bool active)
    {
        var parts = new List<string>();
        if (active && status is { } s)
        {
            if (s.Size > 0)
            {
                parts.Add(state?.Automatic == true ? $"{s.Size} px（自動）" : $"{s.Size} px");
            }
            if (s.ItemCount > 0)
            {
                parts.Add($"項目 {s.ItemCount} 件");
            }
            if (s.CachedCount > 0)
            {
                parts.Add($"画像 {s.CachedCount} 件");
            }
        }
        else if (state is { } st && loaded)
        {
            parts.Add(st.Automatic || st.RequestedSize == 0
                ? st.RecommendedSize > 0 ? $"設定: 自動（{st.RecommendedSize} px）" : "設定: 自動"
                : $"設定: {st.RequestedSize} px");
        }
        return string.Join(" ・ ", parts);
    }

    private void SetBadge(string style, string textStyle, string iconStyle, string glyph, string text)
    {
        badge.Style = AppStyles.Get(style);
        badgeText.Style = AppStyles.Get(textStyle);
        badgeIcon.Style = AppStyles.Get(iconStyle);
        badgeIcon.Glyph = glyph;
        badgeText.Text = text;
    }

    private void UpdateTrialCountdown()
    {
        if (trialEndsAt is not { } end || isActive != true)
        {
            trialText.Visibility = Visibility.Collapsed;
            return;
        }
        var remaining = (int)Math.Ceiling((end - DateTimeOffset.Now).TotalSeconds);
        if (remaining <= 0)
        {
            trialEndsAt = null;
            trialText.Visibility = Visibility.Collapsed;
            // The resident reverts on its own; pick up the new state shortly after.
            var timer = DispatcherQueue.CreateTimer();
            timer.Interval = TimeSpan.FromSeconds(1.5);
            timer.IsRepeating = false;
            timer.Tick += (_, _) => _ = RefreshAsync(background: true);
            timer.Start();
            return;
        }
        trialText.Text = $"お試し中 ・ あと {remaining} 秒で標準の表示に戻ります";
        trialText.Visibility = Visibility.Visible;
    }

    private void UpdateEnabledStates()
    {
        var idle = !busy;
        var running = state?.Running ?? true;
        var active = isActive == true;
        var hasItem = editingKey is not null;
        var dirty = hasItem && pendingImages.ContainsKey(editingKey!);
        var hasImage = hasItem && (imageBox.Text.Length > 0 || SavedImage(editingKey!).Length > 0);

        refreshButton.IsEnabled = idle;
        sizeCombo.IsEnabled = idle;
        trialSwitch.IsEnabled = idle;
        applyButton.IsEnabled = idle && running;
        disableButton.IsEnabled = idle && running && active;
        itemCombo.IsEnabled = idle && items.Count > 0;
        imageBox.IsEnabled = idle && hasItem;
        browseButton.IsEnabled = idle && hasItem;
        saveImageButton.IsEnabled = idle && dirty;
        clearImageButton.IsEnabled = idle && hasImage;
        recoverButton.IsEnabled = idle && !active;
        recoverHint.Visibility = active ? Visibility.Visible : Visibility.Collapsed;
    }

    private void ShowInfo(InfoBarSeverity severity, string message, string? actionText = null,
                          Action? action = null)
    {
        infoBar.Severity = severity;
        infoBar.Title = severity switch
        {
            InfoBarSeverity.Error => "操作を完了できませんでした",
            InfoBarSeverity.Success => "完了",
            _ => "",
        };
        infoBar.Message = message;
        if (actionText is not null && action is not null)
        {
            var button = new Button { Content = actionText };
            AutomationProperties.SetName(button, actionText);
            button.Click += (_, _) =>
            {
                infoBar.IsOpen = false;
                action();
            };
            infoBar.ActionButton = button;
        }
        else
        {
            infoBar.ActionButton = null;
        }
        infoBar.IsOpen = true;
    }

    private static string NormalizePath(string text) => text.Trim().Trim('"').Trim();

    // ───────────────────────────── Smoke test ─────────────────────────────

    private async Task FinishSmokeTestAsync()
    {
        // Test-only overrides do not change the user's Windows theme or display settings.
        if (Enum.TryParse<ElementTheme>(Environment.GetEnvironmentVariable("DEKA_SMOKE_THEME"),
                                        true, out var theme))
        {
            root.RequestedTheme = theme;
        }
        if (int.TryParse(Environment.GetEnvironmentVariable("DEKA_SMOKE_WIDTH"), out var width) &&
            int.TryParse(Environment.GetEnvironmentVariable("DEKA_SMOKE_HEIGHT"), out var height) &&
            width >= 480 && height >= 480)
        {
            AppWindow.Resize(new SizeInt32(width, height));
        }
        // Let layout and rendering settle so measured sizes are real.
        await Task.Delay(400);
        pageScroller.ChangeView(null, 0, null, disableAnimation: true);
        await Task.Delay(200);
        var types = new SortedDictionary<string, int>(StringComparer.Ordinal);
        CountTypes(root, types);
        var report = new JsonObject
        {
            ["ok"] = loadErrors.Count == 0,
            ["nativePath"] = options.NativePath,
            ["nativeFound"] = File.Exists(options.NativePath),
            ["desktopChanged"] = false,
            ["windowTitle"] = Title,
            ["backdrop"] = SystemBackdrop?.GetType().FullName,
            ["theme"] = root.ActualTheme.ToString(),
            ["clientWidth"] = root.ActualWidth,
            ["clientHeight"] = root.ActualHeight,
            ["compact"] = root.ActualWidth < CompactWidth,
            ["statusBadge"] = badgeText.Text,
            ["headline"] = headline.Text,
            ["detail"] = detail.Text,
            ["trial"] = trialText.Visibility == Visibility.Visible ? trialText.Text : null,
            ["message"] = messageText.Text,
            ["running"] = state?.Running,
            ["active"] = isActive,
            ["sizeOptions"] = new JsonArray(sizeCombo.Items.OfType<ComboBoxItem>()
                .Select(i => (JsonNode?)JsonValue.Create(i.Content?.ToString())).ToArray()),
            ["selectedSize"] = (sizeCombo.SelectedItem as ComboBoxItem)?.Tag as string,
            ["trialSwitchOn"] = trialSwitch.IsOn,
            ["displayText"] = displayText.Text,
            ["recommendedSize"] = state?.RecommendedSize,
            ["itemCount"] = items.Count,
            ["items"] = new JsonArray(items.Select(i => (JsonNode?)new JsonObject
            {
                ["key"] = i.Key,
                ["name"] = i.Name,
                ["image"] = i.Image,
            }).ToArray()),
            ["selectedItem"] = editingKey,
            ["imagePath"] = imageBox.Text,
            ["applyEnabled"] = applyButton.IsEnabled,
            ["disableEnabled"] = disableButton.IsEnabled,
            ["saveImageEnabled"] = saveImageButton.IsEnabled,
            ["recoverEnabled"] = recoverButton.IsEnabled,
            ["recoverHintVisible"] = recoverHint.Visibility == Visibility.Visible,
            ["profile"] = profileText.Text,
            ["infoBarOpen"] = infoBar.IsOpen,
            ["infoBarSeverity"] = infoBar.IsOpen ? infoBar.Severity.ToString() : null,
            ["infoBarMessage"] = infoBar.IsOpen ? infoBar.Message : null,
            ["loadErrors"] = new JsonArray(loadErrors.Select(e => (JsonNode?)JsonValue.Create(e)).ToArray()),
            ["elementTypes"] = new JsonObject(types.Select(kv =>
                new KeyValuePair<string, JsonNode?>(kv.Key, JsonValue.Create(kv.Value)))),
        };
        SmokeTest.Write(options.SmokeTestReport!, report);
        await SmokeTest.RenderAsync(root, Path.ChangeExtension(options.SmokeTestReport!, ".png"));
        var scroll = FindScrollViewer(root);
        if (scroll is not null)
        {
            scroll.ChangeView(null, scroll.ScrollableHeight, null, disableAnimation: true);
            await Task.Delay(300);
            await SmokeTest.RenderAsync(root, Path.ChangeExtension(options.SmokeTestReport!, ".bottom.png"));
        }
        Program.ExitCode = loadErrors.Count == 0 ? 0 : 1;
        Close();
    }

    private static ScrollViewer? FindScrollViewer(DependencyObject parent)
    {
        if (parent is ScrollViewer scroll)
        {
            return scroll;
        }
        for (var i = 0; i < VisualTreeHelper.GetChildrenCount(parent); i++)
        {
            var result = FindScrollViewer(VisualTreeHelper.GetChild(parent, i));
            if (result is not null)
            {
                return result;
            }
        }
        return null;
    }

    private static void CountTypes(DependencyObject node, IDictionary<string, int> types)
    {
        var name = node.GetType().Name;
        if (node is Control or TextBlock or Image or FontIcon)
        {
            types[name] = types.TryGetValue(name, out var n) ? n + 1 : 1;
        }
        var count = VisualTreeHelper.GetChildrenCount(node);
        for (var i = 0; i < count; ++i)
        {
            CountTypes(VisualTreeHelper.GetChild(node, i), types);
        }
    }
}
