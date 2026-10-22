#include <EStudioUI/SemanticTree.h>
#include <EStudioUI/UiContext.h>
#include <EStudioUI/Button.h>
#include <EStudioUI/ToolButton.h>
#include <EStudioUI/TextBox.h>
#include <EStudioUI/Slider.h>
#include <EStudioUI/NumericInput.h>
#include <EStudioUI/ContextMenu.h>
#include <EStudioUI/MemoryPlatformServices.h>

#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>

namespace
{
using namespace EStudioUI;
int s_Checks = 0;

void Check(bool value, const char* message)
{
    ++s_Checks;
    if (!value)
        throw std::runtime_error(message);
}

template <typename Callback> void Throws(Callback callback, const char* message)
{
    bool failed = false;
    try
    {
        callback();
    }
    catch (const std::exception&)
    {
        failed = true;
    }
    Check(failed, message);
}

class Custom : public Widget
{
  public:
    mutable bool m_TryMutation = false;
    int m_Invocations = 0;

  protected:
    SemanticInfo DescribeSemantics() const override
    {
        if (m_TryMutation)
            const_cast<Custom*>(this)->Add<Widget>();
        SemanticInfo info;
        info.m_Role = SemanticRole::Button;
        info.m_Name = "Custom paint control";
        info.m_Actions = SemanticAction::Invoke;
        return info;
    }

    SemanticStatus OnSemanticAction(const SemanticCommand&) override
    {
        ++m_Invocations;
        return SemanticStatus::Succeeded;
    }
};

void Semantics(FontFace& font)
{
    UiContext ui(font);
    auto& root = ui.Root();
    auto& button = root.Add<Button>("Launch");
    button.SetId("launch");
    auto& text = root.Add<TextBox>("Original");
    text.SetAccessibleName("Project name");
    auto& toggle = root.Add<ToolButton>("Grid");
    toggle.SetCheckable(true);
    auto& slider = root.Add<Slider>();
    slider.SetRange(-10, 20);
    auto& number = root.Add<NumericInput>(2);
    auto& custom = root.Add<Custom>();
    ui.Prepare(800, 600);
    auto& tree = ui.Semantics();
    const auto buttonId = tree.GetId(button);
    const auto rootId = tree.GetRoot();
    auto node = tree.GetNode(buttonId);
    Check(node && node->m_Info.m_Name == "Launch" && node->m_Info.m_Role == SemanticRole::Button,
          "button caption and role");
    Check(node->m_Parent == rootId && node->m_AutomationId == "launch" && node->m_Enabled,
          "logical hierarchy and automation id");
    Check(tree.GetNode(rootId)->m_Info.m_Role == SemanticRole::Window, "window root role");
    Check(!node->m_ScreenBounds && !node->m_Offscreen, "headless bounds do not pretend to be desktop coordinates");
    Check(tree.GetChildren(rootId, 1, 2).size() == 2, "bounded child pages");
    Throws([&] { tree.GetChildren(rootId, 0, 257); }, "oversized pages rejected");
    const auto bounds = node->m_Bounds;
    Check(tree.HitTest({bounds.m_X + bounds.m_Width / 2, bounds.m_Y + bounds.m_Height / 2}) == buttonId,
          "semantic hit test shares widget input geometry");
    const auto before = ui.GetStats();
    for (int i = 0; i < 50; ++i)
        tree.GetNode(buttonId);
    Check(ui.GetStats().m_LayoutPasses == before.m_LayoutPasses &&
              ui.GetStats().m_StyleResolutions == before.m_StyleResolutions,
          "semantic queries do not prepare frames");
    int invoked = 0;
    button.Clicked().Connect([&] { ++invoked; });
    Check(tree.Perform(buttonId, {SemanticAction::Invoke}) == SemanticStatus::Succeeded && invoked == 1,
          "canonical button activation");
    button.SetEnabled(false);
    Check(tree.Perform(buttonId, {SemanticAction::Invoke}) == SemanticStatus::Disabled && invoked == 1,
          "disabled semantic activation rejected");
    button.SetEnabled(true);
    Check(tree.Perform(tree.GetId(text), {SemanticAction::Focus}) == SemanticStatus::Succeeded &&
              ui.GetFocus() == &text,
          "semantic focus uses canonical focus path");
    Check(tree.GetFocus() == tree.GetId(text) && tree.GetNode(tree.GetId(text))->m_Focused, "queried real focus");
    Check(tree.Perform(tree.GetId(text), {SemanticAction::SetText, "Renamed Ω"}) == SemanticStatus::Succeeded &&
              text.GetText() == "Renamed Ω",
          "Unicode value action");
    text.SetReadOnly(true);
    Check(tree.GetNode(tree.GetId(text))->m_Info.m_ReadOnly &&
              tree.Perform(tree.GetId(text), {SemanticAction::SetText, "Denied"}) == SemanticStatus::Unsupported,
          "read-only values cannot be changed");
    Check(tree.Perform(tree.GetId(toggle), {SemanticAction::Toggle}) == SemanticStatus::Succeeded && toggle.IsChecked(),
          "canonical toggle activation");
    Check(tree.Perform(tree.GetId(slider), {SemanticAction::SetNumber, {}, 7.5}) == SemanticStatus::Succeeded &&
              slider.GetValue() == 7.5,
          "slider range action");
    Check(tree.Perform(tree.GetId(slider), {SemanticAction::SetNumber, {}, 30}) == SemanticStatus::InvalidValue &&
              slider.GetValue() == 7.5,
          "out of range changes rejected without clamping");
    Check(tree.Perform(tree.GetId(number), {SemanticAction::SetNumber, {}, 9}) == SemanticStatus::Succeeded &&
              number.GetValue() == 9,
          "composite numeric control action");
    Check(tree.Perform(tree.GetId(custom), {SemanticAction::Invoke}) == SemanticStatus::Succeeded &&
              custom.m_Invocations == 1,
          "custom control semantics and interaction hooks");
    custom.m_TryMutation = true;
    Throws([&] { tree.GetNode(tree.GetId(custom)); }, "semantic hooks reject tree mutation");
    Check(ui.GetPhase() == UiPhase::Idle && custom.GetChildren().empty(), "inspection phase restored after error");
    custom.m_TryMutation = false;
    const auto customId = tree.GetId(custom);
    root.RemoveChild(custom);
    Check(!tree.GetNode(customId) && tree.Perform(customId, {SemanticAction::Invoke}) == SemanticStatus::Unavailable,
          "removed providers expire by lifetime identity");
    auto& replacement = root.Add<Custom>();
    Check(tree.GetId(replacement) != customId, "replacement cannot retarget an old identity");
    replacement.SetVisible(false);
    Check(tree.GetNode(tree.GetId(replacement))->m_Offscreen, "hidden state");
    auto& owned = ui.AddOwnedPopup(std::make_unique<ContextMenu>(), button);
    ui.ShowPopup(owned, {50, 100});
    ui.Prepare(800, 600);
    const auto children = tree.GetChildren(buttonId);
    Check(children.size() == 1 && children.front() == tree.GetId(owned) &&
              tree.GetNode(tree.GetId(owned))->m_Parent == buttonId,
          "owned popup has one logical parent");
    ui.CloseAllPopups();
    Check(tree.GetChildren(buttonId).empty() && tree.GetNode(tree.GetId(owned))->m_Offscreen,
          "closed popup disappears from child navigation");
    auto& modal = root.Add<Widget>();
    ui.ShowPopup(modal, {100, 100}, PopupOptions::Modal());
    Check(!tree.GetNode(buttonId)->m_Enabled &&
              tree.Perform(buttonId, {SemanticAction::Invoke}) == SemanticStatus::Disabled,
          "modal barrier blocks semantic actions");
    ui.CloseAllPopups();
    PlatformHost host;
    host.m_Dispatcher = ui.GetDispatcher();
    host.m_GetMetrics = []
    {
        WindowMetrics metrics;
        metrics.m_LogicalSize = {800, 600};
        metrics.m_FramebufferSize = {1200, 900};
        metrics.m_PixelScale = {1.5f, 1.5f};
        metrics.m_ScreenOrigin = {-900, 40};
        metrics.m_HasScreenCoordinates = true;
        return metrics;
    };
    ui.SetPlatformServices(std::make_unique<MemoryPlatformServices>(host));
    ui.Prepare(800, 600);
    node = tree.GetNode(buttonId);
    Check(node->m_ScreenBounds && node->m_ScreenBounds->m_X == -900 + node->m_Bounds.m_X * 1.5f,
          "physical desktop bounds use DPI and negative monitor origin");
    bool crossThreadRejected = false;
    std::thread worker(
        [&]
        {
            try
            {
                tree.GetNode(buttonId);
            }
            catch (const std::logic_error&)
            {
                crossThreadRejected = true;
            }
        });
    worker.join();
    Check(crossThreadRejected, "direct worker semantic queries rejected before touching widgets");
    auto& deleting = root.Add<Button>("Delete self");
    const auto deletingId = tree.GetId(deleting);
    deleting.Clicked().Connect([&] { root.RemoveChild(deleting); });
    Check(tree.Perform(deletingId, {SemanticAction::Invoke}) == SemanticStatus::Succeeded && !tree.GetNode(deletingId),
          "semantic activation permits receiver deletion");
}
} // namespace

int main()
{
    try
    {
        FontFace font(ESTUDIOUI_FONT_PATH);
        Semantics(font);
        std::cout << "Semantics: " << s_Checks << " checks passed.\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
