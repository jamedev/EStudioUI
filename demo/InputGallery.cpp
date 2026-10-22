#include "InputGallery.h"

#include <EStudioUI/ContextMenu.h>
#include <EStudioUI/TextBox.h>
#include <EStudioUI/ToolBar.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <fstream>
#include <cmath>
#include <iterator>
#include <stdexcept>

namespace EStudioUI
{
namespace
{
void Check(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(std::string("Input gallery: ") + message);
}

template <class T = Widget> T& Live(const WidgetRef& reference)
{
    auto* widget = dynamic_cast<T*>(reference.Get());
    if (!widget)
        throw std::logic_error("Input gallery target expired.");
    return *widget;
}

Label& Caption(Widget& owner, std::string text, float size = 14)
{
    auto& caption = owner.Add<Label>(std::move(text));
    caption.SetWordWrap(true);
    caption.SetStyleProperty(StyleProperty::FontSize, size);
    return caption;
}

Point Center(const Widget& widget)
{
    const auto bounds = widget.GetBounds();
    return {bounds.m_X + bounds.m_Width / 2, bounds.m_Y + bounds.m_Height / 2};
}

void Click(UiContext& ui, Widget& widget)
{
    const auto position = Center(widget);
    ui.PointerDown(position);
    ui.PointerUp(position);
}

std::string Bytes(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

class NativeInputProbe : public TextBox
{
  public:
    std::optional<KeyboardEvent> m_Key;
    std::optional<PointerEvent> m_Pointer;
    std::function<void()> m_Next;
    mutable int m_Paints = 0;
    mutable std::function<void()> m_PaintOnce;

  protected:
    bool OnInput(const InputEvent& event) override
    {
        if (const auto* key = std::get_if<KeyboardEvent>(&event))
            m_Key = *key;
        if (const auto* pointer = std::get_if<PointerEvent>(&event))
            m_Pointer = *pointer;
        if (m_Next)
        {
            auto callback = std::exchange(m_Next, {});
            callback();
        }
        return TextBox::OnInput(event);
    }

    void Paint(Renderer& renderer, FontFace& font, double time, float opacity) const override
    {
        ++m_Paints;
        if (m_PaintOnce)
        {
            auto callback = std::exchange(m_PaintOnce, {});
            callback();
        }
        TextBox::Paint(renderer, font, time, opacity);
    }
};

void VerifyNativeCallbacks(Window& owner, const std::filesystem::path& assets)
{
    WindowOptions options;
    options.m_Headless = owner.IsHeadless();
    options.m_Width = 360;
    options.m_Height = 180;
    Window window(options, assets / "NotoSans.ttf");
    auto& probe = window.Ui().Root().Add<NativeInputProbe>();
    probe.SetHeight(SizeRule::Fixed(40));
    window.RenderFrame();
    window.Ui().SetFocus(&probe);
    auto* native = reinterpret_cast<GLFWwindow*>(window.GetContextIdentity());
    auto key = glfwSetKeyCallback(native, nullptr);
    glfwSetKeyCallback(native, key);
    Check(key != nullptr, "production native key callback installed");
    key(native, GLFW_KEY_S, 123, GLFW_PRESS, GLFW_MOD_CONTROL);
    Check(probe.m_Key && probe.m_Key->m_PhysicalKey == Key::S && probe.m_Key->m_ScanCode == 123 &&
              probe.m_Key->m_Modifiers.m_PhysicalControl && probe.m_Key->m_Timestamp >= 0,
          "production key callback preserves physical/scancode/modifier/time metadata");
    auto text = glfwSetCharCallback(native, nullptr);
    glfwSetCharCallback(native, text);
    Check(text != nullptr, "production native character callback installed");
    text(native, 0x00f1);
    Check(probe.GetText() == "\xc3\xb1", "native committed Unicode text uses the separate input payload");
    auto motion = glfwSetCursorPosCallback(native, nullptr);
    glfwSetCursorPosCallback(native, motion);
    const auto point = Center(probe);
    const auto scale = window.Platform().GetWindowMetrics().m_InputScale;
    motion(native, static_cast<double>(point.m_X) * scale.m_X, static_cast<double>(point.m_Y) * scale.m_Y);
    Check(probe.m_Pointer && probe.m_Pointer->m_Type == PointerEventType::Move &&
              probe.m_Pointer->m_Device == PointerDevice::Mouse && probe.m_Pointer->m_Timestamp >= 0 &&
              std::abs(probe.m_Pointer->m_Position.m_X - point.m_X) < 0.01f &&
              std::abs(probe.m_Pointer->m_Position.m_Y - point.m_Y) < 0.01f,
          "production pointer callback delivers normalized metadata");
    probe.m_Next = [&]
    {
        bool nestedLoopRejected = false;
        try
        {
            window.Run();
        }
        catch (const std::logic_error&)
        {
            nestedLoopRejected = true;
        }
        Check(nestedLoopRejected, "direct input delivery prevents nested native event pumping");
        const auto paints = probe.m_Paints;
        window.SetSize(400, 220);
        Check(!window.IsClosed() && probe.m_Paints == paints,
              "native resize callbacks defer presentation during input instead of closing the window");
        key(native, GLFW_KEY_A, 987, GLFW_PRESS, 0);
        Check(probe.m_Key && probe.m_Key->m_Key == Key::R,
              "reentrant native key callback queues owning input rather than delivering inside the current event");
    };
    window.Ui().KeyEvent(Key::R, true);
    window.Ui().GetDispatcher().Drain();
    Check(probe.m_Key && probe.m_Key->m_Key == Key::A && probe.m_Key->m_ScanCode == 987 && !window.IsClosed(),
          "queued native input is delivered at the next owner boundary");
    auto focus = glfwSetWindowFocusCallback(native, nullptr);
    glfwSetWindowFocusCallback(native, focus);
    probe.m_Next = [&]
    {
        key(native, GLFW_KEY_B, 654, GLFW_PRESS, 0);
        focus(native, 0);
    };
    window.Ui().SetFocus(&probe);
    window.Ui().KeyEvent(Key::R, true);
    window.Ui().GetDispatcher().Drain();
    Check(probe.m_Key && probe.m_Key->m_Key == Key::R,
          "native deactivation invalidates deferred input from the previous focus session");
    focus(native, 1);
    window.RenderFrame();
    Check(window.Ui().Root().GetBounds().m_Width == 400, "deferred input-time resize reaches the next real frame");
    window.Ui().SetFocus(&probe);
    probe.m_PaintOnce = [&]
    {
        focus(native, 0);
        Check(!window.IsClosed(), "native focus loss during paint queues UI deactivation safely");
    };
    window.Ui().RequestRepaint();
    window.RenderFrame();
    window.Ui().GetDispatcher().Drain();
    Check(!window.IsClosed() && !window.Ui().GetFocus(), "deferred native focus applies after frame traversal");
    window.Close();
}
} // namespace

InputGallery::InputGallery(Window& window, Widget& page) : m_Window(window)
{
    Caption(page, "Input, actions & focus", 24);
    Caption(page, "One command drives its toolbar, button, menu and shortcut. Try Ctrl+S, the Tools submenu, "
                  "and a settings panel with its own popup. Escape closes one layer at a time.");
    auto& ui = window.Ui();
    m_Save = ui.CreateAction("Save document");
    m_Save->SetShortcut({Key::S, ShortcutModifier::Primary});
    m_Grid = ui.CreateAction("Show grid");
    m_Grid->SetCheckable(true);
    m_Grid->SetShortcut({Key::G, ShortcutModifier::Primary});
    auto& toolbar = page.Add<ToolBar>();
    m_SaveButton = WidgetRef(&toolbar.AddAction(m_Save));
    toolbar.AddAction(m_Grid);
    auto& menuButton = toolbar.AddButton("Commands...");
    auto& settings = toolbar.AddButton("Settings...");
    m_SettingsButton = WidgetRef(&settings);
    auto& editor = page.Add<TextBox>("Edit text, then use the shared Save shortcut");
    editor.SetHeight(SizeRule::Fixed(42));
    m_Editor = WidgetRef(&editor);
    auto& secondary = page.Add<Button>();
    secondary.SetAction(m_Save);
    secondary.SetWidth(SizeRule::Fixed(220));
    auto& group = page.Add<Widget>();
    group.AddClass("gallery-pane");
    group.SetFocusScope(true);
    Caption(group, "Local focus scope");
    Caption(group, "Tab and Shift+Tab cycle between these fields. Click elsewhere to leave the scope.");
    group.Add<TextBox>("First scoped field");
    group.Add<TextBox>("Second scoped field");
    m_Status = WidgetRef(&Caption(page, "Ready / shared actions and scoped input"));
    m_Connections.emplace_back(m_Save->Triggered().Connect(
        [this]
        {
            ++m_Executions;
            Live<Label>(m_Status).SetText("Saved " + std::to_string(m_Executions) +
                                          " time(s) through one shared action.");
        }));
    m_Connections.emplace_back(m_Grid->Triggered().Connect(
        [this]
        {
            Live<Label>(m_Status).SetText(m_Grid->GetSnapshot()->m_Checked ? "Grid enabled everywhere."
                                                                           : "Grid disabled everywhere.");
        }));
    auto menu = std::make_unique<ContextMenu>();
    menu->AddAction(m_Save);
    auto& tools = menu->AddMenu("Tools >");
    tools.AddAction(m_Grid);
    tools.AddAction(m_Save);
    m_Menu = WidgetRef(&ui.AddOwnedPopup(std::move(menu), menuButton));
    m_Connections.emplace_back(menuButton.Clicked().Connect(
        [menu = m_Menu, button = WidgetRef(&menuButton)]
        {
            if (auto* live = button.Get())
            {
                const auto bounds = live->GetBounds();
                Live<ContextMenu>(menu).Open({bounds.m_X, bounds.m_Y + bounds.m_Height});
            }
        }));
    auto& modal = ui.Root().Add<Widget>();
    modal.SetId("input-settings");
    modal.AddClass("gallery-pane");
    modal.SetWidth(SizeRule::Fixed(420));
    modal.SetHeight(SizeRule::Fixed(300));
    modal.SetVisible(false);
    modal.SetStyleProperty(StyleProperty::Background, Color{0.12f, 0.16f, 0.26f, 1});
    modal.SetStyleProperty(StyleProperty::Color, Color{0.94f, 0.96f, 1, 1});
    modal.SetStyleProperty(StyleProperty::BorderColor, Color{0.38f, 0.58f, 0.95f, 1});
    modal.SetStyleProperty(StyleProperty::BorderWidth, 2.0f);
    Caption(modal, "Asynchronous settings", 21);
    Caption(modal, "The application keeps running. The chooser opens above this modal surface.");
    m_ModalEditor = WidgetRef(&modal.Add<TextBox>("Workspace name"));
    auto& chooser = modal.Add<Button>("Choose preset...");
    auto preset = std::make_unique<ContextMenu>();
    preset->AddAction("Engineering");
    preset->AddAction("Compact");
    m_ModalMenu = WidgetRef(&ui.AddOwnedPopup(std::move(preset), chooser));
    m_Connections.emplace_back(chooser.Clicked().Connect(
        [menu = m_ModalMenu, button = WidgetRef(&chooser)]
        {
            if (auto* live = button.Get())
            {
                const auto bounds = live->GetBounds();
                Live<ContextMenu>(menu).Open({bounds.m_X, bounds.m_Y + bounds.m_Height});
            }
        }));
    auto& footer = modal.Add<ToolBar>();
    auto& accept = footer.AddButton("Apply");
    auto& cancel = footer.AddButton("Cancel");
    m_Modal = WidgetRef(&modal);
    m_Connections.emplace_back(accept.Clicked().Connect(
        [target = m_Modal]
        {
            if (auto* live = target.Get())
                live->GetContext()->ClosePopup(*live, {PopupCloseReason::Accepted, 1});
        }));
    m_Connections.emplace_back(cancel.Clicked().Connect(
        [target = m_Modal]
        {
            if (auto* live = target.Get())
                live->GetContext()->ClosePopup(*live, {PopupCloseReason::Rejected, 0});
        }));
    m_Connections.emplace_back(settings.Clicked().Connect([this] { OpenSettings(); }));
}

void InputGallery::OpenSettings()
{
    if (m_Session.IsPending())
        return;
    const auto bounds = m_Window.Ui().Root().GetBounds();
    m_Session = m_Window.Ui().ShowPopup(
        Live(m_Modal), {std::max(0.0f, (bounds.m_Width - 420) / 2), std::max(0.0f, (bounds.m_Height - 300) / 2)},
        PopupOptions::Modal());
    m_Session.OnFinished(
        [status = m_Status](PopupResult result)
        {
            if (auto* label = dynamic_cast<Label*>(status.Get()))
                if (result.m_Reason != PopupCloseReason::ContextClosed)
                    label->SetText(result.m_Reason == PopupCloseReason::Accepted ? "Settings applied asynchronously."
                                                                                 : "Settings closed.");
        });
}

void InputGallery::Verify(const std::filesystem::path& assets, const std::filesystem::path& output)
{
    auto& ui = m_Window.Ui();
    m_Window.SetSize(1180, 820);
    m_Window.RenderFrame(ui.GetTime());
    const int before = m_Executions;
    Click(ui, Live(m_SaveButton));
    ui.SetFocus(&Live(m_Editor));
    Modifiers primary;
    primary.m_Control = true;
#ifdef __APPLE__
    primary.m_Meta = true;
#else
    primary.m_PhysicalControl = true;
#endif
    ui.KeyEvent(Key::S, true, false, primary);
    Check(m_Executions == before + 2, "toolbar and focused editor shortcut share one command");
    m_Save->SetEnabled(false);
    ui.KeyEvent(Key::S, true, false, primary);
    Check(m_Executions == before + 2 && !Live(m_SaveButton).IsEnabled(),
          "disabled action synchronizes and blocks shortcuts");
    m_Save->SetEnabled(true);
    ui.KeyEvent(Key::G, true, false, primary);
    Check(m_Grid->GetSnapshot()->m_Checked, "shared checkable action toggles from a shortcut");
    Click(ui, Live(m_SettingsButton));
    Check(ui.GetFocus() == m_ModalEditor.Get(), "modal surface focuses its first editable child");
    auto& menu = Live<ContextMenu>(m_ModalMenu);
    const auto modalBounds = Live(m_Modal).GetBounds();
    auto child = menu.Open({modalBounds.m_X + 140, modalBounds.m_Y + 130});
    Check(ui.GetPopupCount() == 2 && m_Session.IsPending(), "chooser stacks above its modal parent");
    ui.RequestRepaint();
    m_Window.RenderFrame(ui.GetTime());
    m_Window.Graphics().SavePpm(output / "input-foundation.ppm");
    m_Window.Graphics().SetPartialRedrawEnabled(false);
    ui.RequestRepaint();
    m_Window.RenderFrame(ui.GetTime());
    m_Window.Graphics().SavePpm(output / "input-full-reference.ppm");
    Check(Bytes(output / "input-foundation.ppm") == Bytes(output / "input-full-reference.ppm"),
          "stacked modal/popup retained and full actual framebuffers match exactly");
    m_Window.Graphics().SetPartialRedrawEnabled(true);
    ui.KeyEvent(Key::Escape, true);
    Check(!child.IsPending() && m_Session.IsPending() && ui.GetPopupCount() == 1,
          "Escape dismisses one layer and preserves the modal session");
    auto& apply = *Live(m_Modal).GetChildren().back()->GetChildren().front();
    Click(ui, apply);
    Check(m_Session.GetResult() && m_Session.GetResult()->m_Reason == PopupCloseReason::Accepted &&
              ui.GetFocus() == m_SettingsButton.Get(),
          "Apply click settles the asynchronous result and restores pre-modal launcher focus");
    for (int frame = 0; frame < 3; ++frame)
        m_Window.RenderFrame(ui.GetTime());
    ui.RequestRepaint();
    m_Window.RenderFrame(ui.GetTime());
    Check(m_Window.Graphics().GetStats().m_SubmittedInstances == 0,
          "unchanged input page submits no retained GPU instances");
    VerifyNativeCallbacks(m_Window, assets);
    std::ofstream report(output / "input-foundation-report.txt");
    report << "F09 input/actions/focus/popup integration: PASS\n"
           << "Toolbar/menu/button/shortcut command state: PASS\n"
           << "Modal focus, nested chooser, asynchronous result and restoration: PASS\n"
           << "Production GLFW key/character/pointer callback metadata: PASS\n"
           << "Reentrant native input is queued; input-time resize defers native frames; nested native loops are "
              "rejected: PASS\n"
           << "Stacked retained/full actual framebuffer equality: PASS\n"
           << "Unchanged retained GPU instances submitted: 0\n"
           << "Native callbacks were invoked through their production adapters; this does not validate a real keyboard "
              "layout or IME.\n";
}
} // namespace EStudioUI
