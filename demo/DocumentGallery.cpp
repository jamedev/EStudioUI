#include "DocumentGallery.h"
#include "GpuProbeWindow.h"

#include <EStudioUI/BackgroundTasks.h>
#include <EStudioUI/DocumentWorkspace.h>
#include <EStudioUI/FileDialog.h>
#include <EStudioUI/MainWindow.h>
#include <EStudioUI/MessageBox.h>
#include <EStudioUI/NumericInput.h>
#include <EStudioUI/ProgressBar.h>
#include <EStudioUI/TextBox.h>
#include <EStudioUI/UiContext.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <locale>
#include <map>
#include <sstream>
#include <thread>

namespace EStudioUI
{
namespace
{
Point Center(const Widget& widget)
{
    const auto bounds = widget.GetBounds();
    return {bounds.m_X + bounds.m_Width / 2, bounds.m_Y + bounds.m_Height / 2};
}

void Click(UiContext& ui, Widget& widget)
{
    const auto point = ui.ToWindowPoint(widget, Center(widget));
    ui.PointerDown(point);
    ui.PointerUp(point);
}

std::string Read(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void WriteCase(const std::filesystem::path& path, const std::string& bytes)
{
    auto stage = path;
    stage += ".estudioui-" + std::to_string(UiDispatcher::AllocateTargetKey()) + ".new";
    try
    {
        std::ofstream output(stage, std::ios::binary | std::ios::trunc);
        output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        output.flush();
        if (!output)
        {
            throw std::runtime_error("Cannot write the engineering case file.");
        }
        output.close();
#ifdef _WIN32
        if (!MoveFileExW(stage.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        {
            throw std::runtime_error("Cannot install the engineering case file.");
        }
#else
        std::filesystem::rename(stage, path);
#endif
    }
    catch (...)
    {
        std::error_code ignored;
        std::filesystem::remove(stage, ignored);
        throw;
    }
}
} // namespace

struct DocumentGallery::Data : std::enable_shared_from_this<Data>
{
    struct Model
    {
        std::string m_Id;
        std::unique_ptr<State<double>> m_Load, m_Factor;
        std::shared_ptr<UndoHistory> m_History;
        WidgetRef m_View;
        std::filesystem::path m_Path;
        std::vector<ScopedConnection> m_Connections;
    };

    Window* m_Owner;
    WidgetRef m_Workspace, m_Status;
    std::map<std::string, std::shared_ptr<Model>> m_Models;
    std::unique_ptr<BackgroundTasks> m_Tasks;
    std::vector<Task> m_Jobs;
    bool m_Alive = true;
    std::uint64_t m_Serial = 0;

    explicit Data(Window& owner) : m_Owner(&owner)
    {
    }

    DocumentWorkspace* Workspace() const
    {
        return m_Alive ? dynamic_cast<DocumentWorkspace*>(m_Workspace.Get()) : nullptr;
    }

    void Status(std::string text)
    {
        if (auto* label = dynamic_cast<Label*>(m_Status.Get()))
        {
            label->SetText(std::move(text));
        }
    }

    void Write(const std::shared_ptr<Model>& model, DocumentSaveRequest request, std::filesystem::path path,
               std::string bytes)
    {
        auto* view = model->m_View.Get();
        if (!view || !Workspace() || request.GetStopToken().stop_requested())
        {
            return;
        }
        if (!m_Tasks)
        {
            m_Tasks = std::make_unique<BackgroundTasks>(BackgroundTaskOptions{2, 32});
        }
        std::erase_if(m_Jobs, [](const Task& task) { return task.IsReady(); });
        const auto weak = weak_from_this();
        UiTaskOptions<Widget> options;
        options.m_Error = [weak](Widget&, std::exception_ptr error)
        {
            try
            {
                std::rethrow_exception(error);
            }
            catch (const std::exception& failure)
            {
                if (const auto data = weak.lock(); data && data->Workspace())
                {
                    data->Status(failure.what());
                }
            }
        };
        auto job = m_Tasks->Run(
            view->GetContext()->GetHandle(*view),
            [request, path, bytes = std::move(bytes)]
            {
                try
                {
                    if (request.GetStopToken().stop_requested())
                    {
                        return path;
                    }
                    WriteCase(path, bytes);
                    request.Complete({DocumentSaveStatus::Saved});
                    return path;
                }
                catch (...)
                {
                    request.Complete(
                        {DocumentSaveStatus::Failed, "Writing the case file failed; the document remains open."});
                    throw;
                }
            },
            [weak, model](Widget&, const std::filesystem::path& saved)
            {
                if (const auto data = weak.lock(); data && data->Workspace())
                {
                    model->m_Path = saved;
                    data->Workspace()->SetDocumentTitle(model->m_Id, saved.filename().string());
                    data->Status("Engineering case saved; changes made during I/O stay modified.");
                }
            },
            std::move(options));
        if (job.IsReady() && request.IsPending())
        {
            request.Complete({DocumentSaveStatus::Failed,
                              "The background save could not start; retry when the worker queue is available."});
        }
        m_Jobs.push_back(std::move(job));
    }

    void Save(const std::shared_ptr<Model>& model, DocumentSaveRequest request)
    {
        const auto load = model->m_Load->GetSnapshot(), factor = model->m_Factor->GetSnapshot();
        std::ostringstream output;
        output.imbue(std::locale::classic());
        output << std::setprecision(17) << "{\n  \"load_kN\": " << *load << ",\n  \"factor\": " << *factor << "\n}\n";
        auto bytes = output.str();
        if (!model->m_Path.empty())
        {
            Write(model, request, model->m_Path, std::move(bytes));
            return;
        }
        FileDialogOptions options;
        options.m_Title = "Save engineering case";
        options.m_DefaultExtension = "json";
        options.m_FileName = model->m_Id + ".json";
        options.m_Filters = {{"Engineering case JSON", "*.json"}};
        const auto weak = weak_from_this();
        FileDialog::Save(*model->m_View.Get(), options)
            .OnFinished(
                [weak, model, request, bytes = std::move(bytes)](FileDialogResult result)
                {
                    const auto data = weak.lock();
                    if (!data || !data->Workspace() || request.GetStopToken().stop_requested())
                    {
                        return;
                    }
                    if (result.m_Status == FileDialogStatus::Accepted && !result.m_Paths.empty())
                    {
                        data->Write(model, request, result.m_Paths.front(), bytes);
                    }
                    else
                    {
                        request.Complete({result.m_Status == FileDialogStatus::Cancelled ? DocumentSaveStatus::Cancelled
                                                                                         : DocumentSaveStatus::Failed,
                                          "The save picker was cancelled or is unavailable."});
                    }
                });
    }

    void NewDocument()
    {
        auto* workspace = Workspace();
        if (!workspace)
        {
            return;
        }
        if (m_Models.size() >= 128)
        {
            Status("Close a document before opening another case; this workspace supports 128 views.");
            return;
        }
        auto model = std::make_shared<Model>();
        model->m_Id = "load-case-" + std::to_string(++m_Serial);
        model->m_Load =
            std::make_unique<State<double>>(m_Owner->Ui().States(), 20.0 * static_cast<double>((m_Serial - 1) % 10 + 1),
                                            [](double value)
                                            {
                                                if (!std::isfinite(value) || value < 0 || value > 1000)
                                                {
                                                    throw std::invalid_argument("Load range is 0..1000 kN.");
                                                }
                                            });
        model->m_Factor = std::make_unique<State<double>>(m_Owner->Ui().States(), 1.5,
                                                          [](double value)
                                                          {
                                                              if (!std::isfinite(value) || value < 1 || value > 10)
                                                              {
                                                                  throw std::invalid_argument("Factor range is 1..10.");
                                                              }
                                                          });
        model->m_History = std::make_shared<UndoHistory>(m_Owner->Ui().States());
        m_Models.emplace(model->m_Id, model);
        const auto weak = weak_from_this();
        auto& view = workspace->AddDocument({model->m_Id, "Load case " + std::to_string(m_Serial), model->m_History,
                                             [weak, model](DocumentSaveRequest request)
                                             {
                                                 if (const auto data = weak.lock(); data && data->Workspace())
                                                 {
                                                     data->Save(model, request);
                                                 }
                                             }});
        model->m_View = WidgetRef(&view);
        view.SetGap(12);
        view.SetStyleProperty(StyleProperty::Padding, Insets{18, 18, 18, 18});
        view.Add<Label>("Engineering case / persistent document view").SetStyleProperty(StyleProperty::FontSize, 21.0f);
        view.Add<Label>(
                "Commit a numeric edit, try undo/redo, drag the tab to reorder or detach, and close the document.")
            .SetWordWrap(true);
        view.Add<Label>("Applied load / kN");
        auto& load = view.Add<NumericInput>();
        load.SetRange(0, 1000);
        load.SetStep(.5);
        load.SetDecimals(2);
        load.SetValue(model->m_Load->Get());
        view.Add<Label>("Safety factor");
        auto& factor = view.Add<NumericInput>();
        factor.SetRange(1, 10);
        factor.SetStep(.1);
        factor.SetDecimals(2);
        factor.SetValue(model->m_Factor->Get());
        auto& total = view.Add<Label>();
        total.SetStyleProperty(StyleProperty::FontSize, 19.0f);
        const WidgetRef loadRef(&load), factorRef(&factor), totalRef(&total);
        const auto refresh = [weakModel = std::weak_ptr<Model>(model), loadRef, factorRef, totalRef]
        {
            if (const auto item = weakModel.lock())
            {
                if (auto* field = dynamic_cast<NumericInput*>(loadRef.Get()))
                {
                    field->SetValue(item->m_Load->Get());
                }
                if (auto* field = dynamic_cast<NumericInput*>(factorRef.Get()))
                {
                    field->SetValue(item->m_Factor->Get());
                }
                if (auto* label = dynamic_cast<Label*>(totalRef.Get()))
                {
                    label->SetText("Factored load: " + std::to_string(item->m_Load->Get() * item->m_Factor->Get()) +
                                   " kN");
                }
            }
        };
        model->m_Connections.emplace_back(
            model->m_Load->Subscribe([refresh](const StateChange<double>&) { refresh(); }));
        model->m_Connections.emplace_back(
            model->m_Factor->Subscribe([refresh](const StateChange<double>&) { refresh(); }));
        const auto item = std::weak_ptr<Model>(model);
        load.EditingFinished().Connect(
            [item](double value)
            {
                if (const auto live = item.lock())
                {
                    live->m_History->Set(*live->m_Load, value, "Edit load");
                }
            });
        factor.EditingFinished().Connect(
            [item](double value)
            {
                if (const auto live = item.lock())
                {
                    live->m_History->Set(*live->m_Factor, value, "Edit safety factor");
                }
            });
        auto& tools = view.Add<ToolBar>();
        tools.AddButton("Increase load +10 kN")
            .Clicked()
            .Connect(
                [item]
                {
                    if (const auto live = item.lock())
                    {
                        live->m_History->Set(*live->m_Load, std::min(1000.0, live->m_Load->Get() + 10),
                                             "Increase load");
                    }
                });
        tools.AddButton("Set nominal case / one undo group")
            .Clicked()
            .Connect(
                [item]
                {
                    if (const auto live = item.lock())
                    {
                        live->m_History->Group("Set nominal case",
                                               [&]
                                               {
                                                   live->m_History->Set(*live->m_Load, 120.0, "Load");
                                                   live->m_History->Set(*live->m_Factor, 1.5, "Factor");
                                               });
                    }
                });
        refresh();
    }
};

DocumentGallery::DocumentGallery(Window& owner, Widget& page) : m_Data(std::make_shared<Data>(owner))
{
    auto& tools = page.Add<ToolBar>();
    auto& workspace = page.Add<DocumentWorkspace>(owner);
    m_Data->m_Workspace = WidgetRef(&workspace);
    const auto weak = std::weak_ptr<Data>(m_Data);
    tools.AddButton("New engineering case")
        .Clicked()
        .Connect(
            [weak]
            {
                if (const auto data = weak.lock(); data && data->Workspace())
                {
                    data->NewDocument();
                }
            });
    for (const auto& action :
         {workspace.UndoAction(), workspace.RedoAction(), workspace.SaveAction(), workspace.CloseAction()})
    {
        tools.AddAction(action);
    }
    tools.AddButton("Cancel close")
        .Clicked()
        .Connect(
            [weak]
            {
                if (const auto data = weak.lock(); data && data->Workspace())
                {
                    data->Workspace()->CancelClose();
                }
            });
    tools.AddButton("Close all")
        .Clicked()
        .Connect(
            [weak]
            {
                if (const auto data = weak.lock(); data && data->Workspace())
                {
                    data->Workspace()->RequestCloseAll();
                }
            });
    tools.AddButton("Detach active")
        .Clicked()
        .Connect(
            [weak]
            {
                if (const auto data = weak.lock(); data && data->Workspace())
                {
                    const auto id = data->Workspace()->GetCurrentDocument();
                    if (!id.empty())
                    {
                        data->m_Owner->Ui().Defer(
                            [weak, id](UiContext&)
                            {
                                if (const auto live = weak.lock(); live && live->Workspace())
                                {
                                    live->Workspace()->DetachDocument(id);
                                }
                            });
                    }
                }
            });
    auto& status = page.Add<Label>(
        "Save uses an asynchronous platform picker and worker I/O. No document-editor engine is required.");
    status.SetWordWrap(true);
    m_Data->m_Status = WidgetRef(&status);
    workspace.Changed().Connect(
        [weak]
        {
            if (const auto data = weak.lock(); data && data->Workspace())
            {
                const auto ids = data->Workspace()->GetDocumentIds();
                const auto current = data->Workspace()->GetCurrentDocument();
                if (!current.empty())
                {
                    const auto info = data->Workspace()->GetDocumentInfo(current);
                    if (!info.m_SaveError.empty())
                    {
                        data->Status(info.m_SaveError);
                    }
                }
                std::erase_if(data->m_Models, [&](const auto& item)
                              { return std::find(ids.begin(), ids.end(), item.first) == ids.end(); });
            }
        });
    m_Data->NewDocument();
    m_Data->NewDocument();
}

DocumentGallery::~DocumentGallery()
{
    m_Data->m_Alive = false;
    for (const auto& job : m_Data->m_Jobs)
    {
        job.Cancel();
    }
}

void DocumentGallery::Verify(const std::filesystem::path& assets, const std::filesystem::path& output)
{
    (void)assets;
    int checks = 0;
    const auto check = [&](bool value, const char* message)
    {
        ++checks;
        if (!value)
        {
            throw std::runtime_error(std::string("Document workspace GPU: ") + message);
        }
    };
    WindowOptions options;
    options.m_Title = "Document workspace verification";
    options.m_Width = 1180;
    options.m_Height = 800;
    auto probe = m_Data->m_Owner->CreateOwnedWindow(options);
    AllowOversizedGpuProbe(*probe);
    Theme theme;
    theme.m_Mode = ThemeMode::Light;
    probe->Themes().SetTheme(theme);
    auto& ui = probe->Ui();
    auto& shell = ui.Root().Add<MainWindow>(*probe);
    auto& workspace = shell.Central().Add<DocumentWorkspace>(*probe);
    State<int> model(ui.States(), 0);
    auto history = std::make_shared<UndoHistory>(ui.States());
    DocumentSaveRequest ticket;
    auto& content = workspace.AddDocument(
        {"part-a", "Beam analysis", history, [&](DocumentSaveRequest request) { ticket = request; }});
    content.SetStyleProperty(StyleProperty::Padding, Insets{20, 20, 20, 20});
    content.Add<Label>("Persistent document A / GPU-rendered controls");
    auto& editor = content.Add<TextBox>("Notes survive native detach and reattach");
    editor.SetWidth(SizeRule::Fill());
    auto& block = content.Add<Widget>();
    block.SetWidth(SizeRule::Fill());
    block.SetHeight(SizeRule::Fixed(110));
    block.SetStyleProperty(StyleProperty::Background, Color{.15f, .6f, .2f, 1});
    workspace.AddDocument({"part-b", "Load combinations", {}, {}}).Add<Label>("A second persistent document");
    workspace.SetCurrentDocument("part-a");
    const auto frame = [&] { probe->RenderFrame(ui.GetTime() + .1); };
    frame();
    probe->Graphics().SavePpm(output / "documents-workspace.ppm");
    history->Set(model, 1, "Change case");
    frame();
    check(workspace.Tabs().IsTabModified(0), "A canonical edit reaches the actual tab's unsaved marker");
    auto& first = workspace.Tabs().GetTabButton(0);
    auto* close = dynamic_cast<ToolButton*>(first.GetChildren().front().get());
    check(close != nullptr, "Closable tabs expose a real accessible button");
    Click(ui, *close);
    frame();
    auto* prompt = dynamic_cast<Dialog*>(ui.GetPopup());
    check(prompt && !probe->IsClosed(), "A real tab-close click opens asynchronous save/discard/cancel");
    probe->Graphics().SavePpm(output / "documents-save-discard-cancel.ppm");
    Click(ui, *prompt->Buttons().GetButton(StandardButton::Cancel));
    frame();
    check(&workspace.Content("part-a") == &content, "Cancelling a real close keeps the actual view");
    const auto from = Center(workspace.Tabs().GetTabButton(0));
    const auto to = Center(workspace.Tabs().GetTabButton(1));
    ui.PointerDown(from);
    ui.PointerMove(to);
    frame();
    ui.PointerUp(to);
    frame();
    check(workspace.GetDocumentIds().front() == "part-b" && workspace.GetCurrentDocument() == "part-a",
          "Captured pointer reordering follows persistent selected identities");
    auto closing = workspace.RequestClose("part-a");
    frame();
    prompt = dynamic_cast<Dialog*>(ui.GetPopup());
    Click(ui, *prompt->Buttons().GetButton(StandardButton::Save));
    frame();
    history->Set(model, 2, "Edit during save");
    std::thread worker([&] { ticket.Complete({DocumentSaveStatus::Saved}); });
    worker.join();
    frame();
    check(closing.IsPending() && workspace.GetDocumentInfo("part-a").m_Modified && ui.GetPopup(),
          "An old save re-prompts before a newer edit could be discarded");
    probe->Graphics().SavePpm(output / "documents-stale-save.ppm");
    prompt = dynamic_cast<Dialog*>(ui.GetPopup());
    Click(ui, *prompt->Buttons().GetButton(StandardButton::Cancel));
    frame();
    const int selected = workspace.Tabs().GetCurrentIndex();
    const auto drag = Center(workspace.Tabs().GetTabButton(selected));
    const Point outside{drag.m_X + 20, drag.m_Y + 120};
    ui.PointerDown(drag);
    ui.PointerMove(outside);
    ui.PointerUp(outside);
    frame();
    auto* floating = workspace.GetFloatingWindow("part-a");
    check(floating && &workspace.Content("part-a") == &content &&
              editor.GetText() == "Notes survive native detach and reattach",
          "Drag-out creates a native host without recreating the document/editor");
    check(floating->GetContextIdentity() != probe->GetContextIdentity(),
          "Detached documents own a separate shared GL context");
    AllowOversizedGpuProbe(*floating);
    floating->RenderFrame(floating->Ui().GetTime() + .1);
    floating->Graphics().SavePpm(output / "documents-detached.ppm");
    const auto area = block.GetBounds();
    const auto pixel = floating->Graphics().ReadPixel({area.m_X + 12, area.m_Y + 12});
    check(pixel.m_G > .5f && pixel.m_R < .25f, "The transferred real view reaches the floating GPU framebuffer");
    floating->Close();
    frame();
    floating->RenderFrame(floating->Ui().GetTime() + .1);
    auto* floatingPrompt = dynamic_cast<Dialog*>(floating->Ui().GetPopup());
    check(floatingPrompt && !floating->IsClosed(),
          "Native floating-host close is vetoed while dirty data awaits approval");
    Click(floating->Ui(), *floatingPrompt->Buttons().GetButton(StandardButton::Cancel));
    frame();
    workspace.AttachDocument("part-a", 0);
    frame();
    check(!workspace.GetFloatingWindow("part-a") && &workspace.Content("part-a") == &content && model.Get() == 2,
          "Reattach preserves actual widgets, model values and history");
    probe->Graphics().SavePpm(output / "documents-reattached.ppm");
    probe->Graphics().SavePpm(output / "documents-retained.ppm");
    probe->Graphics().SetPartialRedrawEnabled(false);
    probe->RenderFrame(ui.GetTime());
    probe->Graphics().SavePpm(output / "documents-full.ppm");
    check(Read(output / "documents-retained.ppm") == Read(output / "documents-full.ppm"),
          "Document workspace retained pixels equal the full-redraw reference");
    auto* native = reinterpret_cast<GLFWwindow*>(probe->GetContextIdentity());
    auto callback = glfwSetWindowCloseCallback(native, nullptr);
    glfwSetWindowCloseCallback(native, callback);
    glfwSetWindowShouldClose(native, GLFW_TRUE);
    callback(native);
    check(!probe->IsClosed(),
          "The production native close callback clears GLFW's provisional close flag before approval");
    frame();
    prompt = dynamic_cast<Dialog*>(ui.GetPopup());
    check(prompt != nullptr, "Window shutdown uses the same document-close workflow");
    Click(ui, *prompt->Buttons().GetButton(StandardButton::Discard));
    frame();
    check(probe->IsClosed() && workspace.GetDocumentIds().empty(),
          "Approved document closure permits real window shutdown");
    check(!probe->GetCloseRequestError(), "Native close approval contains no callback error");
    std::ofstream report(output / "documents-report.txt");
    report << "Document workspace: " << checks << " real-renderer/native checks passed.\n"
           << "Persistent tab reordering, dirty close decisions, old-save protection, native detach/reattach and "
              "retained/full pixels.\n"
           << "Software OpenGL validates correctness; it does not establish hardware GPU throughput.\n";
    std::cout << "Document workspace GPU: " << checks << " checks passed\n";
}
} // namespace EStudioUI
