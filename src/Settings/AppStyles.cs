// SPDX-License-Identifier: MIT
// Copyright (c) 2026 sh4
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Markup;

namespace DekaDesktopIcon.Settings;

/// <summary>
/// Theme-aware styles loaded at runtime (no XAML compilation). ThemeResource references keep
/// cards, badges and text in sync with light/dark/high-contrast switches.
/// </summary>
internal static class AppStyles
{
    public const string Card = "DekaCardStyle";
    public const string Divider = "DekaDividerStyle";
    public const string Secondary = "DekaSecondaryTextStyle";
    public const string SectionHeader = "DekaSectionHeaderStyle";
    public const string BadgeActive = "DekaBadgeActiveStyle";
    public const string BadgeInactive = "DekaBadgeInactiveStyle";
    public const string BadgeError = "DekaBadgeErrorStyle";
    public const string BadgeTextActive = "DekaBadgeTextActiveStyle";
    public const string BadgeTextInactive = "DekaBadgeTextInactiveStyle";
    public const string BadgeTextError = "DekaBadgeTextErrorStyle";
    public const string BadgeIconActive = "DekaBadgeIconActiveStyle";
    public const string BadgeIconInactive = "DekaBadgeIconInactiveStyle";
    public const string BadgeIconError = "DekaBadgeIconErrorStyle";
    public const string AccentText = "DekaAccentTextStyle";
    public const string IconTile = "DekaIconTileStyle";
    public const string Preview = "DekaPreviewStyle";

    private const string Xaml = """
        <ResourceDictionary
            xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
            xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml">
          <Style x:Key="DekaCardStyle" TargetType="Border">
            <Setter Property="Background" Value="{ThemeResource CardBackgroundFillColorDefaultBrush}" />
            <Setter Property="BorderBrush" Value="{ThemeResource CardStrokeColorDefaultBrush}" />
            <Setter Property="BorderThickness" Value="1" />
            <Setter Property="CornerRadius" Value="{ThemeResource OverlayCornerRadius}" />
            <Setter Property="Padding" Value="0" />
          </Style>
          <Style x:Key="DekaDividerStyle" TargetType="Border">
            <Setter Property="Height" Value="1" />
            <Setter Property="Background" Value="{ThemeResource DividerStrokeColorDefaultBrush}" />
          </Style>
          <Style x:Key="DekaSecondaryTextStyle" TargetType="TextBlock">
            <Setter Property="FontSize" Value="12" />
            <Setter Property="LineHeight" Value="16" />
            <Setter Property="TextWrapping" Value="Wrap" />
            <Setter Property="Foreground" Value="{ThemeResource TextFillColorSecondaryBrush}" />
          </Style>
          <Style x:Key="DekaSectionHeaderStyle" TargetType="TextBlock">
            <Setter Property="FontSize" Value="14" />
            <Setter Property="FontWeight" Value="SemiBold" />
            <Setter Property="Margin" Value="2,28,0,8" />
            <Setter Property="Foreground" Value="{ThemeResource TextFillColorPrimaryBrush}" />
          </Style>
          <Style x:Key="DekaBadgeActiveStyle" TargetType="Border">
            <Setter Property="Background" Value="{ThemeResource AccentFillColorDefaultBrush}" />
            <Setter Property="BorderBrush" Value="{ThemeResource AccentFillColorDefaultBrush}" />
            <Setter Property="BorderThickness" Value="1" />
            <Setter Property="CornerRadius" Value="12" />
            <Setter Property="Padding" Value="10,3,12,4" />
          </Style>
          <Style x:Key="DekaBadgeInactiveStyle" TargetType="Border">
            <Setter Property="Background" Value="{ThemeResource ControlFillColorSecondaryBrush}" />
            <Setter Property="BorderBrush" Value="{ThemeResource ControlStrokeColorDefaultBrush}" />
            <Setter Property="BorderThickness" Value="1" />
            <Setter Property="CornerRadius" Value="12" />
            <Setter Property="Padding" Value="10,3,12,4" />
          </Style>
          <Style x:Key="DekaBadgeErrorStyle" TargetType="Border">
            <Setter Property="Background" Value="{ThemeResource SystemFillColorCriticalBackgroundBrush}" />
            <Setter Property="BorderBrush" Value="{ThemeResource SystemFillColorCriticalBrush}" />
            <Setter Property="BorderThickness" Value="1" />
            <Setter Property="CornerRadius" Value="12" />
            <Setter Property="Padding" Value="10,3,12,4" />
          </Style>
          <Style x:Key="DekaBadgeTextActiveStyle" TargetType="TextBlock">
            <Setter Property="FontSize" Value="12" />
            <Setter Property="FontWeight" Value="SemiBold" />
            <Setter Property="Foreground" Value="{ThemeResource TextOnAccentFillColorPrimaryBrush}" />
          </Style>
          <Style x:Key="DekaBadgeTextInactiveStyle" TargetType="TextBlock">
            <Setter Property="FontSize" Value="12" />
            <Setter Property="FontWeight" Value="SemiBold" />
            <Setter Property="Foreground" Value="{ThemeResource TextFillColorSecondaryBrush}" />
          </Style>
          <Style x:Key="DekaBadgeTextErrorStyle" TargetType="TextBlock">
            <Setter Property="FontSize" Value="12" />
            <Setter Property="FontWeight" Value="SemiBold" />
            <Setter Property="Foreground" Value="{ThemeResource SystemFillColorCriticalBrush}" />
          </Style>
          <Style x:Key="DekaBadgeIconActiveStyle" TargetType="FontIcon">
            <Setter Property="FontSize" Value="12" />
            <Setter Property="Foreground" Value="{ThemeResource TextOnAccentFillColorPrimaryBrush}" />
          </Style>
          <Style x:Key="DekaBadgeIconInactiveStyle" TargetType="FontIcon">
            <Setter Property="FontSize" Value="12" />
            <Setter Property="Foreground" Value="{ThemeResource TextFillColorSecondaryBrush}" />
          </Style>
          <Style x:Key="DekaBadgeIconErrorStyle" TargetType="FontIcon">
            <Setter Property="FontSize" Value="12" />
            <Setter Property="Foreground" Value="{ThemeResource SystemFillColorCriticalBrush}" />
          </Style>
          <Style x:Key="DekaAccentTextStyle" TargetType="TextBlock">
            <Setter Property="FontSize" Value="14" />
            <Setter Property="FontWeight" Value="SemiBold" />
            <Setter Property="TextWrapping" Value="Wrap" />
            <Setter Property="Foreground" Value="{ThemeResource AccentTextFillColorPrimaryBrush}" />
          </Style>
          <Style x:Key="DekaIconTileStyle" TargetType="Border">
            <Setter Property="Width" Value="40" />
            <Setter Property="Height" Value="40" />
            <Setter Property="CornerRadius" Value="{ThemeResource ControlCornerRadius}" />
            <Setter Property="Background" Value="{ThemeResource SubtleFillColorSecondaryBrush}" />
          </Style>
          <Style x:Key="DekaPreviewStyle" TargetType="Border">
            <Setter Property="Width" Value="88" />
            <Setter Property="Height" Value="88" />
            <Setter Property="CornerRadius" Value="{ThemeResource ControlCornerRadius}" />
            <Setter Property="BorderThickness" Value="1" />
            <Setter Property="BorderBrush" Value="{ThemeResource ControlStrokeColorDefaultBrush}" />
            <Setter Property="Background" Value="{ThemeResource ControlFillColorSecondaryBrush}" />
          </Style>
        </ResourceDictionary>
        """;

    public static void Merge(ResourceDictionary target)
    {
        target.MergedDictionaries.Add((ResourceDictionary)XamlReader.Load(Xaml));
    }

    public static Style Get(string key) => (Style)Application.Current.Resources[key];
}
