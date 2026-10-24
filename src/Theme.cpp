#include <EStudioUI/Theme.h>

#include <algorithm>
#include <cmath>
#include <locale>
#include <sstream>
#include <stdexcept>

namespace EStudioUI
{
namespace
{
Color Rgb(unsigned value)
{
    return {static_cast<float>((value >> 16) & 255) / 255, static_cast<float>((value >> 8) & 255) / 255,
            static_cast<float>(value & 255) / 255, 1};
}

Color Mix(Color a, Color b, float amount)
{
    return {a.m_R + (b.m_R - a.m_R) * amount, a.m_G + (b.m_G - a.m_G) * amount, a.m_B + (b.m_B - a.m_B) * amount, 1};
}

std::string Css(Color color)
{
    std::ostringstream output;
    output.imbue(std::locale::classic());
    output << "rgba(" << std::lround(color.m_R * 255) << ',' << std::lround(color.m_G * 255) << ','
           << std::lround(color.m_B * 255) << ',' << color.m_A << ')';
    return output.str();
}

void Replace(std::string& text, std::string_view token, const std::string& value)
{
    for (std::size_t position = 0; (position = text.find(token, position)) != std::string::npos;
         position += value.size())
    {
        text.replace(position, token.size(), value);
    }
}
} // namespace

std::string_view Theme::Name(ThemeFamily family)
{
    switch (family)
    {
    case ThemeFamily::Minimal:
        return "Minimal";
    case ThemeFamily::Engineering:
        return "Engineering";
    case ThemeFamily::Office:
        return "Office";
    case ThemeFamily::Studio:
        return "Studio";
    case ThemeFamily::Game:
        return "Game";
    case ThemeFamily::System:
        return "System";
    }
    throw std::invalid_argument("Unknown theme preset.");
}

ThemePalette Theme::Resolve(const SystemAppearance& appearance) const
{
    (void)Name(m_Family);
    appearance.Validate();
    if (m_Mode < ThemeMode::Light || m_Mode > ThemeMode::System || !std::isfinite(m_FontSize) || m_FontSize < 9 ||
        m_FontSize > 32 || !std::isfinite(m_Density) || m_Density < .75f || m_Density > 1.5f ||
        (m_Radius && (!std::isfinite(*m_Radius) || *m_Radius < 0 || *m_Radius > 24)))
    {
        throw std::invalid_argument("Theme font size, density, radius or mode is outside its supported range.");
    }
    SystemAppearance colors;
    colors.m_Accent = m_Accent.value_or(colors.m_Accent);
    colors.m_Background = m_Background.value_or(colors.m_Background);
    colors.m_Foreground = m_Foreground.value_or(colors.m_Foreground);
    colors.Validate();
    ThemePalette p;
    p.m_Dark =
        m_Mode == ThemeMode::Dark || (m_Mode == ThemeMode::System && appearance.m_ModeKnown && appearance.m_Dark);
    p.m_HighContrast = m_Mode == ThemeMode::System && appearance.m_HighContrast;
    p.m_ReducedMotion = m_ReducedMotion.value_or(appearance.m_ReducedMotion) || p.m_HighContrast;
    const auto dark = p.m_Dark;
    p.m_Background = Rgb(dark ? 0x181818 : 0xfcfcfc);
    p.m_Surface = Rgb(dark ? 0x222222 : 0xf4f4f4);
    p.m_Input = Rgb(dark ? 0x1c1c1c : 0xffffff);
    p.m_Foreground = Rgb(dark ? 0xeeeeee : 0x161616);
    p.m_Accent = Rgb(dark ? 0x789aff : 0x3467eb);
    if (m_Family == ThemeFamily::Engineering)
    {
        p.m_Background = Rgb(dark ? 0x1b1e22 : 0xf5f6f7);
        p.m_Surface = Rgb(dark ? 0x25292e : 0xeceef0);
        p.m_Accent = Rgb(dark ? 0x79a9d7 : 0x285f8e);
    }
    else if (m_Family == ThemeFamily::Office)
    {
        p.m_Accent = Rgb(dark ? 0x75b6f2 : 0x0f6cbd);
    }
    else if (m_Family == ThemeFamily::Studio)
    {
        p.m_Accent = Rgb(dark ? 0x68c9ba : 0x157c6e);
    }
    else if (m_Family == ThemeFamily::Game)
    {
        p.m_Background = Rgb(dark ? 0x151324 : 0xf6f3fd);
        p.m_Surface = Rgb(dark ? 0x242038 : 0xede7fa);
        p.m_Accent = Rgb(dark ? 0xb59aff : 0x7451bd);
    }
    if (m_Mode == ThemeMode::System && appearance.m_AccentKnown)
    {
        p.m_Accent = appearance.m_Accent;
    }
    if (m_Family == ThemeFamily::System && m_Mode == ThemeMode::System && appearance.m_ModeKnown)
    {
        p.m_Background = appearance.m_Background;
        p.m_Foreground = appearance.m_Foreground;
        p.m_Surface = Mix(p.m_Background, p.m_Foreground, .04f);
        p.m_Input = p.m_Background;
    }
    p.m_Background = m_Background.value_or(p.m_Background);
    p.m_Foreground = m_Foreground.value_or(p.m_Foreground);
    p.m_Accent = m_Accent.value_or(p.m_Accent);
    if (m_Background)
    {
        p.m_Surface = Mix(p.m_Background, p.m_Foreground, .035f);
        p.m_Input = p.m_Background;
    }
    p.m_Border = Mix(p.m_Background, p.m_Foreground, dark ? .18f : .14f);
    p.m_Muted = Mix(p.m_Background, p.m_Foreground, .64f);
    p.m_Hover = Mix(p.m_Surface, p.m_Foreground, .065f);
    p.m_Selected = Mix(p.m_Surface, p.m_Accent, dark ? .25f : .12f);
    if (m_Family == ThemeFamily::Minimal)
    {
        p.m_Selected = Mix(p.m_Surface, p.m_Foreground, .08f);
    }
    if (p.m_HighContrast)
    {
        p.m_Background = p.m_Surface = p.m_Input = appearance.m_Background;
        p.m_Foreground = p.m_Muted = p.m_Border = appearance.m_Foreground;
        p.m_Accent = appearance.m_AccentKnown ? appearance.m_Accent : appearance.m_Foreground;
        p.m_Hover = p.m_Selected = p.m_Accent;
    }
    const auto linear = [](float value)
    { return value <= .04045f ? value / 12.92f : std::pow((value + .055f) / 1.055f, 2.4f); };
    const float luminance =
        .2126f * linear(p.m_Accent.m_R) + .7152f * linear(p.m_Accent.m_G) + .0722f * linear(p.m_Accent.m_B);
    p.m_OnAccent = Rgb(luminance > .179f ? 0x101010 : 0xffffff);
    return p;
}

std::string Theme::StyleSheet(const SystemAppearance& appearance) const
{
    const auto p = Resolve(appearance);
    const auto number = [](float value)
    {
        std::ostringstream output;
        output.imbue(std::locale::classic());
        output << value;
        return output.str();
    };
    const float radius = p.m_HighContrast ? 0
                                          : m_Radius.value_or(m_Family == ThemeFamily::Engineering ? 3.0f
                                                              : m_Family == ThemeFamily::Game      ? 9.0f
                                                                                                   : 6.0f);
    std::string css = R"CSS(
/* EStudioUI semantic theme: ordinary CSS, compiled only on a preference change. */
window { background: @bg@; color: @fg@; font-size: @font@px; }
button, toolbutton, textbox, numericinput, combobox, listwidget, treewidget, tablewidget, propertygrid,
groupbox, frame, collapsiblepanel, colorpicker, popup, dialog, contextmenu, toolbar, stackedwidget {
 color: @fg@; font-size: @font@px; background: @surface@; border: 1px solid @border@;
 border-radius: @radius@px; box-shadow: none; background-image: none;
}
button, toolbutton { height: @height@px; padding: 5px 12px; background: @input@; text-align: center; }
button:hover, toolbutton:hover { background: @hover@; border-color: @border@; box-shadow: none; }
button:focus, toolbutton:focus { border: 1px solid @accent@; box-shadow: none; }
button:active, button:pressed, toolbutton:pressed { background: @selected@; border-color: @accent@; }
button:selected, toolbutton:selected { background: @selected@; color: @fg@; border-color: @border@; }
button:disabled, toolbutton:disabled, textbox:disabled, combobox:disabled { opacity: .45; }
button.primary, .dialog-accept { background: @accent@; color: @onaccent@; border-color: @accent@; }
button.primary:hover, .dialog-accept:hover { background: @accent-hover@; color: @onaccent@; }
textbox { height: @height@px; padding: 5px 9px; background: @input@; placeholder-color: @muted@; caret-color: @fg@; selection-background-color: @accent@; selection-color: @onaccent@; }
textbox:hover { border-color: @muted@; }
textbox:focus, textbox:focus-within, numericinput:focus-within, combobox:focus-within { border: 1px solid @accent@; background: @input@; box-shadow: none; }
textbox:read-only { background: @surface@; color: @muted@; }
textbox::caret, textbox::preedit { color: @fg@; }
textbox::selection { background: @accent@; color: @onaccent@; }
numericinput, combobox { height: @height@px; padding: 0; gap: 0; background: @input@; }
numericinput > .numeric-editor, combobox > textbox.combo-editor, filepatheditor textbox.path-editor, folderpicker textbox.path-editor {
 background: @input@; color: @fg@; border-width: 0; border-radius: @radius@px; padding: 4px 8px; font-size: @font@px; placeholder-color: @muted@; caret-color: @fg@;
}
numericinput > .numeric-increment, numericinput > .numeric-decrement, combobox > toolbutton.combo-arrow {
 background: transparent; color: @muted@; border-width: 0; border-radius: @radius@px; padding: 0; box-shadow: none;
}
combobox > toolbutton.combo-arrow:hover, numericinput > toolbutton:hover { background: @hover@; }
combobox listwidget.combo-popup { background: @input@; color: @fg@; border: 1px solid @border@; border-radius: @radius@px; box-shadow: none; }
combobox listwidget.combo-popup::item { background: transparent; color: @fg@; border-radius: @radius@px; }
combobox listwidget.combo-popup::item:hover { background: @hover@; }
combobox listwidget.combo-popup::item:selected { background: @selected@; color: @fg@; }
checkbox, radiobutton { color: @fg@; font-size: @font@px; background: transparent; box-shadow: none; }
checkbox:hover, radiobutton:hover { background: transparent; }
checkbox:focus, radiobutton:focus { box-shadow: none; border-color: @accent@; }
checkbox::indicator, radiobutton::indicator { background: @input@; border: 1px solid @border@; box-shadow: none; }
checkbox::indicator:checked, checkbox::indicator:indeterminate, radiobutton::indicator:checked { background: @accent@; border-color: @accent@; color: @onaccent@; box-shadow: none; }
checkbox::mark { color: @onaccent@; }
radiobutton::mark { background: @onaccent@; }
checkbox::label, radiobutton::label { color: @fg@; }
slider::groove { background: @border@; border-width: 0; border-radius: 2px; box-shadow: none; }
slider::fill { background: @accent@; border-width: 0; box-shadow: none; }
slider::handle { background: @accent@; border: 2px solid @input@; box-shadow: none; }
slider::handle:hover, slider:focus::handle { background: @accent@; box-shadow: none; }
progressbar::track, busyindicator::track { background: @border@; border-width: 0; box-shadow: none; }
progressbar::chunk, busyindicator::chunk { background: @accent@; box-shadow: none; }
progressbar::label { color: @fg@; }
listwidget, treewidget, tablewidget, propertygrid { background: @input@; color: @fg@; row-height: @row@px; alternate-background: @surface@; selection-background-color: @selected@; selection-color: @fg@; hover-background: @hover@; header-background: @surface@; grid-color: @border@; }
listwidget::item, treewidget::item, tablewidget::cell, propertygrid::row { border-width: 0; box-shadow: none; }
tablewidget::header, propertygrid::category, treewidget::branch { color: @muted@; border-color: @border@; box-shadow: none; }
propertygrid::category:expanded, propertygrid::row:read-only { color: @muted@; }
scrollarea { background: @bg@; border: 1px solid @border@; border-radius: @radius@px; }
scrollarea::scroll-track, listwidget::scroll-track, treewidget::scroll-track, tablewidget::scroll-track, propertygrid::scroll-track, scrollbar::scroll-track { background: @surface@; box-shadow: none; border-width: 0; }
scrollarea::scroll-thumb, listwidget::scroll-thumb, treewidget::scroll-thumb, tablewidget::scroll-thumb, propertygrid::scroll-thumb, scrollbar::scroll-thumb { background: @border@; border-width: 0; border-radius: 4px; box-shadow: none; }
scrollbar::scroll-thumb:hover, scrollarea::scroll-thumb:hover, listwidget::scroll-thumb:hover, treewidget::scroll-thumb:hover, tablewidget::scroll-thumb:hover { background: @muted@; }
splitter::handle { background: @border@; border-radius: 0; box-shadow: none; }
splitter::handle:hover, splitter::handle:pressed { background: @accent@; box-shadow: none; }
.workspace-splitter::handle { background: @border@; }
.workspace-splitter::handle:hover, .workspace-splitter::handle:pressed { background: @accent@; }
separator, .divider { background: @border@; border-width: 0; }
collapsiblepanel > .section-header { background: @surface@; color: @fg@; border-width: 0; border-radius: @radius@px; box-shadow: none; }
collapsiblepanel > .section-header:hover { background: @hover@; }
collapsiblepanel:expanded > .section-header { color: @fg@; }
section-content { background: transparent; }
uniteditor, vectoreditor, coloreditor, vector-component, vector-label, color-channel { background: transparent; color: @fg@; }
.unit-choice, .path-browse, .path-recent { background: @surface@; color: @fg@; border: 1px solid @border@; box-shadow: none; border-radius: @radius@px; }
colorpicker, .color-popover { background: @surface@; border: 1px solid @border@; box-shadow: none; }
mainwindow, mainwindow-toolbars, dockmanager, .workspace-splitter, .dock-tabs, dockwidget, dock-content, workspace-central { background: @bg@; color: @fg@; border-radius: 0; box-shadow: none; }
window-titlebar { background: @bg@; color: @fg@; border-width: 0; box-shadow: none; }
window-titlebar label { color: @fg@; font-size: @font@px; }
window-titlebar menubar, titlebar-menus, titlebar-content { background: transparent; }
window-titlebar menubar > button, window-titlebar .caption-button { background: transparent; color: @fg@; border-width: 0; border-radius: 0; box-shadow: none; }
window-titlebar menubar > button:hover, window-titlebar .caption-button:hover { background: @hover@; color: @fg@; }
window-titlebar .caption-close:hover, window-titlebar .caption-close:pressed { background: #c42b3b; color: white; }
window-titlebar .caption-button:pressed { background: @selected@; }
toolbar, dock-header, statusbar { background: @surface@; color: @fg@; border-radius: 0; border-color: @border@; box-shadow: none; }
toolbar .toolbar-scroll { background: transparent; color: @muted@; border-width: 0; box-shadow: none; }
toolbar .toolbar-scroll:hover { background: @hover@; color: @fg@; }
dock-grip, statusbar label { color: @muted@; }
.dock-tabs tabbar, .dock-tabs .tab-button, tabbar toolbutton { background: transparent; color: @muted@; border: 0; box-shadow: none; border-radius: @radius@px; }
.dock-tabs .tab-button:selected, tabbar .tab-button:selected, tabbar .selected { background: @selected@; color: @fg@; border-color: @accent@; }
tabbar, tabpage { background: @bg@; color: @fg@; border-width: 0; }
menubar, dialog-title { background: @surface@; color: @fg@; }
menubar > button, menu-trigger, dialog-close { background: transparent; color: @fg@; border-width: 0; box-shadow: none; }
menubar > button:hover, menu-trigger:hover, dialog-close:hover { background: @hover@; }
tabbar toolbutton:hover, .dock-tabs .tab-button:hover { background: @hover@; color: @fg@; }
contextmenu, popup, dialog, tooltip { background: @input@; color: @fg@; border: 1px solid @border@; border-radius: @radius@px; box-shadow: none; }
contextmenu .menu-action { color: @fg@; background: transparent; box-shadow: none; }
contextmenu .menu-action:hover, contextmenu .menu-action:focus { background: @hover@; }
#title { color: @fg@; font-size: 26px; }
.subtitle, .eyebrow, .field-label, .description, #footer, #edit-status, .creator-mark { color: @muted@; }
.card { background: @input@; border: 1px solid @border@; border-radius: @radius@px; box-shadow: none; }
#message, #css-sample { background: @surface@; color: @fg@; border: 1px solid @border@; border-radius: @radius@px; }
designer-surface { background: @surface@; border: 1px solid @border@; }
design-host, design-overlay { background: transparent; border-width: 0; }
design-overlay { color: @accent@; }
)CSS";
    for (const auto& [token, value] : std::initializer_list<std::pair<std::string_view, std::string>>{
             {"@bg@", Css(p.m_Background)},
             {"@fg@", Css(p.m_Foreground)},
             {"@surface@", Css(p.m_Surface)},
             {"@input@", Css(p.m_Input)},
             {"@accent@", Css(p.m_Accent)},
             {"@accent-hover@", Css(Mix(p.m_Accent, Rgb(p.m_Dark ? 0xffffff : 0), p.m_Dark ? .14f : .22f))},
             {"@onaccent@", Css(p.m_OnAccent)},
             {"@muted@", Css(p.m_Muted)},
             {"@border@", Css(p.m_Border)},
             {"@hover@", Css(p.m_Hover)},
             {"@selected@", Css(p.m_Selected)},
             {"@radius@", number(radius)},
             {"@font@", number(m_FontSize)},
             {"@height@", number(std::max(32 * m_Density, m_FontSize + 12))},
             {"@row@", number(std::max(30 * m_Density, m_FontSize + 10))}})
    {
        Replace(css, token, value);
    }
    if (p.m_ReducedMotion || m_Family != ThemeFamily::Game)
    {
        css += "\nbutton, toolbutton, textbox, combobox, checkbox::indicator, radiobutton::indicator { transition: "
               "none; animation: none; }\n";
    }
    else
    {
        css += "\nbutton, toolbutton { transition: background-color 120ms ease; }\n";
    }
    if (p.m_HighContrast)
    {
        css += "\n* { color: " + Css(p.m_Foreground) + " !important; background: " + Css(p.m_Background) +
               " !important; border-color: " + Css(p.m_Foreground) +
               " !important; box-shadow: none !important; background-image: none "
               "!important; animation: none !important; transition: none !important; }\n";
    }
    return css;
}
} // namespace EStudioUI
