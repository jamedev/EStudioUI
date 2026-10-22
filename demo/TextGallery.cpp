#include "TextGallery.h"

#include <EStudioUI/Button.h>
#include <EStudioUI/TextBox.h>
#include <glad/gl.h>

#include <algorithm>
#include <fstream>
#include <stdexcept>

namespace EStudioUI
{
namespace
{
void Check(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(std::string("Text GPU: ") + message);
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
} // namespace

void TextGallery::AddFonts(WindowOptions& options, const std::filesystem::path& assets,
                           const std::filesystem::path& primary)
{
    auto directory = assets / "fonts";
    if (!std::filesystem::exists(directory / "NotoSansArabic.ttf"))
        directory = primary.parent_path();
    for (const auto* name : {"NotoSansArabic.ttf", "NotoSansHebrew.ttf", "NotoSansDevanagari.ttf"})
        if (std::filesystem::exists(directory / name))
            options.m_Fonts.push_back({directory / name});
    options.m_Fonts.push_back({primary, {}, 700});
    if (std::filesystem::exists(directory / "NotoSansItalic.ttf"))
        options.m_Fonts.push_back({directory / "NotoSansItalic.ttf", {}, 400, true});
}

TextGallery::TextGallery(Window& window, Widget& page) : m_Window(window)
{
    page.SetId("text-foundation");
    auto& heading = page.Add<Label>("Typography that follows your application");
    heading.AddClass("text-heading");
    auto& note = page.Add<Label>("Mix scripts, select across directions, and use the arrow keys. Font families, "
                                 "weight, style, direction and language are inherited CSS properties.");
    note.AddClass("text-note");
    page.Add<Label>("Regular / Café / Ελληνικά / Кириллица").AddClass("text-caption");
    auto& bold = page.Add<Label>("Bold uses another actual font instance");
    bold.AddClass("text-caption");
    bold.AddClass("text-bold");
    auto& italic = page.Add<Label>("Italic comes from an actual italic font");
    italic.AddClass("text-caption");
    italic.AddClass("text-italic");
    page.Add<Label>("GPU / العربية / עברית / नमस्ते").AddClass("text-caption");
    auto& editor = page.Add<TextBox>("abc אבג XYZ / العربية");
    editor.SetAccessibleName("Mixed-direction example");
    auto& rtl = page.Add<TextBox>("العربية English עברית");
    rtl.AddClass("text-rtl");
    rtl.SetAccessibleName("Right-to-left paragraph");
    auto& wrapped =
        page.Add<Label>("אבגדה English العربية — mixed-direction help text wraps without changing the original "
                        "paragraph direction. Resize the window to inspect the shared measurement and drawing.");
    wrapped.AddClass("text-wrap");
    auto& actions = page.Add<Widget>();
    actions.AddClass("text-actions");
    const WidgetRef target(&editor);
    m_Connections.emplace_back(actions.Add<Button>("Select mixed text")
                                   .Clicked()
                                   .Connect(
                                       [target]
                                       {
                                           if (auto* field = dynamic_cast<TextBox*>(target.Get()))
                                           {
                                               field->GetContext()->SetFocus(field);
                                               field->SetSelection(1, 6);
                                           }
                                       }));
    auto& status =
        page.Add<Label>("The composition, clipboard and undo APIs are shared with ordinary TextBox controls.");
    status.AddClass("text-note");
}

void TextGallery::Verify(const std::filesystem::path& assets, const std::filesystem::path& output)
{
    WindowOptions options;
    options.m_Width = 720;
    options.m_Height = 420;
    options.m_Headless = m_Window.IsHeadless();
    AddFonts(options, assets, assets / "NotoSans.ttf");
    Window probe(options, assets / "NotoSans.ttf");
    auto& ui = probe.Ui();
    auto& gpu = probe.Graphics();
    auto& font = ui.GetFont();
    std::string error;
    Check(
        ui.SetStyleSheet(
            "window { padding: 16px; gap: 12px; background: #101827; } label { font-size: 24px; height: auto; color: "
            "white; } textbox { height: 76px; font-size: 24px; background: #142030; color: white; } textbox::selection "
            "{ background: #20a0f0; color: white; } textbox::preedit { background: #ffb050; height: 2px; }",
            error),
        "probe CSS parses");
    ui.Root().Add<Label>("GPU / العربية / עברית / नमस्ते");
    auto& editor = ui.Root().Add<TextBox>("abc אבג XYZ");
    auto& note =
        ui.Root().Add<Label>("אבגדה English العربية — Unicode wrapping keeps paragraph direction across lines.");
    note.SetWordWrap(true);
    ui.Prepare(720, 420);
    ui.SetFocus(&editor);
    editor.SetSelection(1, 6);
    probe.RenderFrame(1);
    const auto rectangles = editor.GetTextRangeRectangles(1, 6);
    Check(rectangles.size() == 2, "actual editor exposes two painted selection fragments");
    for (const auto& rectangle : rectangles)
    {
        const auto pixel = gpu.ReadPixel({rectangle.m_X + rectangle.m_Width * 0.5f, editor.GetContentBounds().m_Y + 2});
        Check(pixel.m_B > 0.85f && pixel.m_G > 0.5f && pixel.m_R < 0.2f,
              "each disjoint visual selection fragment reaches the framebuffer");
    }
    const auto retained = Pixels();
    gpu.SetPartialRedrawEnabled(false);
    probe.RenderFrame(1);
    Check(Pixels() == retained, "mixed-direction retained rendering equals the full redraw reference");
    gpu.SavePpm(output / "text-foundation-0-17.ppm");
    editor.SetSelection(4, 4);
    probe.RenderFrame(1);
    const auto before = Pixels();
    const auto state = ui.GetTextInputState();
    Check(ui.DispatchComposition({TextCompositionKind::Preedit, "العربية", 6, state.m_Client, state.m_Epoch}),
          "mixed-script preedit uses the shared editor protocol");
    probe.RenderFrame(1);
    Check(Pixels() != before && editor.GetText() == "abc אבג XYZ",
          "GPU preedit changes presentation while canonical mixed text remains unchanged");
    Check(ui.DispatchComposition({TextCompositionKind::Cancel, {}, 0, state.m_Client, state.m_Epoch}),
          "mixed-script preedit cancels");
    probe.RenderFrame(1);
    Check(Pixels() == before, "composition cancellation restores exact original mixed-text pixels");
    Style regularStyle;
    regularStyle.m_FontSize = 84;
    auto boldStyle = regularStyle;
    boldStyle.m_FontWeight = 700;
    const auto regular = font.Shape("O", regularStyle);
    auto heavy = font.Shape("O", boldStyle);
    Check(regular.GetFontIdentity(0) != heavy.GetFontIdentity(0), "regular and bold source identities are distinct");
    heavy.m_Glyphs = regular.m_Glyphs;
    const Rect clip{0, 0, 720, 420};
    const auto draw = [&](const TextLayout& layout)
    {
        gpu.BeginFrame(720, 420, 720, 420, {0, 0, 0, 1});
        gpu.DrawText(layout, {40, 130}, {1, 1, 1, 1}, clip);
        gpu.EndFrame();
        return Pixels();
    };
    const auto regularPixels = draw(regular);
    const auto boldPixels = draw(heavy);
    Check(regularPixels != boldPixels,
          "identical glyph numbers from two actual font instances use different GPU outlines");
    gpu.ClearFontCache();
    Check(draw(heavy) == boldPixels,
          "font cache retirement invalidates retained glyph addresses and redraws identical pixels");
    gpu.SetFontCacheLimit(1024);
    gpu.BeginFrame(720, 420, 720, 420, {0, 0, 0, 1});
    bool rejected = false;
    try
    {
        gpu.DrawText(font.Shape("ABCDEFGHIJKLMNOPQRSTUVWXYZ", 48), {10, 100}, {1, 1, 1, 1}, clip);
    }
    catch (const std::length_error&)
    {
        rejected = true;
    }
    gpu.EndFrame();
    Check(rejected && gpu.GetStats().m_FontBytes <= 1024, "GPU cache admission enforces its configured byte budget");
    gpu.SetFontCacheLimit(32 * 1024 * 1024);
    std::ofstream report(output / "text-foundation-0-17.txt");
    report << "F10a actual GPU text checks: PASS\n"
           << "Mixed scripts and fallback; disjoint bidi selection pixels; retained/full redraw equality;\n"
           << "preedit/cancel equality; actual font/glyph cache identities; budget admission and cache retirement.\n"
           << "Driver: " << gpu.GetDriverDescription() << '\n';
}
} // namespace EStudioUI
