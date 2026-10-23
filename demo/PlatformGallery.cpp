#include "PlatformGallery.h"

#include <EStudioUI/MemoryPlatformServices.h>
#include <EStudioUI/SemanticTree.h>
#include <EStudioUI/TextBox.h>
#include <EStudioUI/TextInputClient.h>
#include <EStudioUI/ToolBar.h>
#include <EStudioUI/Application.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <fstream>
#include <sstream>
#include <stdexcept>

namespace EStudioUI
{
namespace
{
template <typename T> T& Live(const WidgetRef& ref)
{
    auto* value = dynamic_cast<T*>(ref.Get());
    if (!value)
        throw std::logic_error("Platform gallery receiver expired.");
    return *value;
}

void Status(const WidgetRef& target, std::string message)
{
    if (auto* label = dynamic_cast<Label*>(target.Get()))
        label->SetText(std::move(message));
}

struct NativeWorkState
{
    bool m_Active = true;
    bool m_Destroyed = false;
};

class RetiringServices final : public PlatformServices
{
  public:
    RetiringServices(PlatformHost host, std::shared_ptr<NativeWorkState> state)
        : PlatformServices(std::move(host)), m_State(std::move(state))
    {
    }

    ~RetiringServices() override
    {
        Close();
        m_State->m_Destroyed = true;
    }

  protected:
    PlatformCapabilities DescribeCapabilities() const override
    {
        return {};
    }

    ClipboardResult ReadClipboardCore() override
    {
        return {};
    }

    PlatformResult WriteClipboardCore(const ClipboardData&) override
    {
        return {};
    }

    void BeginFileDialog(const FileDialogOptions&, std::stop_token,
                         std::function<void(FileDialogResult)> complete) override
    {
        complete({FileDialogStatus::Cancelled});
    }

    bool NativeWorkPending() const override
    {
        return m_State->m_Active;
    }

  private:
    std::shared_ptr<NativeWorkState> m_State;
};

struct EndNativeWork
{
    std::shared_ptr<NativeWorkState> m_State;

    ~EndNativeWork()
    {
        m_State->m_Active = false;
    }
};

const char* Support(PlatformSupport support)
{
    switch (support)
    {
    case PlatformSupport::Native:
        return "native";
    case PlatformSupport::Emulated:
        return "emulated";
    default:
        return "unsupported";
    }
}

void Check(bool value, const char* message)
{
    if (!value)
        throw std::runtime_error(std::string("Platform gallery: ") + message);
}
} // namespace

PlatformGallery::PlatformGallery(Window& window, Widget& page, std::filesystem::path assets)
    : m_Window(window), m_Assets(std::move(assets))
{
    page.SetGap(12);
    auto& title = page.Add<Label>("Native services / explicit capabilities, asynchronous results");
    title.SetWordWrap(true);
    title.SetStyleProperty(StyleProperty::FontSize, 20.0f);
    auto& toolbar = page.Add<ToolBar>();
    auto& copy = toolbar.AddButton("Copy formats");
    m_Copy = WidgetRef(&copy);
    auto& paste = toolbar.AddButton("Read clipboard");
    auto& open = toolbar.AddButton("Open file");
    m_Open = WidgetRef(&open);
    auto& save = toolbar.AddButton("Save file");
    auto& folder = toolbar.AddButton("Choose folder");
    auto& cancel = toolbar.AddButton("Cancel picker");
    auto& tools = page.Add<ToolBar>();
    auto& owned = tools.AddButton("Owned window");
    auto& inspect = tools.AddButton("Inspect platform & semantics");
    auto& text = page.Add<TextBox>("EStudioUI / Unicode Ω / clipboard formats");
    text.SetAccessibleName("Clipboard text");
    text.SetPlaceholder("Text to copy; richer clipboard formats are capability dependent");
    m_Text = WidgetRef(&text);
    auto& drop = page.Add<Label>("Drop files or folders here. Paths are copied before the native callback returns.");
    drop.SetAccessibleName("File drop area");
    drop.SetAcceptDrops(true);
    drop.SetCursor(CursorShape::Crosshair);
    drop.SetWordWrap(true);
    drop.SetHeight(SizeRule::Fixed(110));
    drop.SetStyleProperty(StyleProperty::Background, Color{0.08f, 0.28f, 0.24f, 1});
    drop.SetStyleProperty(StyleProperty::Padding, Insets{14, 14, 14, 14});
    m_Drop = WidgetRef(&drop);
    auto& status = page.Add<Label>("Ready. Native file dialogs return an asynchronous session.");
    status.SetWordWrap(true);
    m_Status = WidgetRef(&status);
    auto& readout = page.Add<Label>();
    readout.SetWordWrap(true);
    m_Readout = WidgetRef(&readout);
    auto& note = page.Add<Label>("Try a Windows input method in the editor. Temporary composition is drawn by the GPU; "
                                 "committing produces one validated edit. Platform capabilities and native acceptance "
                                 "results are reported separately.");
    note.SetWordWrap(true);
    m_Connections.emplace_back(copy.Clicked().Connect(
        [this]
        {
            ClipboardData data;
            data.m_Text = Live<TextBox>(m_Text).GetText();
            const auto capabilities = m_Window.Platform().GetCapabilities();
            if (capabilities.Get(PlatformFeature::ClipboardHtml) != PlatformSupport::Unsupported)
                data.m_Html = "<b>EStudioUI / native formats</b>";
            if (capabilities.Get(PlatformFeature::ClipboardCustom) != PlatformSupport::Unsupported)
                data.m_Custom["application/x-estudioui-example"] = {1, 2, 3};
            const auto result = m_Window.Platform().WriteClipboard(data);
            Status(m_Status, result ? "Copied supported clipboard formats." : result.m_Message);
        }));
    m_Connections.emplace_back(paste.Clicked().Connect(
        [this]
        {
            const auto read = m_Window.Platform().ReadClipboard();
            if (!read.m_Result)
            {
                Status(m_Status, read.m_Result.m_Message);
                return;
            }
            if (read.m_Data.m_Text && Live<TextBox>(m_Text).CanSetText(*read.m_Data.m_Text))
                Live<TextBox>(m_Text).SetText(*read.m_Data.m_Text);
            Status(m_Status, "Clipboard: text=" + std::to_string(read.m_Data.m_Text.has_value()) +
                                 ", HTML=" + std::to_string(read.m_Data.m_Html.has_value()) +
                                 ", files=" + std::to_string(read.m_Data.m_Files.size()) +
                                 ", custom=" + std::to_string(read.m_Data.m_Custom.size()));
        }));
    m_Connections.emplace_back(open.Clicked().Connect([this] { Pick(FileDialogKind::OpenFile); }));
    m_Connections.emplace_back(save.Clicked().Connect([this] { Pick(FileDialogKind::SaveFile); }));
    m_Connections.emplace_back(folder.Clicked().Connect([this] { Pick(FileDialogKind::SelectFolder); }));
    m_Connections.emplace_back(cancel.Clicked().Connect([this] { m_Picker.Cancel(); }));
    m_Connections.emplace_back(owned.Clicked().Connect([this] { OpenOwnedWindow(); }));
    m_Connections.emplace_back(inspect.Clicked().Connect(
        [this]
        {
            // Queries run after this input route has finished, preserving a coherent semantic snapshot.
            m_Window.Ui()
                .GetHandle(Live<Label>(m_Readout))
                .Post(
                    [this, alive = std::weak_ptr<int>(m_Lifetime)](Label&)
                    {
                        if (!alive.expired())
                            Inspect();
                    });
        }));
    m_Connections.emplace_back(drop.FilesDropped().Connect(
        [target = m_Drop](const FileDropEvent& event)
        {
            std::string message = "Dropped " + std::to_string(event.m_Files.size()) + " path(s):";
            for (const auto& path : event.m_Files)
            {
                const auto utf8 = path.u8string();
                message += "\n" + std::string(utf8.begin(), utf8.end());
            }
            Status(target, std::move(message));
        }));
    // Capability reads are safe during construction; semantic indexing waits for explicit inspection.
    const auto caps = m_Window.Platform().GetCapabilities();
    readout.SetText(caps.m_Adapter + " / file dialogs: " + Support(caps.Get(PlatformFeature::FileDialogs)) +
                    " / accessibility: " + Support(caps.Get(PlatformFeature::Accessibility)));
}

void PlatformGallery::Pick(FileDialogKind kind)
{
    if (m_Picker.IsPending())
    {
        Status(m_Status, "A picker is already pending. Cancel it or finish the native dialog.");
        return;
    }
    FileDialogOptions options;
    options.m_Kind = kind;
    options.m_Title = "EStudioUI / native picker";
    if (kind != FileDialogKind::SelectFolder)
        options.m_Filters = {{"Text files", "*.txt"}, {"All files", "*.*"}};
    if (kind == FileDialogKind::SaveFile)
    {
        options.m_FileName = "example.txt";
        options.m_DefaultExtension = "txt";
    }
    m_Picker = m_Window.Platform().ShowFileDialog(options);
    m_Picker.OnFinished(
        [target = m_Status](const FileDialogResult& result)
        {
            std::string message;
            switch (result.m_Status)
            {
            case FileDialogStatus::Accepted:
                message = "Accepted";
                break;
            case FileDialogStatus::Cancelled:
                message = "Cancelled";
                break;
            case FileDialogStatus::Unsupported:
                message = "Unsupported by this adapter";
                break;
            case FileDialogStatus::Busy:
                message = "Native picker is still closing";
                break;
            case FileDialogStatus::OwnerClosed:
                return;
            case FileDialogStatus::Failed:
                message = "Failed: " + result.m_Message;
                break;
            }
            for (const auto& path : result.m_Paths)
            {
                const auto utf8 = path.u8string();
                message += " / " + std::string(utf8.begin(), utf8.end());
            }
            Status(target, std::move(message));
        });
    if (m_Picker.IsPending())
        Status(m_Status, "Picker pending / the framework continues processing other windows.");
}

void PlatformGallery::Inspect()
{
    auto& ui = m_Window.Ui();
    const auto caps = ui.Platform().GetCapabilities();
    const auto metrics = ui.Platform().GetWindowMetrics();
    auto& tree = ui.Semantics();
    const auto node = tree.GetNode(tree.GetId(Live<TextBox>(m_Text)));
    std::ostringstream info;
    info << caps.m_Adapter << "\nClipboard HTML: " << Support(caps.Get(PlatformFeature::ClipboardHtml))
         << " / dialogs: " << Support(caps.Get(PlatformFeature::FileDialogs))
         << " / accessibility: " << Support(caps.Get(PlatformFeature::Accessibility))
         << " / composition: " << Support(caps.Get(PlatformFeature::TextComposition))
         << "\nLogical client: " << metrics.m_LogicalSize.m_X << " x " << metrics.m_LogicalSize.m_Y
         << " / framebuffer: " << metrics.m_FramebufferSize.m_X << " x " << metrics.m_FramebufferSize.m_Y
         << " / pixel scale: " << metrics.m_PixelScale.m_X << ", " << metrics.m_PixelScale.m_Y
         << "\nSemantic editor: " << node->m_Info.m_Name << " / stable widget ID " << node->m_Id.m_Widget
         << " / value: " << node->m_Info.m_Value;
    Status(m_Readout, info.str());
}

void PlatformGallery::OpenOwnedWindow()
{
    std::erase_if(m_Windows, [](const auto& window) { return window->IsClosed(); });
    WindowOptions options;
    options.m_Width = 560;
    options.m_Height = 230;
    options.m_Title = "EStudioUI / owned native window";
    options.m_Headless = m_Window.IsHeadless();
    options.m_Owner = &m_Window;
    auto window = std::make_unique<Window>(options, m_Assets / "NotoSans.ttf");
    window->Ui().Root().SetGap(16);
    window->Ui().Root().Add<Label>("Closing the parent also closes this owned window.");
    window->Ui().Root().Add<Button>("Close").Clicked().Connect([target = window->GetHandle()]
                                                               { target.Post([](Window& live) { live.Close(); }); });
    m_Windows.push_back(std::move(window));
}

void PlatformGallery::Verify(const std::filesystem::path& output)
{
    WindowOptions options;
    options.m_Width = 1100;
    options.m_Height = 720;
    options.m_Headless = m_Window.IsHeadless();
    options.m_PlatformFactory = [](PlatformHost host)
    { return std::make_unique<MemoryPlatformServices>(std::move(host)); };
    Window probe(options, m_Assets / "NotoSans.ttf");
    PlatformGallery gallery(probe, probe.Ui().Root(), m_Assets);
    auto& ui = probe.Ui();
    probe.RenderFrame();
    const auto click = [&](const WidgetRef& ref)
    {
        auto& widget = Live<Widget>(ref);
        const auto bounds = widget.GetBounds();
        const Point center{bounds.m_X + bounds.m_Width / 2, bounds.m_Y + bounds.m_Height / 2};
        ui.PointerDown(center);
        ui.PointerUp(center);
    };
    click(gallery.m_Copy);
    Check(probe.Platform().ReadClipboard().m_Data.m_Html.has_value(),
          "gallery publishes supported formats through injected services");
    auto& memory = dynamic_cast<MemoryPlatformServices&>(probe.Platform());
    memory.QueueFileDialogResult({FileDialogStatus::Accepted, {std::filesystem::path("example.txt")}});
    click(gallery.m_Open);
    probe.RenderFrame();
    Check(Live<Label>(gallery.m_Status).GetText().starts_with("Accepted"),
          "asynchronous picker updates the live GPU caption");
    auto* native = reinterpret_cast<GLFWwindow*>(probe.GetContextIdentity());
    auto drop = glfwSetDropCallback(native, nullptr);
    glfwSetDropCallback(native, drop);
    Check(drop != nullptr, "production native file-drop callback exists");
    if (probe.IsHeadless())
        glfwShowWindow(native);
    else
        glfwSetWindowPos(native, 32, 32);
    glfwFocusWindow(native);
    glfwPollEvents();
    probe.RenderFrame();
    const auto bounds = Live<Label>(gallery.m_Drop).GetBounds();
    const auto visible = bounds.Intersect(Live<Label>(gallery.m_Drop).GetClip());
    Check(visible.m_Width > 2 && visible.m_Height > 2, "native file-drop fixture is inside the client viewport");
    const auto metrics = probe.Platform().GetWindowMetrics();
    const Point point{visible.m_X + visible.m_Width * 0.5f, visible.m_Y + visible.m_Height * 0.5f};
    glfwSetCursorPos(native, static_cast<double>(point.m_X) * metrics.m_InputScale.m_X,
                     static_cast<double>(point.m_Y) * metrics.m_InputScale.m_Y);
    double nativeX = 0, nativeY = 0;
    glfwGetCursorPos(native, &nativeX, &nativeY);
    const auto actual = metrics.FromNative({static_cast<float>(nativeX), static_cast<float>(nativeY)});
    if (!visible.Contains(actual))
    {
        std::ostringstream diagnostic;
        diagnostic << "Platform gallery: native cursor did not reach the drop fixture; focus="
                   << glfwGetWindowAttrib(native, GLFW_FOCUSED) << ", requested=" << point.m_X << ',' << point.m_Y
                   << ", actual=" << actual.m_X << ',' << actual.m_Y << ", input scale=" << metrics.m_InputScale.m_X
                   << ',' << metrics.m_InputScale.m_Y;
        throw std::runtime_error(diagnostic.str());
    }
    ui.States().Batch(
        [&]
        {
            std::string source = "owned-drop-Ω.txt";
            const char* paths[] = {source.c_str()};
            drop(native, 1, paths);
            source.assign("overwritten native callback buffer");
            Check(Live<Label>(gallery.m_Drop).GetText().find("owned-drop-Ω.txt") == std::string::npos,
                  "native drop delivery is deferred until the active state batch has completed");
        });
    probe.RenderFrame();
    Check(Live<Label>(gallery.m_Drop).GetText().find("owned-drop-Ω.txt") != std::string::npos,
          "production drop callback owns UTF-8 paths before source storage expires");
    gallery.Inspect();
    probe.RenderFrame();
    const auto before = ui.GetStats();
    const auto id = ui.Semantics().GetId(Live<TextBox>(gallery.m_Text));
    for (int i = 0; i < 50; ++i)
        ui.Semantics().GetNode(id);
    probe.RenderFrame();
    Check(before.m_StyleResolutions == ui.GetStats().m_StyleResolutions &&
              before.m_LayoutPasses == ui.GetStats().m_LayoutPasses,
          "semantic inspection preserves style/layout caches");
    const auto pixel = probe.Graphics().ReadPixel({bounds.m_X + bounds.m_Width - 8, bounds.m_Y + bounds.m_Height - 8});
    Check(pixel.m_G > pixel.m_R + 0.1f, "file-drop area appears in the actual framebuffer");
    probe.Graphics().SavePpm(output / "platform-services.ppm");

    auto& editor = Live<TextBox>(gallery.m_Text);
    editor.SetText("");
    ui.SetFocus(&editor);
    gallery.Inspect();
    probe.RenderFrame();
    const auto editorBounds = editor.GetBounds();
    const auto samples = [&]
    {
        std::vector<Color> values;
        const float width = std::min(240.0f, editorBounds.m_Width);
        for (float y = 2; y < editorBounds.m_Height - 2; y += 3)
            for (float x = 2; x < width - 2; x += 3)
                values.push_back(probe.Graphics().ReadPixel({editorBounds.m_X + x, editorBounds.m_Y + y}));
        return values;
    };
    const auto originalPixels = samples();
    const auto input = ui.GetTextInputState();
    Check(input.m_Active && input.m_Client != 0, "focused editor supplies a native text-input session");
    Check(ui.DispatchComposition({TextCompositionKind::Preedit, "GPU preedit Ω", 3, input.m_Client, input.m_Epoch}),
          "composition updates use the shared editor protocol");
    probe.RenderFrame();
    Check(editor.GetText().empty() && ui.GetTextInputState().m_Composing,
          "temporary composition never becomes canonical text");
    Check(samples() != originalPixels, "preedit changes actual GPU pixels");
    probe.Graphics().SavePpm(output / "platform-composition.ppm");
    Check(ui.DispatchComposition({TextCompositionKind::Cancel, {}, 0, input.m_Client, input.m_Epoch}),
          "composition cancellation is accepted by the current session");
    probe.RenderFrame();
    Check(samples() == originalPixels && editor.GetText().empty(),
          "cancellation restores actual editor pixels and canonical text");
    Check(ui.DispatchComposition({TextCompositionKind::Commit, "Committed Ω", 0, input.m_Client, input.m_Epoch}),
          "native commit follows the canonical validated edit path");
    Check(editor.GetText() == "Committed Ω", "native committed text is inserted once");
    ui.KeyEvent(Key::Z, true, false, Modifiers{.m_Control = true});
    Check(editor.GetText().empty(), "one undo removes the complete composition commit");
    VerifyVirtualItems();

    WindowOptions childOptions = options;
    childOptions.m_Owner = &probe;
    Window child(childOptions, m_Assets / "NotoSans.ttf");
    probe.Close();
    Check(child.IsClosed(), "parent close closes an owned secondary window without requiring owner deletion");
    const auto nativeState = std::make_shared<NativeWorkState>();
    EndNativeWork unwind{nativeState};
    options.m_PlatformFactory = [nativeState](PlatformHost host)
    { return std::make_unique<RetiringServices>(std::move(host), nativeState); };
    auto retiring = std::make_unique<Window>(options, m_Assets / "NotoSans.ttf");
    retiring.reset();
    Check(!nativeState->m_Destroyed, "native resources outlive a deleted C++ window while native work is pending");
    nativeState->m_Active = false;
    probe.RenderFrame();
    Check(nativeState->m_Destroyed, "a safe owner turn retires native resources after worker unwind");

    std::ofstream report(output / "platform-report.txt");
    report
        << "F11 platform/semantic/composition GPU integration: PASS\n"
        << "Injected clipboard/picker; owning production GLFW file drop; stable semantic queries; owned windows.\n"
        << "50 semantic queries: zero style/layout recalculations. Real framebuffer capture: platform-services.ppm.\n"
        << "Preedit changes actual GPU pixels without a canonical edit; cancellation restores pixels; commit undoes "
           "once.\n"
        << "100,000 list rows and table cells reveal/select by virtual identity and agree with actual GPU geometry.\n"
        << "This deterministic probe does not claim native clipboard, picker or assistive-technology readiness.\n";
}
} // namespace EStudioUI
