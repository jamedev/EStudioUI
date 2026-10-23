#include <EStudioUI/MenuItem.h>
#include <EStudioUI/ContextMenu.h>
#include <EStudioUI/MenuBar.h>
#include <EStudioUI/Renderer.h>
#include <EStudioUI/TextBuffer.h>

namespace EStudioUI
{
namespace
{
constexpr const char* s_Parts[]{"label", "icon", "shortcut"};
}

MenuItem::MenuItem(std::string text) : ToolButton(std::move(text))
{
    AddClass("menu-action");
    for (const auto* part : s_Parts)
        RegisterStylePart(part, 0, [](const Style&, unsigned) { return Style{}; });
    RegisterStylePart("mark", 0,
                      [](const Style&, unsigned)
                      {
                          Style style;
                          style.m_Width = {Length::Unit::Pixels, 2};
                          return style;
                      });
    RegisterStylePart("arrow", 0,
                      [](const Style&, unsigned)
                      {
                          Style style;
                          style.m_Width = {Length::Unit::Pixels, 2};
                          return style;
                      });
    SetAccessibleRole(SemanticRole::MenuItem);
}

MenuItem::~MenuItem()
{
    if (auto* bar = dynamic_cast<MenuBar*>(m_MenuBar.Get()))
        bar->TrackCommand(GetAction(), false);
}

void MenuItem::SetAction(std::shared_ptr<Action> action)
{
    VerifyStructuralMutation();
    const auto previous = GetAction();
    if (previous == action)
        return;
    if (action)
        (void)action->GetSnapshot();
    if (auto* bar = dynamic_cast<MenuBar*>(m_MenuBar.Get()))
    {
        bar->TrackCommand(action, true);
        bar->TrackCommand(previous, false);
    }
    ToolButton::SetAction(std::move(action));
}

void MenuItem::SetIcon(std::string icon)
{
    VerifyStructuralMutation();
    if (icon.size() > 256)
        throw std::length_error("A menu glyph icon is limited to 256 UTF-8 bytes.");
    if (m_Icon == icon)
        return;
    m_Icon = TextBuffer::Sanitize(icon);
    Invalidate(Invalidation::Content);
}

const std::string& MenuItem::GetIcon() const
{
    VerifyAccess();
    return m_Icon;
}

void MenuItem::SetShortcutHint(std::string hint)
{
    VerifyStructuralMutation();
    if (hint.size() > 512)
        throw std::length_error("A menu shortcut hint is limited to 512 UTF-8 bytes.");
    if (m_Hint == hint)
        return;
    m_Hint = TextBuffer::Sanitize(hint);
    Invalidate(Invalidation::Content);
}

const std::string& MenuItem::GetShortcutHint() const
{
    VerifyAccess();
    return m_Hint;
}

void MenuItem::OnActionChanged()
{
    const WidgetRef self(this);
    ToolButton::OnActionChanged();
    if (self.Get())
    {
        const auto state = GetAction()->GetSnapshot();
        SetShortcutHint(state->m_Shortcuts.empty() ? std::string{} : state->m_Shortcuts.front().ToString());
    }
}

void MenuItem::OnFontsChanged() noexcept
{
    Label::OnFontsChanged();
    m_Layouts = {};
}

const TextLayout& MenuItem::Text(FontFace& font, std::size_t part) const
{
    const auto& text = part == 0 ? GetText() : part == 1 ? m_Icon : m_Hint;
    const auto& style = GetPartStyle(s_Parts[part]);
    if (m_LayoutTexts[part] != text || !font.Matches(m_Layouts[part], style))
    {
        m_LayoutTexts[part] = text;
        m_Layouts[part] = font.Shape(TextBuffer::Sanitize(text), style);
    }
    return m_Layouts[part];
}

Point MenuItem::Measure(FontFace& font) const
{
    const auto& caption = Text(font, 0);
    const auto& icon = Text(font, 1);
    const auto& hint = Text(font, 2);
    const float leading = GetImageIcon().IsEmpty() ? 32 : std::max(32.0f, GetImageIconSize().m_X + 8);
    return {leading + caption.m_Width + (m_Hint.empty() ? 0 : hint.m_Width + 18) + 18,
            std::max({20.0f, caption.m_Ascender + caption.m_Descender, icon.m_Ascender + icon.m_Descender,
                      hint.m_Ascender + hint.m_Descender, GetImageIcon().IsEmpty() ? 0 : GetImageIconSize().m_Y})};
}

MeasureResult MenuItem::MeasureConstrained(FontFace& font, const MeasureConstraints&) const
{
    const auto size = Measure(font);
    const auto& text = Text(font, 0);
    return {{}, size, (size.m_Y - text.m_Ascender - text.m_Descender) * 0.5f + text.m_Ascender, false};
}

void MenuItem::Paint(Renderer& renderer, FontFace& font, double, float opacity) const
{
    renderer.DrawBox(GetBounds(), GetStyle(), GetClip(), opacity);
    const auto b = GetContentBounds();
    const auto& caption = Text(font, 0);
    const bool rtl = caption.m_RightToLeft;
    const float leadingWidth = GetImageIcon().IsEmpty() ? 32 : std::max(32.0f, GetImageIconSize().m_X + 8);
    const float hintWidth = m_Hint.empty() ? 0 : Text(font, 2).m_Width + 18;
    const Rect leading{rtl ? b.m_X + b.m_Width - leadingWidth + 8 : b.m_X, b.m_Y, leadingWidth - 8, b.m_Height};
    const Rect trailing{rtl ? b.m_X : b.m_X + b.m_Width - 14, b.m_Y, 14, b.m_Height};
    const Rect label{b.m_X + (rtl ? 18 + hintWidth : leadingWidth), b.m_Y,
                     std::max(0.0f, b.m_Width - leadingWidth - 18 - hintWidth), b.m_Height};
    const Rect hint{rtl ? b.m_X + 18 : b.m_X + b.m_Width - 18 - hintWidth, b.m_Y, std::max(0.0f, hintWidth - 18),
                    b.m_Height};
    const auto text = [&](std::size_t part, Rect rect)
    {
        const auto& style = GetPartStyle(s_Parts[part]);
        const auto& layout = Text(font, part);
        auto color = style.m_Color;
        color.m_A *= opacity * style.m_Opacity;
        renderer.PushState(style.m_Transform.Resolve(rect));
        renderer.DrawText(
            layout,
            {rect.m_X + (rtl ? std::max(0.0f, rect.m_Width - layout.m_Width) : 0),
             rect.m_Y + (rect.m_Height - layout.m_Ascender - layout.m_Descender) * 0.5f + layout.m_Ascender},
            color, GetClip().Intersect(rect));
        renderer.PopState();
    };
    text(0, label);
    if (!(IsCheckable() && IsChecked()))
    {
        if (!GetImageIcon().IsEmpty())
            PaintImageIcon(renderer, leading, opacity);
        else if (!m_Icon.empty())
            text(1, leading);
    }
    if (!m_Hint.empty())
        text(2, hint);
    const auto line = [&](const char* part, Rect rect, bool arrow)
    {
        const auto& style = GetPartStyle(part);
        auto color = style.m_Color;
        color.m_A *= opacity * style.m_Opacity;
        renderer.PushState(style.m_Transform.Resolve(rect));
        const float x = rect.m_X + rect.m_Width * 0.5f, y = rect.m_Y + rect.m_Height * 0.5f;
        const float stroke = std::max(0.5f, style.m_Width.Resolve(2, 2));
        if (arrow)
        {
            const float sign = rtl ? -1.0f : 1.0f;
            renderer.DrawLine({x - sign * 2, y - 4}, {x + sign * 2, y}, color, stroke, GetClip().Intersect(rect));
            renderer.DrawLine({x + sign * 2, y}, {x - sign * 2, y + 4}, color, stroke, GetClip().Intersect(rect));
        }
        else
        {
            renderer.DrawLine({x - 5, y}, {x - 1, y + 4}, color, stroke, GetClip().Intersect(rect));
            renderer.DrawLine({x - 1, y + 4}, {x + 6, y - 5}, color, stroke, GetClip().Intersect(rect));
        }
        renderer.PopState();
    };
    if (IsCheckable() && IsChecked())
        line("mark", leading, false);
    if (m_HasSubmenu)
        line("arrow", trailing, true);
}

SemanticInfo MenuItem::DescribeSemantics() const
{
    auto info = ToolButton::DescribeSemantics();
    info.m_Role = SemanticRole::MenuItem;
    if (m_HasSubmenu)
    {
        const auto* menu = dynamic_cast<const ContextMenu*>(m_Submenu.Get());
        info.m_Expanded = menu && menu->IsOpen();
        info.m_Expandable = true;
        info.m_Actions = info.m_Actions | SemanticAction::Expand | SemanticAction::Collapse;
    }
    if (!m_Hint.empty())
        info.m_Description = m_Hint;
    return info;
}

SemanticStatus MenuItem::OnSemanticAction(const SemanticCommand& command)
{
    if (m_HasSubmenu && (command.m_Action == SemanticAction::Expand || command.m_Action == SemanticAction::Collapse))
    {
        if (auto* menu = dynamic_cast<ContextMenu*>(m_Submenu.Get()); menu && menu->IsOpen())
        {
            if (command.m_Action == SemanticAction::Collapse)
                menu->Close();
            return SemanticStatus::Succeeded;
        }
        if (command.m_Action == SemanticAction::Expand)
            Trigger();
        return SemanticStatus::Succeeded;
    }
    return ToolButton::OnSemanticAction(command);
}
} // namespace EStudioUI
