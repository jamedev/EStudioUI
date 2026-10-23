#include <EStudioUI/NumericInput.h>
#include <EStudioUI/SplitToolButton.h>
#include <EStudioUI/TabWidget.h>
#include <EStudioUI/ToolBar.h>
#include <EStudioUI/UiContext.h>

#include <cmath>
#include <iostream>
#include <stdexcept>

namespace
{
using namespace EStudioUI;
int s_Checks = 0;

void Check(bool condition, const std::string& message)
{
    ++s_Checks;
    if (!condition)
        throw std::runtime_error(message);
}

bool Near(float a, float b)
{
    return std::abs(a - b) < 0.02f;
}

void Apply(UiContext& ui, std::string_view css)
{
    std::string error;
    Check(ui.SetStyleSheet(css, error), "composite CSS parses: " + error);
    ui.Prepare(800, 600);
}

ToolButton* FindTabButton(Widget& widget, const std::string& text)
{
    if (auto* button = dynamic_cast<ToolButton*>(&widget); button && button->GetText() == text)
        return button;
    for (const auto& child : widget.GetChildren())
        if (auto* found = FindTabButton(*child, text))
            return found;
    return nullptr;
}

void CompositeMetrics(FontFace& font)
{
    UiContext ui(font);
    auto& toolbar = ui.Root().Add<ToolBar>();
    toolbar.AddButton("Command");
    toolbar.AddSeparator();
    auto& chooser = ui.Root().Add<SplitToolButton>();
    chooser.AddAction("Line");
    auto& numeric = ui.Root().Add<NumericInput>();
    auto& tabs = ui.Root().Add<TabWidget>();
    tabs.AddTab("First");
    tabs.AddTab("Second");
    auto& menu = ui.Root().Add<ContextMenu>();
    menu.AddAction("Action");
    menu.AddSeparator();
    menu.SetVisible(true);
    Apply(ui, "toolbar { gap: 17px; } .toolbar-separator { width: 5px; } "
              "splittoolbutton { gap: 7px; } .split-arrow { width: 47px; } "
              "numericinput { gap: 9px; } .numeric-increment, .numeric-decrement { width: 39px; } "
              "tabbar { gap: 19px; } contextmenu { gap: 11px; } .menu-separator { height: 4px; }");
    Check(Near(toolbar.GetStyle().m_Gap, 17), "toolbar gap is theme controlled");
    Check(Near(toolbar.GetChildren()[1]->GetBounds().m_Width, 5), "toolbar separator CSS width reaches layout");
    Check(Near(chooser.GetStyle().m_Gap, 7), "split-button gap is theme controlled");
    Check(Near(chooser.GetChildren()[1]->GetBounds().m_Width, 47), "split-arrow CSS width reaches hit geometry");
    Check(Near(numeric.GetStyle().m_Gap, 9), "numeric input gap is theme controlled");
    Check(Near(numeric.GetChildren()[0]->GetBounds().m_Width, 39), "numeric stepper CSS width reaches layout");
    Check(Near(FindTabButton(tabs, "First")->GetParent()->GetStyle().m_Gap, 19), "tab strip gap is theme controlled");
    Check(Near(menu.GetStyle().m_Gap, 11), "popup gap is theme controlled");
    Check(Near(menu.GetChildren()[1]->GetBounds().m_Height, 4), "popup separator CSS height reaches layout");
    chooser.GetChildren()[1]->SetWidth(SizeRule::Fixed(31));
    toolbar.SetGap(3);
    ui.Prepare(800, 600);
    Check(Near(chooser.GetChildren()[1]->GetBounds().m_Width, 31), "explicit application size overrides cosmetic CSS");
    Check(Near(toolbar.GetStyle().m_Gap, 3), "explicit application gap overrides cosmetic CSS");
}

void TabSelection(FontFace& font)
{
    UiContext ui(font);
    auto& tabs = ui.Root().Add<TabWidget>();
    tabs.AddTab("First");
    tabs.AddTab("Second");
    auto& first = *FindTabButton(tabs, "First");
    auto& second = *FindTabButton(tabs, "Second");
    Apply(ui, "tabbar .tab-button { background: blue; } tabbar .tab-button:selected { background: red; }");
    Check(first.GetStyle().m_Background.m_R > 0.9f && second.GetStyle().m_Background.m_B > 0.9f,
          "selected tab exposes a semantic CSS state");
    tabs.SetCurrentIndex(1);
    ui.Prepare(800, 600);
    Check(second.GetStyle().m_Background.m_R > 0.9f && first.GetStyle().m_Background.m_B > 0.9f,
          "tab selection updates both selector styles");
    const WidgetRef tabsRef(&tabs);
    auto& command = dynamic_cast<ToolButton&>(first);
    command.SetCheckable(true);
    command.Toggled().Connect([&](bool) { ui.Root().RemoveChild(tabs); });
    command.Clicked().Emit();
    Check(!tabsRef.Get(), "tab command tolerates owner deletion during an earlier toggle callback");
}

void PopupAncestry(FontFace& font)
{
    UiContext ui(font);
    auto& panel = ui.Root().Add<Widget>();
    panel.AddClass("copper");
    auto& chooser = panel.Add<SplitToolButton>();
    chooser.AddAction("Line");
    chooser.AddAction("Arc");
    Apply(ui, ".copper { color: #aa4411; } .copper .split-menu { background: #123456; gap: 8px; } "
              ".copper .split-menu .menu-action { color: #44cc88; } "
              ".paper .split-menu { background: #eeeeee; }");
    chooser.OpenMenu();
    ui.Prepare(800, 600);
    auto* popup = ui.GetPopup();
    Check(popup && chooser.IsMenuOpen(), "composed popup opens through the normal overlay layer");
    Check(Near(popup->GetStyle().m_Background.m_R, 18.0f / 255.0f), "popup matches logical owner ancestry");
    Check(Near(chooser.GetAction(0).GetStyle().m_Color.m_G, 204.0f / 255.0f),
          "popup descendants inherit selector scope");
    panel.RemoveClass("copper");
    panel.AddClass("paper");
    ui.Prepare(800, 600);
    Check(Near(popup->GetStyle().m_Background.m_R, 238.0f / 255.0f), "open popup follows its owner's theme changes");
    const WidgetRef popupRef(popup);
    ui.Root().RemoveChild(panel);
    ui.Prepare(800, 600);
    Check(!popupRef.Get() && !ui.GetPopup(), "owner deletion prunes its root-owned popup safely");
}
} // namespace

int main()
{
    try
    {
        FontFace font(ESTUDIOUI_FONT_PATH);
        CompositeMetrics(font);
        TabSelection(font);
        PopupAncestry(font);
        std::cout << "Composite styles PASS / " << s_Checks << " checks\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Composite styles FAIL / " << error.what() << '\n';
        return 1;
    }
}
