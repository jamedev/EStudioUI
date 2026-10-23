#include "RenderingProfile.h"

#include <EStudioUI/Label.h>
#include <EStudioUI/Slider.h>
#include <EStudioUI/TextBox.h>
#include <glad/gl.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cctype>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <vector>

namespace EStudioUI
{
namespace
{
using Clock = std::chrono::steady_clock;

double Milliseconds(Clock::time_point start)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

std::string Read(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
        throw std::runtime_error("Cannot read profiling stylesheet " + path.string());
    return {std::istreambuf_iterator<char>(input), {}};
}

std::string Quoted(std::string_view text)
{
    std::string result = "\"";
    for (const char character : text)
    {
        if (character == '"')
            result += '"';
        result += character;
    }
    return result + '"';
}

std::string JsonString(std::string_view text)
{
    std::string result = "\"";
    for (const unsigned char character : text)
    {
        if (character == '"' || character == '\\')
            result += '\\';
        if (character == '\n')
            result += "\\n";
        else if (character == '\r')
            result += "\\r";
        else if (character == '\t')
            result += "\\t";
        else if (character < 32)
            result += '?';
        else
            result += static_cast<char>(character);
    }
    return result + '"';
}

const char* Reason(RedrawReason reason)
{
    switch (reason)
    {
    case RedrawReason::None:
        return "none";
    case RedrawReason::Partial:
        return "partial";
    case RedrawReason::FullRequested:
        return "full-requested";
    case RedrawReason::HistoryInvalid:
        return "history-invalid";
    case RedrawReason::ClearChanged:
        return "clear-changed";
    case RedrawReason::ViewportChanged:
        return "viewport-changed";
    case RedrawReason::DamageArea:
        return "damage-area";
    case RedrawReason::DamageRectangles:
        return "damage-rectangles";
    }
    return "unknown";
}

template <typename T> T* Find(Widget& widget, const std::function<bool(const T&)>& predicate)
{
    if (auto* candidate = dynamic_cast<T*>(&widget); candidate && predicate(*candidate))
        return candidate;
    for (const auto& child : widget.GetChildren())
        if (auto* candidate = Find<T>(*child, predicate))
            return candidate;
    return nullptr;
}

int FindPage(TabWidget& tabs, const std::function<bool(Widget&)>& predicate)
{
    for (int index = 0; index < tabs.GetTabCount(); ++index)
        if (predicate(tabs.GetPage(index)))
            return index;
    throw std::runtime_error("A required gallery page is missing from the rendering profile.");
}

double Percentile(std::vector<double> values, double fraction)
{
    if (values.empty())
        return -1;
    std::sort(values.begin(), values.end());
    const auto index = static_cast<std::size_t>(std::ceil(fraction * static_cast<double>(values.size())));
    return values[std::min(values.size() - 1, index ? index - 1 : 0)];
}

struct Frame
{
    std::string m_Case, m_Mode, m_Phase;
    int m_Index = 0;
    RenderStats m_Render;
    UiStats m_Ui;
    std::size_t m_Shapes = 0;
    double m_InputMilliseconds = 0, m_CpuMilliseconds = 0, m_SwapMilliseconds = 0, m_IntervalMilliseconds = 0;
    double m_GpuMilliseconds = -1, m_GpuUploadMilliseconds = -1, m_GpuDrawMilliseconds = -1;
    double m_GpuPresentMilliseconds = -1;
};

struct Workload
{
    std::string m_Name, m_Group, m_Description;
    int m_Page = 0;
    Slider* m_Slider = nullptr;
    TextBox* m_Editor = nullptr;
    bool m_Selection = false;
    bool m_Flat = false;
};

struct Summary
{
    std::string m_Case, m_Mode;
    std::size_t m_Frames = 0, m_Timed = 0, m_Full = 0, m_AreaFallback = 0, m_RectangleFallback = 0;
    double m_CpuMedian = -1, m_CpuP95 = -1, m_InputP95 = -1, m_SwapP95 = -1;
    double m_GpuMedian = -1, m_GpuP95 = -1, m_GpuUploadMedian = -1, m_GpuDrawMedian = -1, m_GpuPresentMedian = -1;
    double m_RepaintFraction = 0, m_MeanGlyphs = 0, m_MeanDrawCalls = 0, m_ActualRate = 0;
};

Summary Summarize(const std::vector<Frame>& frames, std::string name, std::string mode)
{
    Summary summary;
    summary.m_Case = std::move(name);
    summary.m_Mode = std::move(mode);
    std::vector<double> cpu, input, swap, gpu, upload, draw, present;
    double intervals = 0;
    for (const auto& frame : frames)
    {
        if (frame.m_Phase != "measured" || frame.m_Case != summary.m_Case || frame.m_Mode != summary.m_Mode)
            continue;
        ++summary.m_Frames;
        cpu.push_back(frame.m_CpuMilliseconds);
        input.push_back(frame.m_InputMilliseconds);
        swap.push_back(frame.m_SwapMilliseconds);
        intervals += frame.m_IntervalMilliseconds;
        const auto& stats = frame.m_Render;
        summary.m_RepaintFraction += stats.m_FramebufferPixels
                                        ? static_cast<double>(stats.m_RepaintPixels) /
                                              static_cast<double>(stats.m_FramebufferPixels)
                                        : 0;
        summary.m_MeanGlyphs += static_cast<double>(stats.m_ReplayedGlyphs);
        summary.m_MeanDrawCalls += static_cast<double>(stats.m_DrawCalls);
        summary.m_Full += stats.m_FramebufferPixels && stats.m_RepaintPixels == stats.m_FramebufferPixels ? 1u : 0u;
        summary.m_AreaFallback += stats.m_RedrawReason == RedrawReason::DamageArea ? 1u : 0u;
        summary.m_RectangleFallback += stats.m_RedrawReason == RedrawReason::DamageRectangles ? 1u : 0u;
        if (frame.m_GpuMilliseconds >= 0)
        {
            ++summary.m_Timed;
            gpu.push_back(frame.m_GpuMilliseconds);
            upload.push_back(frame.m_GpuUploadMilliseconds);
            draw.push_back(frame.m_GpuDrawMilliseconds);
            present.push_back(frame.m_GpuPresentMilliseconds);
        }
    }
    summary.m_CpuMedian = Percentile(cpu, .5);
    summary.m_CpuP95 = Percentile(cpu, .95);
    summary.m_InputP95 = Percentile(input, .95);
    summary.m_SwapP95 = Percentile(swap, .95);
    summary.m_GpuMedian = Percentile(gpu, .5);
    summary.m_GpuP95 = Percentile(gpu, .95);
    summary.m_GpuUploadMedian = Percentile(upload, .5);
    summary.m_GpuDrawMedian = Percentile(draw, .5);
    summary.m_GpuPresentMedian = Percentile(present, .5);
    if (summary.m_Frames)
    {
        const auto count = static_cast<double>(summary.m_Frames);
        summary.m_RepaintFraction /= count;
        summary.m_MeanGlyphs /= count;
        summary.m_MeanDrawCalls /= count;
        summary.m_ActualRate = intervals > 0 ? count * 1000 / intervals : 0;
    }
    return summary;
}

void WriteFrames(const std::filesystem::path& output, const std::vector<Frame>& frames)
{
    std::ofstream csv(output / "render-profile-frames.csv");
    if (!csv)
        throw std::runtime_error("Cannot create rendering frame report.");
    csv << "case,redraw,phase,index,frame_serial,input_wall_ms,cpu_render_wall_ms,native_swap_wall_ms,frame_interval_ms,"
           "gpu_sample_matched,gpu_total_ms,gpu_upload_ms,gpu_draw_ms,gpu_present_copy_ms,redraw_reason,dirty_tiles,"
           "dirty_tile_pixels,damage_candidate_rectangles,repaint_pixels,framebuffer_pixels,damage_rectangles,"
           "recorded_glyphs,replayed_glyphs,submitted_instances,draw_calls,instance_bytes,effect_bytes,font_bytes,"
           "gradients,shadows,style_resolutions,layout_passes,measurements,arrangements,shape_operations\n";
    csv << std::setprecision(9);
    for (const auto& frame : frames)
    {
        const auto& render = frame.m_Render;
        csv << Quoted(frame.m_Case) << ',' << frame.m_Mode << ',' << frame.m_Phase << ',' << frame.m_Index << ','
            << render.m_FrameSerial << ',' << frame.m_InputMilliseconds << ',' << frame.m_CpuMilliseconds << ','
            << frame.m_SwapMilliseconds << ',' << frame.m_IntervalMilliseconds << ','
            << (frame.m_GpuMilliseconds >= 0 ? 1 : 0) << ',';
        if (frame.m_GpuMilliseconds >= 0)
            csv << frame.m_GpuMilliseconds;
        csv << ',';
        if (frame.m_GpuUploadMilliseconds >= 0)
            csv << frame.m_GpuUploadMilliseconds;
        csv << ',';
        if (frame.m_GpuDrawMilliseconds >= 0)
            csv << frame.m_GpuDrawMilliseconds;
        csv << ',';
        if (frame.m_GpuPresentMilliseconds >= 0)
            csv << frame.m_GpuPresentMilliseconds;
        csv << ',' << Reason(render.m_RedrawReason) << ',' << render.m_DirtyTileCount << ',' << render.m_DirtyTilePixels
            << ',' << render.m_DamageCandidateRectangles << ',' << render.m_RepaintPixels << ','
            << render.m_FramebufferPixels << ',' << render.m_DamageRectangles << ',' << render.m_Glyphs << ','
            << render.m_ReplayedGlyphs << ',' << render.m_SubmittedInstances << ',' << render.m_DrawCalls << ','
            << render.m_InstanceBytes << ',' << render.m_EffectBytes << ',' << render.m_FontBytes << ','
            << render.m_Gradients << ',' << render.m_Shadows << ',' << frame.m_Ui.m_StyleResolutions << ','
            << frame.m_Ui.m_LayoutPasses << ',' << frame.m_Ui.m_Measurements << ',' << frame.m_Ui.m_Arrangements << ','
            << frame.m_Shapes << '\n';
    }
    csv.close();
    if (!csv)
        throw std::runtime_error("Cannot finish rendering frame report.");
}

void WriteSummaries(const std::filesystem::path& output, const std::vector<Summary>& summaries,
                    const std::vector<Workload>& workloads, const std::string& driver, bool software)
{
    std::ofstream csv(output / "render-profile-summary.csv");
    std::ofstream report(output / "render-profile.txt");
    if (!csv || !report)
        throw std::runtime_error("Cannot create rendering summary reports.");
    csv << "case,redraw,measured_frames,matched_gpu_samples,gpu_sample_coverage,cpu_render_median_ms,cpu_render_p95_ms,"
           "input_p95_ms,native_swap_p95_ms,gpu_total_median_ms,gpu_total_p95_ms,gpu_upload_median_ms,gpu_draw_median_ms,"
           "gpu_present_copy_median_ms,mean_repaint_fraction,mean_replayed_glyphs,mean_draw_calls,full_frames,"
           "damage_area_fallbacks,damage_rectangle_fallbacks,actual_scripted_fps\n";
    csv << std::setprecision(9);
    report << "EStudioUI paced rendering profile\nDriver: " << driver << '\n'
           << "Software renderer detected: " << (software ? "yes" : "no (driver description heuristic)") << "\n\n"
           << "Scripted forced frames; this is not an idle scheduler or GPU utilization measurement.\n"
           << "No pixel readback or glFinish is used. GPU timestamps match their originating frame serial.\n"
           << "GPU upload/draw/presentation-copy scopes exclude native swap and desktop composition.\n"
           << "GPU scopes can include GPU idle gaps between commands; they are not Task Manager busy percentages.\n"
           << "CPU render wall time includes preparation, paint recording and GL submission; input/swap are separate.\n"
           << "Missing GPU samples remain empty in CSV and -1 in aggregates; they are never treated as zero.\n"
           << "Entry/warmup/drain frames are stored separately and excluded from steady-state percentiles.\n"
           << "GPU glyph caches are cleared before each run. CPU font/shaping caches can persist between runs.\n"
           << "Native focus, OS scheduling, power state and other applications affect these measurements.\n"
           << "Use the same resolution, DPI, case, rate and build for before/after comparisons.\n";
    if (software)
        report << "SOFTWARE GL: timings describe the host CPU renderer, not hardware GPU load or expected FPS.\n";
    report << '\n';
    for (const auto& workload : workloads)
        report << workload.m_Name << ": " << workload.m_Description << '\n';
    report << '\n' << std::fixed << std::setprecision(3);
    for (const auto& summary : summaries)
    {
        const double coverage = summary.m_Frames
                                    ? static_cast<double>(summary.m_Timed) / static_cast<double>(summary.m_Frames)
                                    : 0;
        csv << Quoted(summary.m_Case) << ',' << summary.m_Mode << ',' << summary.m_Frames << ',' << summary.m_Timed
            << ',' << coverage << ',' << summary.m_CpuMedian << ',' << summary.m_CpuP95 << ',' << summary.m_InputP95
            << ',' << summary.m_SwapP95 << ',' << summary.m_GpuMedian << ',' << summary.m_GpuP95 << ','
            << summary.m_GpuUploadMedian << ',' << summary.m_GpuDrawMedian << ',' << summary.m_GpuPresentMedian << ','
            << summary.m_RepaintFraction << ',' << summary.m_MeanGlyphs << ',' << summary.m_MeanDrawCalls << ','
            << summary.m_Full << ',' << summary.m_AreaFallback << ',' << summary.m_RectangleFallback << ','
            << summary.m_ActualRate << '\n';
        report << summary.m_Case << " / " << summary.m_Mode << ": GPU median/p95 " << summary.m_GpuMedian << '/'
               << summary.m_GpuP95 << " ms; CPU render median/p95 " << summary.m_CpuMedian << '/' << summary.m_CpuP95
               << " ms; repaint " << summary.m_RepaintFraction * 100 << "%; glyphs " << summary.m_MeanGlyphs
               << "; draws " << summary.m_MeanDrawCalls << "; GPU samples " << summary.m_Timed << '/'
               << summary.m_Frames << "; actual scripted FPS " << summary.m_ActualRate << "; area/rectangle fallbacks "
               << summary.m_AreaFallback << '/' << summary.m_RectangleFallback << ".\n";
    }
    csv.close();
    report.close();
    if (!csv || !report)
        throw std::runtime_error("Cannot finish rendering summary reports.");
}

void WriteMetadata(Window& window, const std::filesystem::path& output, const RenderingProfileOptions& options,
                   const std::string& driver, bool software)
{
    const auto metrics = window.Platform().GetWindowMetrics();
    std::ofstream json(output / "render-profile-metadata.json");
    if (!json)
        throw std::runtime_error("Cannot create rendering metadata.");
    json << "{\n  \"schema\": 1,\n  \"driver\": " << JsonString(driver)
         << ",\n  \"build_configuration\": " << JsonString(ESTUDIOUI_DEMO_BUILD_CONFIGURATION)
         << ",\n  \"headless\": " << (window.IsHeadless() ? "true" : "false")
         << ",\n  \"software_renderer_detected\": " << (software ? "true" : "false")
         << ",\n  \"logical_width\": " << metrics.m_LogicalSize.m_X
         << ",\n  \"logical_height\": " << metrics.m_LogicalSize.m_Y
         << ",\n  \"framebuffer_width\": " << metrics.m_FramebufferSize.m_X
         << ",\n  \"framebuffer_height\": " << metrics.m_FramebufferSize.m_Y
         << ",\n  \"pixel_scale_x\": " << metrics.m_PixelScale.m_X
         << ",\n  \"pixel_scale_y\": " << metrics.m_PixelScale.m_Y
         << ",\n  \"requested_scripted_fps\": " << options.m_FrameRate
         << ",\n  \"gpu_timestamp_bits\": " << window.Graphics().GetStats().m_GpuTimestampBits
         << ",\n  \"warmup_frames_per_run\": " << options.m_Warmup
         << ",\n  \"measured_frames_per_run\": " << options.m_Frames
         << ",\n  \"case_filter\": " << JsonString(options.m_Case)
         << ",\n  \"gpu_sample_association\": \"exact originating frame serial\","
            "\n  \"swap_interval_request\": 0,\n  \"synchronous_readbacks\": false,"
            "\n  \"workload_mode\": \"paced forced-frame benchmark, not normal idle scheduling\"\n}\n";
    json.close();
    if (!json)
        throw std::runtime_error("Cannot finish rendering metadata.");
}
} // namespace

void ProfileRenderingWorkload(Window& window, TabWidget& tabs, const std::filesystem::path& assets,
                              const std::filesystem::path& theme, const std::filesystem::path& output,
                              const RenderingProfileOptions& options)
{
    if (options.m_Frames < 1 || options.m_Frames > 1800 || options.m_Warmup < 0 || options.m_Warmup > 600 ||
        !std::isfinite(options.m_FrameRate) || options.m_FrameRate < 0 || options.m_FrameRate > 240)
        throw std::invalid_argument("Rendering profile needs 1..1800 frames, 0..600 warmup frames and 0..240 Hz.");
    const std::array groups{"all", "rich-css", "widget-styles", "isolated-slider", "text", "baseline"};
    if (std::find(groups.begin(), groups.end(), options.m_Case) == groups.end())
        throw std::invalid_argument("Unknown rendering profile case: " + options.m_Case);
    std::filesystem::create_directories(output);
    auto& ui = window.Ui();
    auto& gpu = window.Graphics();
    auto* native = reinterpret_cast<GLFWwindow*>(window.GetContextIdentity());
    const int originalPage = tabs.GetCurrentIndex();
    const bool originalPartial = gpu.IsPartialRedrawEnabled();
    struct Restore
    {
        Window& m_Window;
        TabWidget& m_Tabs;
        int m_Page;
        bool m_Partial;
        ~Restore()
        {
            if (m_Window.IsClosed())
                return;
            try
            {
                m_Window.Ui().Deactivate();
                m_Window.Graphics().SetGpuTimingEnabled(false);
                m_Window.Graphics().SetPartialRedrawEnabled(m_Partial);
                m_Tabs.SetCurrentIndex(m_Page);
                glfwSwapInterval(1);
            }
            catch (...)
            {
            }
        }
    } restore{window, tabs, originalPage, originalPartial};
    std::string css = Read(theme);
    for (const char* file : {"animation-start.css", "rich-buttons.css", "rich-widgets.css", "text-controls.css",
                             "essential-widgets.css", "command-dialogs.css", "image-widgets.css"})
        css += '\n' + Read(assets / file);
    css += R"CSS(
        #profile-isolated { gap: 12px; }
        #profile-isolated .profile-surface { padding: 30px; border-radius: 14px; height: 180px;
            background: linear-gradient(110deg, #152c46, #293049); box-shadow: 0 8px 20px #0008; }
        #profile-isolated slider { height: 48px; background: linear-gradient(90deg, #263a56, #283d45);
            border: 1px solid #63849c; border-radius: 10px; box-shadow: 0 4px 12px #0008; }
        #profile-isolated slider::groove { height: 8px; border-radius: 4px; background: #172334; }
        #profile-isolated slider::fill { background: linear-gradient(90deg, #398dff, #bc63e5); }
        #profile-isolated slider::handle { width: 22px; height: 22px; background: #e6f1ff;
            border-radius: 11px; box-shadow: 0 2px 6px #0008; }
        #profile-isolated.profile-flat .profile-surface { background-image: none; box-shadow: none; }
        #profile-isolated.profile-flat slider { background-image: none; box-shadow: none; }
        #profile-isolated.profile-flat slider::fill { background-image: none; background: #398dff; }
        #profile-isolated.profile-flat slider::handle { box-shadow: none; }
        #profile-text { gap: 0px; overflow: hidden; }
        #profile-text .profile-note { height: 22px; font-size: 13px; }
        #profile-text textbox { height: 22px; min-height: 22px; font-size: 16px; padding: 0px 2px;
            margin: 0px; border: none; border-radius: 0px; box-shadow: none; background-image: none; }
    )CSS";
    std::string error;
    if (!ui.SetStyleSheet(css, error))
        throw std::runtime_error("Rendering profile CSS: " + error);
    const int richPage = FindPage(tabs, [](Widget& page) { return page.GetId() == "rich-css"; });
    const int stylesPage = FindPage(
        tabs, [](Widget& page)
        {
            return Find<Label>(page, [](const Label& label)
                               { return label.GetText() == "One toolkit. Three personalities."; }) != nullptr;
        });
    auto& isolated = tabs.AddTab("GPU profile / isolated slider");
    isolated.SetId("profile-isolated");
    isolated.Add<Label>("Isolated slider: identical geometry with effects enabled and disabled.");
    auto& surface = isolated.Add<Widget>();
    surface.AddClass("profile-surface");
    auto& isolatedSlider = surface.Add<Slider>();
    isolatedSlider.SetRange(0, 100);
    isolatedSlider.SetStep(.5);
    isolatedSlider.SetValue(50);
    const int isolatedPage = tabs.GetTabCount() - 1;
    auto& textPage = tabs.AddTab("GPU profile / text foundation");
    textPage.SetId("profile-text");
    textPage.Add<Label>("Dense mixed-script single-line controls; document editing remains a separate feature.")
        .AddClass("profile-note");
    std::vector<TextBox*> editors;
    for (int line = 0; line < 36; ++line)
    {
        auto& editor = textPage.Add<TextBox>("Line " + std::to_string(line + 1) +
                                             " / GPU typography: Café Ελληνικά Кириллица العربية עברית 0123456789");
        editor.SetHeight(SizeRule::Fixed(22));
        editors.push_back(&editor);
    }
    const int densePage = tabs.GetTabCount() - 1;
    const std::string originalText = editors[4]->GetText();
    std::vector<Workload> workloads{
        {"static-presentation-baseline", "baseline",
         "Actual Inputs page, forced static frames; not idle scheduler power.", 0},
        {"rich-css-animations", "rich-css",
         "Actual Rich CSS page with its persistent CSS animations and effects.", richPage},
        {"widget-styles-slider", "widget-styles",
         "Actual styled slider drag, including synchronized NumericInput and distant status updates.", stylesPage},
        {"isolated-slider-effects", "isolated-slider",
         "Slider pointer drag without gallery synchronization; gradients and analytic shadows enabled.",
         isolatedPage, &isolatedSlider},
        {"isolated-slider-flat", "isolated-slider",
         "Same slider geometry/input with gradients and shadows disabled.",
         isolatedPage, &isolatedSlider, nullptr, false, true},
        {"dense-text-edit", "text",
         "Existing mixed-script controls; distinct fixed-length commits require fresh shaping in both redraw modes.",
         densePage, nullptr, editors[4]},
        {"dense-text-selection", "text",
         "Captured pointer selection in a mixed-script editor among dense unchanged text.",
         densePage, nullptr, editors[4], true}};
    std::erase_if(workloads, [&](const Workload& workload)
                  { return options.m_Case != "all" && workload.m_Group != options.m_Case; });
    const auto driver = gpu.GetDriverDescription();
    std::string lowerDriver = driver;
    std::transform(lowerDriver.begin(), lowerDriver.end(), lowerDriver.begin(),
                   [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
    const bool software = window.IsHeadless() || lowerDriver.find("llvmpipe") != std::string::npos ||
                          lowerDriver.find("softpipe") != std::string::npos ||
                          lowerDriver.find("software") != std::string::npos;
    gpu.SetGpuTimingEnabled(true);
    glfwMakeContextCurrent(native);
    glfwSwapInterval(0);
    if (!window.IsHeadless())
        window.Platform().FocusWindow();
    std::vector<Frame> frames;
    const auto capacity = static_cast<std::size_t>(options.m_Frames + options.m_Warmup + 17) * workloads.size() * 2;
    frames.reserve(capacity);
    std::unordered_map<std::uint64_t, std::size_t> frameIndices;
    frameIndices.reserve(capacity);
    std::vector<Summary> summaries;
    std::uint64_t textRevision = 0;
    for (const auto& workload : workloads)
    {
        for (const bool retained : {false, true})
        {
            if (window.IsClosed())
                throw std::runtime_error("Rendering profile cancelled because its window was closed.");
            const std::string mode = retained ? "retained" : "full";
            std::cout << "Rendering profile: " << workload.m_Name << " / " << mode << std::endl;
            ui.Deactivate();
            tabs.SetCurrentIndex(workload.m_Page);
            if (workload.m_Flat)
                isolated.AddClass("profile-flat");
            else
                isolated.RemoveClass("profile-flat");
            if (workload.m_Slider)
                workload.m_Slider->SetValue(50);
            if (workload.m_Editor)
                workload.m_Editor->SetText(originalText);
            gpu.SetPartialRedrawEnabled(retained);
            gpu.ClearFontCache();
            Slider* slider = workload.m_Slider;
            bool sliderChanged = false;
            Clock::time_point previousStart{};
            auto deadline = Clock::now();
            const auto interval = options.m_FrameRate > 0
                                      ? std::chrono::duration_cast<Clock::duration>(
                                            std::chrono::duration<double>(1 / options.m_FrameRate))
                                      : Clock::duration::zero();
            const auto record = [&](int index, std::string phase, bool input)
            {
                if (interval != Clock::duration::zero())
                    std::this_thread::sleep_until(deadline);
                const auto frameStart = Clock::now();
                deadline = frameStart + interval;
                glfwPollEvents();
                if (window.IsClosed())
                    throw std::runtime_error("Rendering profile cancelled because its window was closed.");
                Frame frame;
                frame.m_Case = workload.m_Name;
                frame.m_Mode = mode;
                frame.m_Phase = std::move(phase);
                frame.m_Index = index;
                frame.m_IntervalMilliseconds =
                    previousStart == Clock::time_point{}
                        ? 0
                        : std::chrono::duration<double, std::milli>(frameStart - previousStart).count();
                previousStart = frameStart;
                const auto beforeUi = ui.GetStats();
                const auto beforeFonts = ui.GetFont().GetStats();
                const double beforeSlider = slider ? slider->GetValue() : 0;
                const auto inputStart = Clock::now();
                if (input)
                {
                    const double cycle = static_cast<double>(index % 60) / 59;
                    const float fraction = static_cast<float>(.1 + .8 * (cycle < .5 ? cycle * 2 : 2 - cycle * 2));
                    if (slider || (workload.m_Editor && workload.m_Selection))
                    {
                        const auto bounds = slider ? slider->GetContentBounds() : workload.m_Editor->GetContentBounds();
                        const Point point{bounds.m_X + bounds.m_Width * fraction,
                                          bounds.m_Y + bounds.m_Height * .5f};
                        if (index == 0)
                            ui.PointerDown(point);
                        else
                            ui.PointerMove(point);
                    }
                    else if (workload.m_Editor)
                    {
                        if (index == 0)
                            ui.SetFocus(workload.m_Editor);
                        workload.m_Editor->SetSelection(originalText.size(), workload.m_Editor->GetText().size());
                        // The admitted frame limits keep this suffix at six digits, including across redraw modes.
                        ui.TextInput("edit" + std::to_string(100000 + ++textRevision));
                    }
                    else
                        ui.PointerMove({2, 2});
                }
                frame.m_InputMilliseconds = Milliseconds(inputStart);
                if (slider && slider->GetValue() != beforeSlider)
                    sliderChanged = true;
                const auto renderStart = Clock::now();
                window.RenderFrame();
                frame.m_CpuMilliseconds = Milliseconds(renderStart);
                frame.m_Render = gpu.GetStats();
                const auto swapStart = Clock::now();
                if (!window.IsHeadless())
                    glfwSwapBuffers(native);
                else
                    glFlush();
                frame.m_SwapMilliseconds = Milliseconds(swapStart);
                const auto afterUi = ui.GetStats();
                frame.m_Ui.m_StyleResolutions = afterUi.m_StyleResolutions - beforeUi.m_StyleResolutions;
                frame.m_Ui.m_LayoutPasses = afterUi.m_LayoutPasses - beforeUi.m_LayoutPasses;
                frame.m_Ui.m_Measurements = afterUi.m_Measurements - beforeUi.m_Measurements;
                frame.m_Ui.m_Arrangements = afterUi.m_Arrangements - beforeUi.m_Arrangements;
                frame.m_Shapes = ui.GetFont().GetStats().m_Shapes - beforeFonts.m_Shapes;
                if (!frameIndices.emplace(frame.m_Render.m_FrameSerial, frames.size()).second)
                    throw std::runtime_error("A profile frame was not submitted; keep the window restored and visible.");
                frames.push_back(std::move(frame));
                const auto& stats = frames.back().m_Render;
                if (stats.m_GpuMilliseconds >= 0)
                {
                    const auto found = frameIndices.find(stats.m_GpuTimingFrame);
                    if (found != frameIndices.end())
                    {
                        auto& completed = frames[found->second];
                        const double sum = stats.m_GpuUploadMilliseconds + stats.m_GpuDrawMilliseconds +
                                           stats.m_GpuPresentMilliseconds;
                        if (!std::isfinite(stats.m_GpuMilliseconds) ||
                            !std::isfinite(stats.m_GpuUploadMilliseconds) ||
                            !std::isfinite(stats.m_GpuDrawMilliseconds) ||
                            !std::isfinite(stats.m_GpuPresentMilliseconds) || stats.m_GpuUploadMilliseconds < 0 ||
                            stats.m_GpuDrawMilliseconds < 0 || stats.m_GpuPresentMilliseconds < 0 ||
                            std::abs(sum - stats.m_GpuMilliseconds) > std::max(.000001, stats.m_GpuMilliseconds * .000001))
                            throw std::runtime_error("GPU timing scopes do not match their completed frame.");
                        completed.m_GpuMilliseconds = stats.m_GpuMilliseconds;
                        completed.m_GpuUploadMilliseconds = stats.m_GpuUploadMilliseconds;
                        completed.m_GpuDrawMilliseconds = stats.m_GpuDrawMilliseconds;
                        completed.m_GpuPresentMilliseconds = stats.m_GpuPresentMilliseconds;
                    }
                }
            };
            record(-1, "entry", false);
            if (workload.m_Group == "widget-styles")
            {
                slider = Find<Slider>(tabs.GetPage(stylesPage), [](const Slider& candidate)
                                      {
                                          return candidate.IsVisible() && candidate.IsEnabled() &&
                                                 candidate.GetOrientation() == Axis::Horizontal;
                                      });
                if (!slider)
                    throw std::runtime_error("Actual Widget styles horizontal slider is missing.");
            }
            WriteMetadata(window, output, options, driver, software);
            for (int frame = 0; frame < options.m_Warmup + options.m_Frames; ++frame)
                record(frame, frame < options.m_Warmup ? "warmup" : "measured", true);
            if (slider && !sliderChanged)
                throw std::runtime_error("Rendering profile input did not change the actual slider.");
            ui.Deactivate();
            for (int frame = 0; frame < 16; ++frame)
                record(frame, "drain", false);
            auto summary = Summarize(frames, workload.m_Name, mode);
            if (summary.m_Frames != static_cast<std::size_t>(options.m_Frames) || summary.m_Timed > summary.m_Frames)
                throw std::runtime_error("Rendering profile frame/sample counts are inconsistent.");
            if (!retained && summary.m_Full != summary.m_Frames)
                throw std::runtime_error("The full-redraw reference did not repaint every measured frame.");
            if (retained && workload.m_Group == "baseline" && options.m_Warmup >= 4 &&
                (summary.m_RepaintFraction != 0 || summary.m_MeanDrawCalls != 0 || summary.m_MeanGlyphs != 0))
                throw std::runtime_error("Static retained frames unexpectedly replayed analytic work.");
            if (workload.m_Selection && options.m_Warmup >= 4)
                for (const auto& frame : frames)
                    if (frame.m_Case == workload.m_Name && frame.m_Mode == mode && frame.m_Phase == "measured" &&
                        frame.m_Shapes != 0)
                        throw std::runtime_error("Pointer selection unexpectedly reshaped unchanged mixed text.");
            summaries.push_back(std::move(summary));
            for (auto& completed : summaries)
                completed = Summarize(frames, completed.m_Case, completed.m_Mode);
            WriteFrames(output, frames);
            WriteSummaries(output, summaries, workloads, driver, software);
        }
    }
    std::cout << "Rendering profile written to " << output << '\n';
}
} // namespace EStudioUI
