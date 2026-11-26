// SPDX-License-Identifier: MIT
// Copyright (c) 2026 sh4
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Automation;
using Microsoft.UI.Xaml.Automation.Peers;
using Microsoft.UI.Xaml.Controls;

namespace DekaDesktopIcon.Settings;

/// <summary>
/// Windows Settings style row: icon tile, title/description, trailing control. In compact mode the
/// control wraps under the text so narrow windows stay readable without horizontal scrolling.
/// </summary>
internal sealed class SettingRow
{
    private readonly FrameworkElement? control;

    public SettingRow(string glyph, FrameworkElement body, FrameworkElement? control)
    {
        this.control = control;
        Root = new Grid
        {
            Padding = new Thickness(16, 14, 16, 14),
            ColumnSpacing = 16,
            MinHeight = 68,
        };
        Root.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
        Root.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
        Root.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
        Root.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
        Root.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });

        var tile = new Border
        {
            Style = AppStyles.Get(AppStyles.IconTile),
            VerticalAlignment = VerticalAlignment.Center,
            Child = new FontIcon { Glyph = glyph, FontSize = 18 },
        };
        AutomationProperties.SetAccessibilityView(tile, AccessibilityView.Raw);
        Root.Children.Add(tile);

        body.VerticalAlignment = VerticalAlignment.Center;
        Grid.SetColumn(body, 1);
        Root.Children.Add(body);

        if (control is not null)
        {
            control.VerticalAlignment = VerticalAlignment.Center;
            Root.Children.Add(control);
        }
        SetCompact(false);
    }

    public Grid Root { get; }

    public void SetCompact(bool compact)
    {
        if (control is null)
        {
            return;
        }
        Root.RowSpacing = compact ? 12 : 0;
        Grid.SetRow(control, compact ? 1 : 0);
        Grid.SetColumn(control, compact ? 1 : 2);
        Grid.SetColumnSpan(control, compact ? 2 : 1);
        control.HorizontalAlignment = compact ? HorizontalAlignment.Left : HorizontalAlignment.Right;
    }

    public static StackPanel TextBody(string title, string description, out TextBlock descriptionBlock)
    {
        var titleBlock = new TextBlock
        {
            Text = title,
            Style = (Style)Application.Current.Resources["BodyTextBlockStyle"],
            TextWrapping = TextWrapping.Wrap,
        };
        descriptionBlock = new TextBlock
        {
            Text = description,
            Style = AppStyles.Get(AppStyles.Secondary),
            Visibility = description.Length > 0 ? Visibility.Visible : Visibility.Collapsed,
        };
        return new StackPanel { Spacing = 2, Children = { titleBlock, descriptionBlock } };
    }

    public static Border Card(params UIElement[] rows)
    {
        var stack = new StackPanel();
        for (var i = 0; i < rows.Length; ++i)
        {
            if (i > 0)
            {
                stack.Children.Add(new Border { Style = AppStyles.Get(AppStyles.Divider) });
            }
            stack.Children.Add(rows[i]);
        }
        return new Border { Style = AppStyles.Get(AppStyles.Card), Child = stack };
    }

    public static TextBlock SectionHeader(string text)
    {
        var header = new TextBlock { Text = text, Style = AppStyles.Get(AppStyles.SectionHeader) };
        AutomationProperties.SetHeadingLevel(header, AutomationHeadingLevel.Level2);
        return header;
    }
}
