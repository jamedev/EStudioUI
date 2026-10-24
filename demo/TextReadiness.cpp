#include "TextReadiness.h"
#include "TextGallery.h"
#include "../benchmarks/TextCorpus.h"

#include <EStudioUI/FontEditor.h>
#include <EStudioUI/Label.h>
#include <EStudioUI/TextBox.h>
#include <glad/gl.h>

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace EStudioUI
{
namespace
{
int s_Checks = 0;

void Check(bool value, const char* message)
{
    ++s_Checks;
    if (!value)
    {
        throw std::runtime_error(std::string("Text readiness GPU: ") + message);
    }
}

std::vector<unsigned char> Pixels()
{
    GLint viewport[4]{};
    glGetIntegerv(GL_VIEWPORT, viewport);
    std::vector<unsigned char> result(static_cast<std::size_t>(viewport[2]) * static_cast<std::size_t>(viewport[3]) *
                                      4);
    glReadPixels(0, 0, viewport[2], viewport[3], GL_RGBA, GL_UNSIGNED_BYTE, result.data());
    return result;
}

void DrawPair(Renderer& gpu, const TextLayout& first, const TextLayout& second)
{
    const Rect clip{0, 0, 720, 420};
    gpu.BeginFrame(720, 420, 720, 420, {0.95f, 0.96f, 0.97f, 1});
    gpu.DrawSolid({18, 18, 680, 380}, {0.12f, 0.16f, 0.20f, 1}, clip);
    gpu.DrawText(first, {50, 130}, {0.8f, 0.9f, 1, 1}, clip);
    // The later miss can relocate the first glyph's already recorded address.
    gpu.PushState({1, 0, 0, 1, 5, 8});
    gpu.PushClip({130, 40, 510, 270}, 12);
    gpu.DrawText(second, {160, 130}, {1, 0.85f, 0.6f, 0.85f}, clip);
    gpu.DrawSolid({80, 85, 190, 20}, {0.2f, 0.7f, 0.9f, 0.25f}, clip);
    gpu.PopState();
    gpu.EndFrame();
}
} // namespace

void VerifyTextReadiness(Window& window, const std::filesystem::path& assets, const std::filesystem::path& output)
{
    s_Checks = 0;
    std::filesystem::create_directories(output);
    WindowOptions options;
    options.m_Width = 720;
    options.m_Height = 720;
    options.m_Headless = window.IsHeadless();
    TextGallery::AddFonts(options, assets, assets / "NotoSans.ttf");
    Window probe(options, assets / "NotoSans.ttf");
    auto& font = probe.Ui().GetFont();
    auto& gpu = probe.Graphics();
    Renderer reference(font);
    reference.SetPartialRedrawEnabled(false);
    const std::string alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    std::size_t largest = 0;
    for (const auto c : alphabet)
    {
        gpu.ClearFontCache();
        const auto layout = font.Shape(std::string(1, c), 72);
        DrawPair(gpu, layout, layout);
        largest = std::max(largest, gpu.GetStats().m_FontBytes);
    }
    const auto budget = std::max<std::size_t>(4096, largest * 3);
    gpu.SetFontCacheLimit(budget);
    auto first = font.Shape("A", 72);
    // Keep unused residency ahead of A so compaction must change a live address.
    DrawPair(gpu, font.Shape("O", 72), first);
    std::size_t evictions = 0, compactions = 0;
    std::ofstream frames(output / "text-cache-churn.csv");
    frames << "frame,budget,font_bytes,visible_glyphs,evictions,compactions,font_upload_bytes\n";
    for (std::size_t i = 0; i < alphabet.size(); ++i)
    {
        const auto next = font.Shape(std::string(1, alphabet[i]), 72);
        DrawPair(gpu, first, next);
        const auto actual = Pixels();
        const auto stats = gpu.GetStats();
        DrawPair(reference, first, next);
        Check(actual == Pixels(), "every compacted frame equals an independent full redraw exactly");
        Check(stats.m_FontBytes <= budget && stats.m_VisibleGlyphs <= 2,
              "glyph residency and current-frame pins stay inside the configured budget");
        evictions += stats.m_FontEvictions;
        compactions += stats.m_FontCompactions;
        frames << i << ',' << budget << ',' << stats.m_FontBytes << ',' << stats.m_VisibleGlyphs << ','
               << stats.m_FontEvictions << ',' << stats.m_FontCompactions << ',' << stats.m_FontUploadBytes << '\n';
    }
    Check(evictions > 0 && compactions > 0, "small-cache churn actually exercises retirement and relocation");
    const auto last = font.Shape(std::string(1, alphabet.back()), 72);
    DrawPair(gpu, first, last);
    Check(gpu.GetStats().m_DrawCalls == 0 && gpu.GetStats().m_FontUploadBytes == 0 &&
              gpu.GetStats().m_RepaintPixels == 0,
          "an identical frame performs no analytic drawing or font upload after compaction");
    const auto completed = Pixels();
    const auto serial = gpu.GetStats().m_FrameSerial;
    gpu.SavePpm(output / "text-cache-compaction.ppm");
    const Rect clip{0, 0, 720, 420};
    gpu.BeginFrame(960, 720, 960, 720, {0, 0, 0, 1});
    bool failed = false;
    try
    {
        gpu.DrawText(font.Shape(alphabet, 12), {10, 100}, {1, 1, 1, 1}, clip);
    }
    catch (const FontCacheCapacityError& error)
    {
        failed = error.GetReason() == FontCacheCapacityReason::VisibleWorkingSet &&
                 error.GetRequiredBytes() > error.GetByteLimit() && error.GetRequiredGlyphs() > 1;
    }
    Check(failed, "oversized visible working sets return an owned, actionable capacity diagnostic");
    bool endRejected = false;
    try
    {
        gpu.EndFrame();
    }
    catch (const FontCacheCapacityError&)
    {
        endRejected = true;
    }
    Check(endRejected, "failed admission cannot submit a silently truncated frame");
    gpu.AbortFrame();
    Check(gpu.GetStats().m_FrameSerial == serial && Pixels() == completed,
          "aborting a pre-submission failure preserves the last completed pixels and serial");
    const auto preserved = gpu.ReadPixel({30, 30});
    Check(preserved.m_R < 0.2f, "the previous retained framebuffer remains available for capture after abort");
    gpu.SetFontCacheLimit(32 * 1024 * 1024);
    DrawPair(gpu, first, last);
    DrawPair(reference, first, last);
    const auto recovered = Pixels();
    DrawPair(gpu, first, last);
    Check(Pixels() == recovered, "the next valid frame recovers correctly from failed admission");
    gpu.ClearFontCache();
    gpu.BeginFrame(720, 420, 720, 420, {0, 0, 0, 1});
    gpu.DrawText(font.Shape(alphabet, 72), {10000, 10000}, {1, 1, 1, 1}, clip);
    gpu.DrawText(font.Shape("     ", 72), {10, 100}, {1, 1, 1, 1}, clip);
    gpu.EndFrame();
    Check(gpu.GetStats().m_CachedGlyphs == 0 && gpu.GetStats().m_VisibleGlyphs == 0 &&
              gpu.GetStats().m_FontBytes == 24 && gpu.GetStats().m_FontUploadBytes == 0,
          "offscreen text and whitespace do not admit glyphs or upload font buffers");
    gpu.SetFontCacheLimit(1024);
    gpu.BeginFrame(720, 420, 720, 420, {0, 0, 0, 1});
    bool singleFailed = false;
    try
    {
        gpu.DrawText(font.Shape("@", 72), {20, 100}, {1, 1, 1, 1}, clip);
    }
    catch (const FontCacheCapacityError& error)
    {
        singleFailed = error.GetReason() == FontCacheCapacityReason::SingleGlyph;
    }
    gpu.AbortFrame();
    Check(singleFailed, "one over-budget outline is distinguished from a large visible working set");
    gpu.SetFontCacheLimit(32 * 1024 * 1024);
    BuildTextReadiness(probe);
    probe.RenderFrame(1);
    Check(gpu.GetStats().m_Glyphs > 0,
          "the focused native acceptance harness builds and renders through ordinary widgets");
    gpu.SavePpm(output / "text-readiness-harness.ppm");
    std::ofstream report(output / "text-readiness-gpu.txt");
    report << "Text readiness GPU: " << s_Checks << " checks passed\n"
           << "Cache budget: " << budget << " bytes; evictions: " << evictions << "; compactions: " << compactions
           << '\n'
           << "Driver: " << gpu.GetDriverDescription() << '\n'
           << "Physical IME, Narrator, mixed-monitor DPI and hardware performance: NOT TESTED\n";
    std::cout << "Text readiness GPU: " << s_Checks << " checks passed\n";
}

void ProfileTextReadiness(Window& window, const std::filesystem::path& output)
{
    std::filesystem::create_directories(output);
    auto& font = window.Ui().GetFont();
    auto& gpu = window.Graphics();
    gpu.SetGpuTimingEnabled(true);
    const auto corpus = Testing::MakeTextCorpus();
    std::ofstream csv(output / "text-profile.csv");
    csv << "case,phase,frame,stored_paragraphs,stored_bytes,requested_paragraphs,cpu_ms,shapes,shape_hits,shape_bytes,"
           "geometry_bytes,visible_glyphs,font_bytes,font_upload_bytes,font_evictions,font_compactions,draw_calls,"
           "repaint_pixels,gpu_ms,gpu_sample_frame\n";
    const Rect clip{0, 0, 960, 720};
    for (const auto& fixture : corpus)
    {
        for (const auto* phase : {"cold-and-warm", "scroll", "edit", "font-churn", "wrap"})
        {
            font.ClearCaches();
            gpu.ClearFontCache();
            for (int frame = 0; frame < 24; ++frame)
            {
                const auto before = font.GetStats();
                const auto start = std::chrono::steady_clock::now();
                const auto count = std::min(fixture.m_VisibleCount, fixture.m_Paragraphs.size());
                Style style;
                style.m_FontSize = 16;
                if (std::string_view(phase) == "font-churn")
                {
                    style.m_FontWeight = frame % 2 ? 700.0f : 400.0f;
                    style.m_FontSize += static_cast<float>(frame % 3);
                }
                gpu.BeginFrame(960, 720, 960, 720, {0.96f, 0.97f, 0.98f, 1});
                float baseline = 24;
                for (std::size_t row = 0; row < count; ++row)
                {
                    const auto offset = std::string_view(phase) == "scroll" ? static_cast<std::size_t>(frame) : 0;
                    auto text = fixture.m_Paragraphs[(row + offset) % fixture.m_Paragraphs.size()];
                    if (std::string_view(phase) == "edit" && row == 0)
                    {
                        text += " edit=" + std::to_string(frame);
                    }
                    if (std::string_view(phase) == "wrap")
                    {
                        // Long wrapping still exercises the existing paragraph API; the viewport
                        // only admits visible glyphs. This is not a document layout implementation.
                        for (const auto& line : font.Wrap(text, style, 900))
                        {
                            if (baseline < 720)
                            {
                                gpu.DrawText(line, {18, baseline}, {0.08f, 0.1f, 0.13f, 1}, clip);
                            }
                            baseline += 22;
                        }
                    }
                    else
                    {
                        gpu.DrawText(font.Shape(text, style, true), {18, baseline}, {0.08f, 0.1f, 0.13f, 1}, clip);
                        baseline += 21;
                    }
                }
                gpu.EndFrame();
                const auto elapsed =
                    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
                const auto after = font.GetStats();
                const auto stats = gpu.GetStats();
                csv << fixture.m_Id << ',' << phase << ',' << frame << ',' << fixture.m_Paragraphs.size() << ','
                    << fixture.m_Bytes << ',' << count << ',' << elapsed << ',' << after.m_Shapes - before.m_Shapes
                    << ',' << after.m_CacheHits - before.m_CacheHits << ',' << after.m_ShapeBytes << ','
                    << after.m_GeometryBytes << ',' << stats.m_VisibleGlyphs << ',' << stats.m_FontBytes << ','
                    << stats.m_FontUploadBytes << ',' << stats.m_FontEvictions << ',' << stats.m_FontCompactions << ','
                    << stats.m_DrawCalls << ',' << stats.m_RepaintPixels << ',' << stats.m_GpuMilliseconds << ','
                    << stats.m_GpuTimingFrame << '\n';
            }
        }
    }
    if (!csv)
    {
        throw std::runtime_error("Cannot write text profile CSV.");
    }
    std::ofstream metadata(output / "text-profile.txt");
    metadata
        << "Driver: " << gpu.GetDriverDescription() << '\n'
        << "Viewport: 960x720 logical and physical pixels; 24 unpaced frames per case/phase.\n"
        << "No synchronous pixel readback; CPU includes shaping, wrapping, recording and submission.\n"
        << "GPU times are asynchronous command intervals, not utilization; sample frame identifies delayed results.\n"
        << "Wrap currently computes the whole requested paragraph. This fixture is not a document editor.\n";
    std::cout << "Text profile: " << (output / "text-profile.csv").string() << '\n';
}

void BuildTextReadiness(Window& window)
{
    window.SetTitle("EStudioUI — text readiness acceptance");
    std::string error;
    if (!window.Ui().SetStyleSheet(
            "* { color: #20252b; } window { background: #f5f6f8; padding: 22px; gap: 10px; } "
            "label { color: #20252b; font-size: 16px; height: auto; } "
            "textbox, combobox, numericinput { height: 38px; background: white; color: #20252b; border: 1px solid "
            "#9aa4af; padding: 8px; } "
            "combobox, numericinput { padding: 0px; gap: 0px; } "
            "combobox textbox, numericinput textbox { padding: 6px; background: transparent; border-width: 0px; } "
            "toolbutton { padding: 0px; color: #20252b; background: #e5e8ed; border: 1px solid #9aa4af; } "
            "checkbox { height: 38px; color: #20252b; } "
            "textbox::selection { background: #2874b5; color: white; } "
            "textbox::preedit { background: #b05010; height: 2px; } "
            "fonteditor { height: auto; gap: 8px; } .font-preview { background: white; padding: 10px; } "
            "#rtl { direction: rtl; language: ar; }",
            error))
    {
        throw std::runtime_error(error);
    }
    auto& root = window.Ui().Root();
    root.Add<Label>("Text readiness — native input and typography");
    auto& instructions = root.Add<Label>(
        "Check IME commit/cancel, dead keys, AltGr, mixed-direction dragging, copy/paste, undo/redo, "
        "focus changes, candidate-window placement, Narrator and moving between different-DPI monitors. "
        "Record each observed result in acceptance.json; automated GPU tests cannot establish these results.");
    instructions.SetWordWrap(true);
    auto& mixed = root.Add<TextBox>("abc אבג XYZ / العربية / नमस्ते / é");
    mixed.SetAccessibleName("Mixed-direction text acceptance field");
    auto& rtl = root.Add<TextBox>("العربية English עברית");
    rtl.SetId("rtl");
    rtl.SetAccessibleName("Right-to-left text acceptance field");
    auto& empty = root.Add<TextBox>();
    empty.SetPlaceholder("IME, dead-key and AltGr input — compose here");
    empty.SetAccessibleName("Native composition acceptance field");
    auto& fonts = root.Add<FontEditor>();
    fonts.RefreshFamilies();
    fonts.SetPreviewText("Font preview / العربية / עברית / नमस्ते / é / 0123456789");
    auto selection = fonts.GetFont();
    selection.m_Direction = "rtl";
    selection.m_Language = "he";
    fonts.SetFont(std::move(selection));
}
} // namespace EStudioUI
