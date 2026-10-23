#include <EStudioUI/FontFace.h>
#include <EStudioUI/Label.h>
#include <EStudioUI/SemanticTree.h>
#include <EStudioUI/TextBox.h>
#include <EStudioUI/TextBuffer.h>
#include <EStudioUI/UiContext.h>

#include <hb.h>
#include <hb-ot.h>

#include <algorithm>
#include <fstream>
#include <iterator>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace
{
using namespace EStudioUI;
int s_Checks = 0;

void Check(bool condition, const char* message)
{
    ++s_Checks;
    if (!condition)
        throw std::runtime_error(message);
}

template <typename Function> void Throws(Function function, const char* message)
{
    bool rejected = false;
    try
    {
        function();
    }
    catch (const std::exception&)
    {
        rejected = true;
    }
    Check(rejected, message);
}

std::filesystem::path FontPath(const char* name)
{
    return std::filesystem::path(ESTUDIOUI_TEST_FONT_DIR) / name;
}

std::vector<std::uint32_t> ArabicReference(std::string_view text)
{
    std::ifstream input(FontPath("NotoSansArabic.ttf"), std::ios::binary);
    const std::vector<char> bytes(std::istreambuf_iterator<char>(input), {});
    std::unique_ptr<hb_blob_t, decltype(&hb_blob_destroy)> blob(
        hb_blob_create(bytes.data(), static_cast<unsigned>(bytes.size()), HB_MEMORY_MODE_READONLY, nullptr, nullptr),
        hb_blob_destroy);
    std::unique_ptr<hb_face_t, decltype(&hb_face_destroy)> face(hb_face_create(blob.get(), 0), hb_face_destroy);
    std::unique_ptr<hb_font_t, decltype(&hb_font_destroy)> font(hb_font_create(face.get()), hb_font_destroy);
    hb_ot_font_set_funcs(font.get());
    const auto units = static_cast<int>(hb_face_get_upem(face.get()));
    hb_font_set_scale(font.get(), units, units);
    std::unique_ptr<hb_buffer_t, decltype(&hb_buffer_destroy)> buffer(hb_buffer_create(), hb_buffer_destroy);
    hb_buffer_add_utf8(buffer.get(), text.data(), static_cast<int>(text.size()), 0, static_cast<int>(text.size()));
    hb_buffer_set_direction(buffer.get(), HB_DIRECTION_RTL);
    hb_buffer_set_script(buffer.get(), HB_SCRIPT_ARABIC);
    hb_buffer_set_language(buffer.get(), hb_language_from_string("und", -1));
    hb_feature_t features[2]{};
    hb_feature_from_string("liga=0", -1, &features[0]);
    hb_feature_from_string("clig=0", -1, &features[1]);
    hb_shape(font.get(), buffer.get(), features, 2);
    unsigned count = 0;
    const auto* info = hb_buffer_get_glyph_infos(buffer.get(), &count);
    std::vector<std::uint32_t> result;
    for (unsigned i = 0; i < count; ++i)
        result.push_back(info[i].codepoint);
    return result;
}

void Layouts()
{
    FontFace fonts(ESTUDIOUI_FONT_PATH);
    const auto arabic = fonts.AddFont({FontPath("NotoSansArabic.ttf")});
    const auto hebrew = fonts.AddFont({FontPath("NotoSansHebrew.ttf")});
    const auto devanagari = fonts.AddFont({FontPath("NotoSansDevanagari.ttf")});
    const auto bold = fonts.AddFont({ESTUDIOUI_FONT_PATH, {}, 700});
    const auto italic = fonts.AddFont({FontPath("NotoSansItalic.ttf"), {}, 400, true});

    Style style;
    style.m_FontSize = 20;
    const std::string caption = "GPU العربية עברית नमस्ते";
    const auto mixed = fonts.Shape(caption, style);
    Check(!mixed.m_MissingGlyphs && mixed.m_Fonts.size() >= 3,
          "Mixed-script caption uses complete grapheme fallback across actual registered faces");
    Check(mixed.m_Offsets == TextBuffer::Graphemes(caption), "Fallback preserves canonical grapheme byte boundaries");
    const auto rtl = fonts.Shape("אבגד", style, true);
    Check(rtl.m_RightToLeft && !rtl.m_MissingGlyphs && rtl.GetFontIdentity(0) == hebrew,
          "Automatic paragraph direction and actual Hebrew font resolve together");
    Check(std::abs(rtl.CaretPosition(0) - rtl.m_Width) < 0.01f && std::abs(rtl.CaretPosition(8)) < 0.01f,
          "Pure RTL logical endpoints map to opposite visual edges");
    for (const auto offset : rtl.m_Offsets)
        Check(rtl.HitTest(rtl.CaretPosition(offset)) == offset,
              "RTL caret hit test returns the original logical grapheme");
    auto caret = rtl.EdgeCaret(false);
    Check(caret.m_Offset == 0, "RTL Home chooses the right paragraph edge");
    for (int i = 0; i < 4; ++i)
    {
        const auto before = rtl.CaretPosition(caret);
        caret = rtl.MoveCaret(caret, -1);
        Check(rtl.CaretPosition(caret) < before, "Visual Left advances toward the next RTL grapheme");
    }
    Check(caret.m_Offset == 8 && rtl.MoveCaret(caret, -1) == caret, "Visual navigation stops at the physical edge");
    const auto selection = fonts.Shape("abc אבג XYZ", style, true);
    for (const auto& text : {"abc אבג XYZ", "abc אבג XYZ / العربية", "العربية English עברית"})
    {
        const auto layout = fonts.Shape(text, style, true);
        for (const auto& span : layout.m_Spans)
        {
            if (span.m_Right - span.m_Left < 0.1f)
                continue;
            for (const bool right : {false, true})
            {
                const float x = span.m_Left + (span.m_Right - span.m_Left) * (right ? 0.9f : 0.1f);
                const auto hit = layout.HitTestCaret(x);
                const auto expected = right != span.m_RightToLeft ? span.m_Last : span.m_First;
                Check(hit.m_Offset == expected,
                      "Pointer hits choose the edge of the pointed grapheme, including each side of bidi boundaries");
                Check(std::abs(layout.CaretPosition(hit) - (right ? span.m_Right : span.m_Left)) < 0.01f,
                      "Pointer caret affinity stays at the nearest visual edge");
            }
        }
    }
    Check(selection.SelectionRectangles(1, 6).size() == 2,
          "A contiguous logical selection can produce disjoint visual rectangles");
    Check(std::abs(selection.CaretPosition(TextCaret{4, false}) - selection.CaretPosition(TextCaret{4, true})) > 5,
          "A bidi boundary retains both visual caret affinities");
    caret = selection.m_VisualCarets.front().m_Caret;
    for (std::size_t i = 0; i < selection.m_VisualCarets.size(); ++i)
    {
        Check(std::binary_search(selection.m_Offsets.begin(), selection.m_Offsets.end(), caret.m_Offset),
              "Mixed visual navigation never splits a UTF-8 grapheme");
        const auto next = selection.MoveCaret(caret, 1);
        Check(selection.CaretPosition(next) + 0.01f >= selection.CaretPosition(caret),
              "Visual Right is monotone across mixed runs");
        caret = next;
    }
    const auto joined = fonts.Shape("سلام", style, true);
    const auto reference = ArabicReference("سلام");
    Check(joined.GetFontIdentity(0) == arabic && !joined.m_MissingGlyphs &&
              joined.m_Glyphs.size() == reference.size() &&
              std::equal(joined.m_Glyphs.begin(), joined.m_Glyphs.end(), reference.begin(),
                         [](const auto& glyph, auto id) { return glyph.m_Id == id; }),
          "Editable Arabic matches an independent native HarfBuzz run with explicit Arabic script and RTL direction");
    Style left = style, right = style;
    left.m_TextDirection = "ltr";
    right.m_TextDirection = "rtl";
    Check(fonts.Shape("(", right).m_Glyphs.front().m_Id == fonts.Shape(")", left).m_Glyphs.front().m_Id,
          "RTL punctuation receives Unicode mirrored glyphs");
    const auto isolated = fonts.Shape("A\u2067אבג\u2069 Z", style);
    Check(!isolated.m_MissingGlyphs && isolated.m_Offsets == TextBuffer::Graphemes("A\u2067אבג\u2069 Z"),
          "Directional isolates are analyzed without losing logical boundaries");
    const auto missing = fonts.Shape("\U0001F9D1", style);
    Check(missing.m_MissingGlyphs > 0, "Unregistered coverage is reported rather than claimed as successful fallback");
    const auto before = fonts.GetStats();
    (void)fonts.Shape(caption, style);
    Check(fonts.GetStats().m_Shapes == before.m_Shapes && fonts.GetStats().m_CacheHits == before.m_CacheHits + 1,
          "Exact shared shaping requests reuse bounded cached results");
    style.m_FontWeight = 700;
    const auto heavier = fonts.Shape("Weight", style);
    Check(heavier.GetFontIdentity(0) == bold, "CSS weight selects an actual variable-weight font instance");
    style.m_FontStyle = "italic";
    style.m_FontWeight = 400;
    Check(fonts.Shape("Italic", style).GetFontIdentity(0) == italic, "CSS italic selects an actual italic font file");
    style.m_FontStyle = "normal";
    style.m_FontFamily = "Noto Sans Devanagari";
    Check(fonts.Shape("नमस्ते", style).GetFontIdentity(0) == devanagari,
          "Explicit Devanagari family selection uses its registered native face");
    style.m_FontFamily = "'Noto Sans Hebrew', sans-serif";
    Check(fonts.Shape("Family", style).GetFontIdentity(0) == hebrew,
          "Quoted ordered family names resolve before generic fallback");
    style = {};
    style.m_FontSize = 20;
    const auto lines = fonts.Wrap("אבגדה English Arabic العربية עוד מילים", style, 100);
    Check(lines.size() > 2, "Unicode wrapping splits mixed-script display text");
    for (const auto& line : lines)
        Check(line.m_RightToLeft && line.m_Width <= 100.01f && !line.m_MissingGlyphs,
              "Wrapped lines retain original paragraph levels and reshape their actual line context");
    Throws([&] { fonts.Shape(std::string(1, char(0xFF)), style); }, "Malformed public shaping input is diagnosed");
    Throws([&] { fonts.Shape("two\nparagraphs", style); },
           "Single-line shaping cannot silently drop a second paragraph");
    const auto capped = TextBuffer::Sanitize(std::string(1048575, 'a') + "e\u0301");
    Check(capped.size() == 1048575 && capped.back() == 'a',
          "The global UTF-8 input cap drops an entire straddling grapheme");
    Throws([&] { fonts.Wrap("text", style, -1); }, "Wrapping rejects invalid dimensions");
}

void BudgetsAndRetirement()
{
    FontLimits limits;
    limits.m_MaxFaces = 2;
    limits.m_MaxGeometryBytes = 4096;
    limits.m_MaxShapeBytes = 8192;
    limits.m_MaxShapeEntries = 4;
    FontFace fonts(ESTUDIOUI_FONT_PATH, limits);
    const auto alternate = fonts.AddFont({FontPath("NotoSansHebrew.ttf")});
    auto retained = fonts.Shape("אבג", 20);
    const auto revision = fonts.GetRevision();
    Throws([&] { fonts.AddFont({FontPath("NotoSansArabic.ttf")}); }, "Face count admission is explicit and atomic");
    Check(fonts.GetRevision() == revision && fonts.GetStats().m_RegisteredFaces == 2,
          "Rejected font admission preserves registry revision and registrations");
    Check(fonts.RemoveFont(alternate) && fonts.GetStats().m_LiveFaces == 2,
          "Retired resources remain live while an owned layout needs them");
    Throws([&] { fonts.AddFont({FontPath("NotoSansArabic.ttf")}); },
           "Live retired layouts count toward the same font budget");
    Check(!retained.GetGlyph(retained.m_Glyphs.front()).m_Curves.empty(),
          "Retained layout can still load its actual font outline after deregistration");
    retained = {};
    Check(fonts.GetStats().m_LiveFaces == 1, "Final layout retirement releases native font resources");
    fonts.AddFont({FontPath("NotoSansArabic.ttf")});
    for (int i = 0; i < 60; ++i)
        (void)fonts.Shape("cache entry " + std::to_string(i), 16);
    Check(fonts.GetStats().m_ShapeEntries <= 4 && fonts.GetStats().m_ShapeBytes <= 8192,
          "Shared text cache remains within entry and byte budgets under churn");
    const auto run = fonts.Shape("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz", 16);
    for (const auto& glyph : run.m_Glyphs)
    {
        (void)run.GetGlyph(glyph);
        Check(fonts.GetStats().m_GeometryBytes <= 4096, "Actual loaded outline storage respects the global LRU budget");
    }
    fonts.ClearCaches();
    Check(!fonts.GetStats().m_ShapeBytes && !fonts.GetStats().m_GeometryBytes,
          "Explicit cache retirement releases accounted storage");
    bool ownerRejected = false;
    std::thread worker(
        [&]
        {
            try
            {
                fonts.AddFont({ESTUDIOUI_FONT_PATH});
            }
            catch (const std::logic_error&)
            {
                ownerRejected = true;
            }
        });
    worker.join();
    Check(ownerRejected, "Wrong-thread font registration rejects before resource mutation");
}

class InspectFont : public Widget
{
  public:
    explicit InspectFont(FontFace& font) : m_Font(font)
    {
    }

    SemanticInfo DescribeSemantics() const override
    {
        m_Font.AddFont({ESTUDIOUI_FONT_PATH});
        return {};
    }

  private:
    FontFace& m_Font;
};

class MeasuringLabel : public Label
{
  public:
    using Label::Label;

    float IntrinsicWidth() const
    {
        return GetIntrinsicSize().m_X;
    }
};

void ControlsAndInvalidation()
{
    FontFace fonts(ESTUDIOUI_FONT_PATH);
    fonts.AddFont({FontPath("NotoSansHebrew.ttf")});
    UiContext ui(fonts);
    std::string error;
    Check(ui.SetStyleSheet("window { font-family: 'Special', sans-serif; font-weight: 700; language: he; direction: "
                           "auto; } label { height: auto; } textbox { width: 400px; height: 70px; font-size: 20px; }",
                           error),
          "Typography CSS parses and inherits through ordinary controls");
    auto& label = ui.Root().Add<MeasuringLabel>("Font resource changes");
    label.SetWidth(SizeRule::Content());
    auto& editor = ui.Root().Add<TextBox>("abc אבג XYZ");
    ui.Prepare(800, 300);
    const auto styles = ui.GetStats().m_StyleResolutions;
    const auto originalWidth = label.IntrinsicWidth();
    const auto variant = fonts.AddFont({ESTUDIOUI_FONT_PATH, "Special", 700});
    Check(ui.NeedsRepaint(), "Font registration wakes retained UI presentation without application cache plumbing");
    ui.Prepare(800, 300);
    Check(ui.GetStats().m_StyleResolutions == styles && std::abs(label.IntrinsicWidth() - originalWidth) > 0.1f,
          "Font registration refreshes measurement while retaining unrelated CSS caches");
    ui.SetFocus(&editor);
    const auto dragLayout = fonts.Shape(editor.GetText(), editor.GetStyle(), true);
    const auto content = editor.GetContentBounds();
    const Point anchor{content.m_X + 0.1f, content.m_Y + content.m_Height * 0.5f};
    ui.PointerDown(anchor);
    for (const auto& span : dragLayout.m_Spans)
        for (const bool right : {false, true})
        {
            const float x = span.m_Left + (span.m_Right - span.m_Left) * (right ? 0.9f : 0.1f);
            ui.PointerMove({content.m_X + x, anchor.m_Y});
            const auto expected = right != span.m_RightToLeft ? span.m_Last : span.m_First;
            Check(editor.GetSelectionAnchor() == 0 && editor.GetSelectionCaret() == expected &&
                      editor.GetSelectedText() == editor.GetText().substr(0, expected),
                  "A captured mixed-direction drag preserves its anchor and the exact logical clipboard range");
        }
    ui.PointerUp(anchor);
    editor.SetSelection(1, 6);
    Check(editor.GetTextRangeRectangles(1, 6).size() == 2,
          "TextBox exposes disjoint mixed-direction selection rectangles");
    ui.KeyEvent(Key::Left, true);
    Check(editor.GetSelectedText().empty(), "Visual movement collapses a mixed-direction selection");
    ui.KeyEvent(Key::Home, true);
    const auto start = ui.GetTextInputState().m_CaretBounds.m_X;
    ui.KeyEvent(Key::Right, true);
    Check(ui.GetTextInputState().m_CaretBounds.m_X >= start,
          "Editor caret and native composition geometry share visual navigation");
    auto& inspect = ui.Root().Add<InspectFont>(fonts);
    const auto count = fonts.GetStats().m_RegisteredFaces;
    Throws([&] { (void)ui.Semantics().GetNode(ui.Semantics().GetId(inspect)); },
           "Font registration respects semantic inspection purity");
    Check(fonts.GetStats().m_RegisteredFaces == count,
          "Rejected metadata mutation has no font registration side effect");
    Check(fonts.RemoveFont(variant), "Font removal invalidates dependent UI caches safely");
}
} // namespace

int main()
{
    try
    {
        Layouts();
        BudgetsAndRetirement();
        ControlsAndInvalidation();
        std::cout << "Font text: " << s_Checks << " checks passed.\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Font text FAIL / " << error.what() << '\n';
        return 1;
    }
}
