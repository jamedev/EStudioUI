#include "CommandGallery.h"
#include <EStudioUI/CheckBox.h>
#include <EStudioUI/GroupBox.h>
#include <EStudioUI/InputDialog.h>
#include <EStudioUI/MenuBar.h>
#include <EStudioUI/MessageBox.h>
#include <EStudioUI/ScrollArea.h>
#include <EStudioUI/Slider.h>
#include <glad/gl.h>

#include <cmath>
#include <fstream>
#include <iterator>
#include <stdexcept>

namespace EStudioUI
{
namespace
{
void Check(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(std::string("Command/dialog GPU: ") + message);
}

std::string Read(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
        throw std::runtime_error("Missing stylesheet: " + path.string());
    return {std::istreambuf_iterator<char>(stream), {}};
}

std::vector<unsigned char> Pixels()
{
    GLint viewport[4]{};
    glGetIntegerv(GL_VIEWPORT, viewport);
    std::vector<unsigned char> pixels(static_cast<std::size_t>(viewport[2]) * static_cast<std::size_t>(viewport[3]) *
                                      4);
    glReadPixels(0, 0, viewport[2], viewport[3], GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    return pixels;
}

void Status(WidgetRef target, std::string text)
{
    if (auto* live = dynamic_cast<Label*>(target.Get()))
        live->SetText(std::move(text));
}
} // namespace

CommandGallery::CommandGallery(Window& window, Widget& page) : m_Window(window)
{
    page.AddClass("command-gallery");
    auto& scroller = page.Add<ScrollArea>();
    scroller.SetHeight(SizeRule::Fill());
    auto& surface = scroller.Content();
    surface.SetGap(14);
    auto& heading = surface.Add<Label>("Commands and dialogs, without a nested event loop.");
    heading.AddClass("command-heading");
    heading.SetWordWrap(true);
    heading.SetHeight(SizeRule::Content());
    auto& note = surface.Add<Label>("Menus share application actions. Prompts return owned results. Every surface uses "
                                    "the same GPU renderer and CSS.");
    note.SetWordWrap(true);
    note.SetHeight(SizeRule::Content());
    m_Menu = &surface.Add<MenuBar>();
    m_Menu->SetAccessibleName("Sketch commands");
    auto& file = m_Menu->AddMenu("File");
    auto& edit = m_Menu->AddMenu("Edit");
    auto& view = m_Menu->AddMenu("View");
    auto& status = surface.Add<Label>("Ready. Try a menu, a dialog or the anchored popover.");
    status.AddClass("command-status");
    status.SetWordWrap(true);
    status.SetHeight(SizeRule::Content());
    const WidgetRef output(&status);
    auto save = window.Ui().CreateAction("Save sketch");
    save->SetShortcut({Key::S, ShortcutModifier::Primary});
    save->Triggered().Connect([output]
                              { Status(output, "Save command executed. The application owns its document I/O."); });
    file.AddAction(save).SetIcon("S");
    auto& recent = file.AddMenu("Recent sketches");
    recent.AddAction("Bracket sketch")
        .Clicked()
        .Connect([output] { Status(output, "Recent command: Bracket sketch"); });
    recent.AddAction("Gear profile").Clicked().Connect([output] { Status(output, "Recent command: Gear profile"); });
    file.AddSeparator();
    file.AddAction("Export (unavailable)", false);
    edit.AddAction("Undo").SetShortcutHint("Ctrl+Z");
    edit.AddAction("Redo").SetShortcutHint("Ctrl+Shift+Z");
    auto grid = window.Ui().CreateAction("Show grid");
    grid->SetCheckable(true);
    grid->SetChecked(true);
    grid->Triggered().Connect(
        [output, state = std::weak_ptr<Action>(grid)]
        {
            if (auto live = state.lock())
                Status(output, live->GetSnapshot()->m_Checked ? "Grid enabled" : "Grid disabled");
        });
    view.AddAction(grid);
    auto& group = surface.Add<GroupBox>("Asynchronous surfaces");
    group.SetHeight(SizeRule::Content());
    auto& body = group.Content();
    body.SetHeight(SizeRule::Content());
    body.SetGap(12);
    auto& row = body.Add<Widget>();
    row.SetLayoutDirection(Axis::Horizontal);
    row.SetGap(10);
    row.SetHeight(SizeRule::Content());
    auto& confirm = row.Add<Button>("Save confirmation");
    confirm.SetWidth(SizeRule::Fill());
    auto& input = row.Add<Button>("Validated input");
    input.SetWidth(SizeRule::Fill());
    auto& tools = row.Add<Button>("Modeless tools");
    tools.SetWidth(SizeRule::Fill());
    m_Confirm = &MessageBox::Create(confirm, "Unsaved sketch",
                                    "Save the current sketch before closing? Results distinguish Save, Discard and "
                                    "Cancel; the application chooses what to do next.");
    m_Confirm->SetKind(MessageKind::Question);
    m_Confirm->SetButtons({StandardButton::Save, StandardButton::Discard, StandardButton::Cancel});
    const WidgetRef confirmation(m_Confirm);
    confirm.Clicked().Connect(
        [confirmation, output]
        {
            if (auto* live = dynamic_cast<MessageBox*>(confirmation.Get()))
                live->Open().OnFinished(
                    [output](PopupResult result)
                    {
                        Status(output, "Confirmation ended: response " + std::to_string(result.m_Value) + ", cause " +
                                           std::to_string(static_cast<int>(result.m_Reason)));
                    });
        });
    m_Input = &InputDialog::Create(input, "Grid spacing", "Enter grid spacing between 1 and 1000", "25");
    m_Input->Editor().SetValidator(std::make_shared<IntegerValidator>(1, 1000));
    const WidgetRef prompt(m_Input);
    input.Clicked().Connect(
        [prompt, output]
        {
            if (auto* live = dynamic_cast<InputDialog*>(prompt.Get()))
                live->Open().OnFinished(
                    [output](InputDialogResult result)
                    {
                        Status(output,
                               result.m_Text ? "Accepted grid spacing: " + *result.m_Text : "Grid spacing cancelled");
                    });
        });
    m_Tools = &Dialog::Create(tools, "Display tools");
    m_Tools->SetButtons({StandardButton::Close});
    m_Tools->Content().Add<CheckBox>("Show construction geometry").SetChecked(true);
    auto& transparency = m_Tools->Content().Add<Slider>();
    transparency.SetValue(70);
    transparency.SetAccessibleName("Geometry opacity");
    auto& toolNote =
        m_Tools->Content().Add<Label>("This surface stays open while you interact with the workspace behind it.");
    toolNote.SetWordWrap(true);
    toolNote.SetHeight(SizeRule::Content());
    const WidgetRef toolDialog(m_Tools);
    tools.Clicked().Connect(
        [toolDialog]
        {
            if (auto* live = dynamic_cast<Dialog*>(toolDialog.Get()))
                live->Open(false);
        });
    auto& popoverButton = body.Add<Button>("Anchored popover");
    popoverButton.SetWidth(SizeRule::Content());
    m_Anchor = WidgetRef(&popoverButton);
    m_Popover = &Popup::Create(popoverButton);
    m_Popover->Add<Label>("Quick sketch options").SetHeight(SizeRule::Content());
    m_Popover->Add<CheckBox>("Snap to grid").SetChecked(true);
    m_Popover->Add<CheckBox>("Show dimensions");
    auto& done = m_Popover->Add<Button>("Done");
    const WidgetRef popup(m_Popover);
    done.Clicked().Connect(
        [popup]
        {
            if (auto* live = dynamic_cast<Popup*>(popup.Get()))
                live->Close({PopupCloseReason::Accepted, 1});
        });
    const WidgetRef anchor(&popoverButton);
    popoverButton.Clicked().Connect(
        [popup, anchor]
        {
            auto* live = dynamic_cast<Popup*>(popup.Get());
            if (live && anchor.Get())
                live->Open(*anchor.Get());
        });
    auto& help =
        surface.Add<Label>("Keyboard: Tab to the menu bar, Down to open, arrows to navigate, Escape to dismiss. Dialog "
                           "Enter uses its default response; Apply/Help stay open. Resize while a dialog is open.");
    help.SetWordWrap(true);
    help.SetHeight(SizeRule::Content());
}

void CommandGallery::Verify(const std::filesystem::path& assets, const std::filesystem::path& output)
{
    WindowOptions options;
    options.m_Width = 900;
    options.m_Height = 620;
    options.m_Headless = m_Window.IsHeadless();
    Window probe(options, assets / "NotoSans.ttf");
    auto& ui = probe.Ui();
    auto& gpu = probe.Graphics();
    const auto css =
        Read(assets / "dark.css") + Read(assets / "essential-widgets.css") + Read(assets / "command-dialogs.css");
    std::string error;
    Check(ui.SetStyleSheet(css, error), "rich command/dialog stylesheet parses");
    CommandGallery gallery(probe, ui.Root());
    probe.RenderFrame(1);
    probe.RenderFrame(2);
    Check(!ui.HasAnimations() && gpu.GetStats().m_RepaintPixels == 0,
          "closed gallery remains idle with zero repaint pixels");
    Check(gallery.m_Anchor.Get()->GetClip().Intersect(gallery.m_Anchor.Get()->GetBounds()).m_Height > 20,
          "interactive popover trigger is actually visible in the gallery");
    gpu.SavePpm(output / "commands-gallery-0-19.ppm");
    auto menu = gallery.m_Menu->OpenMenu(0);
    probe.RenderFrame(3);
    const auto menuPixels = Pixels();
    Check(gpu.GetStats().m_Glyphs > 20 && gpu.GetStats().m_Glyphs < 1500,
          "menu caption/icon/shortcut glyph work stays bounded");
    gpu.SavePpm(output / "commands-menu-0-19.ppm");
    const auto styleWork = ui.GetStats().m_StyleResolutions;
    probe.RenderFrame(4);
    Check(gpu.GetStats().m_RepaintPixels == 0 && ui.GetStats().m_StyleResolutions == styleWork,
          "open static menus perform no extra style or repaint work");
    gpu.SetPartialRedrawEnabled(false);
    probe.RenderFrame(5);
    Check(Pixels() == menuPixels, "menu retained framebuffer matches the full-render reference exactly");
    gpu.SetPartialRedrawEnabled(true);
    gallery.m_Menu->CloseMenu();
    auto input = gallery.m_Input->Open();
    probe.RenderFrame(6);
    gpu.SavePpm(output / "commands-input-0-19.ppm");
    const auto before = gallery.m_Input->GetBounds();
    Check(std::abs(2 * before.m_X + before.m_Width - 900) < 0.01f, "actual rendered prompt starts centered");
    probe.SetSize(680, 460);
    probe.RenderFrame(7);
    const auto after = gallery.m_Input->GetBounds();
    Check(std::abs(2 * after.m_X + after.m_Width - 680) < 0.01f && before.m_X != after.m_X,
          "intermediate resize repositions the actual rendered prompt");
    gpu.SavePpm(output / "commands-resize-0-19.ppm");
    gallery.m_Input->Editor().SetText("-");
    gallery.m_Input->Accept();
    Check(input.IsPending(), "invalid/intermediate input cannot complete a prompt");
    gallery.m_Input->Editor().SetText("48");
    ui.KeyEvent(Key::Enter, true);
    Check(input.GetResult()->m_Text == "48", "Enter commits an owned validated input result");
    auto confirmation = gallery.m_Confirm->Open();
    probe.RenderFrame(8);
    gpu.SavePpm(output / "commands-confirm-0-19.ppm");
    gallery.m_Confirm->Buttons().GetButton(StandardButton::Discard)->Trigger();
    Check(confirmation.GetResult()->m_Value == static_cast<int>(StandardButton::Discard),
          "GPU dialog uses the same stable destructive response");
    auto popup = gallery.m_Popover->Open(*gallery.m_Anchor.Get());
    probe.RenderFrame(9);
    gpu.SavePpm(output / "commands-popover-0-19.ppm");
    const auto overlaid = Pixels();
    gpu.SetPartialRedrawEnabled(false);
    probe.RenderFrame(10);
    Check(overlaid == Pixels(), "popover shadow/content retained pixels match full rendering");
    gallery.m_Popover->Close();
    gpu.SetPartialRedrawEnabled(true);
    probe.RenderFrame(11);
    probe.RenderFrame(12);
    Check(gpu.GetStats().m_RepaintPixels == 0, "closing surfaces restores an idle framebuffer");
    Check(ui.SetStyleSheet(Read(assets / "light.css") + Read(assets / "essential-widgets.css") +
                               Read(assets / "command-dialogs.css"),
                           error),
          "light-theme command/dialog composition parses");
    gallery.m_Confirm->Open();
    probe.RenderFrame(13);
    gpu.SavePpm(output / "commands-light-0-19.ppm");
    gallery.m_Confirm->Reject();
    std::ofstream report(output / "commands-report.txt");
    report << "EStudioUI 0.19 commands/dialogs: PASS\nReal framebuffer menu, prompt, confirmation, popover and resize "
              "captures.\n"
           << "Static gallery/menu: zero repaint pixels; no continuously requested frames.\n"
           << "Retained/full menu and popover pixels match exactly.\n"
           << "Validated owned input result; stable confirmation response; centered intermediate resize.\n";
}
} // namespace EStudioUI
