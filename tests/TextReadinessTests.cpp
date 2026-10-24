#include <EStudioUI/Bindings.h>
#include <EStudioUI/FontEditor.h>
#include <EStudioUI/FontPicker.h>
#include <EStudioUI/Label.h>
#include <EStudioUI/UiContext.h>
#include "../benchmarks/TextCorpus.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace EStudioUI;

namespace
{
int s_Checks = 0;

void Check(bool value, const char* message)
{
    ++s_Checks;
    if (!value)
    {
        throw std::runtime_error(message);
    }
}

template <class F> void Throws(F action, const char* message)
{
    bool rejected = false;
    try
    {
        action();
    }
    catch (const std::exception&)
    {
        rejected = true;
    }
    Check(rejected, message);
}

void FontContracts()
{
    FontSelection selection;
    selection.Validate();
    Check(FontSelection::IsValidFamilyList("'Noto Sans', sans-serif") && FontSelection::IsValidFamilyList("日本語"),
          "UTF-8 quoted lists share one font admission contract");
    for (const std::string family :
         {std::string{}, std::string("'unfinished"), std::string("A,,B"), std::string("A\nB"),
          std::string("\xff"), std::string(1025, 'x'), std::string("A\0B", 3)})
    {
        selection.m_Family = family;
        Throws([&] { selection.Validate(); }, "Malformed or oversized family lists reject before font resolution");
    }
    selection = {};
    for (const std::string language :
         {std::string{}, std::string("en_US"), std::string("en us"), std::string(64, 'a'), std::string("日本語")})
    {
        selection.m_Language = language;
        Throws([&] { selection.Validate(); }, "Invalid language syntax and limits share the same boundary");
    }
    selection = {};
    selection.m_Language = "sr-Latn-RS";
    selection.Validate();
    selection.m_Size = std::numeric_limits<float>::quiet_NaN();
    Throws([&] { selection.Validate(); }, "Nonfinite font size rejects");
    FontFace font(ESTUDIOUI_FONT_PATH);
    UiContext ui(font);
    auto& editor = ui.Root().Add<FontEditor>();
    auto& picker = ui.Root().Add<FontPicker>();
    State<FontSelection> state(ui.States(), FontSelection{});
    auto binding = BindFont(editor, state);
    auto second = BindFont(picker, state);
    selection = {};
    selection.m_Family = "'Noto Sans', sans-serif";
    selection.m_Direction = "rtl";
    selection.m_Language = "he";
    selection.m_Size = 22;
    selection.m_Style = "oblique";
    state.Set(selection);
    ui.Prepare(800, 600);
    const auto preview = editor.GetPreview().GetStyle();
    Check(editor.GetFont() == selection && picker.GetFont() == selection && preview.m_TextDirection == "rtl" &&
              preview.m_Language == "he" && preview.m_FontStyle == "oblique",
          "Complete font selection reaches both bound controls and preview shaping fields");
    const auto before = font.GetStats();
    const auto previous = state.Get();
    for (const auto bad : {"", "invalid_language", "language with spaces"})
    {
        auto candidate = previous;
        candidate.m_Language = bad;
        Throws([&] { state.Set(candidate); }, "Invalid bound typography rejects atomically before preview mutation");
        Check(state.Get() == previous && editor.GetFont() == previous && picker.GetFont() == previous,
              "Rejected font candidates preserve all bound views");
    }
    auto bad = previous;
    bad.m_Family = std::string(1025, 'x');
    Throws([&] { editor.SetFont(bad); }, "FontEditor cannot accept more family bytes than the shaper");
    Check(font.GetStats().m_Shapes == before.m_Shapes, "Rejected assignments perform no shaping");
    Style style;
    style.m_Language = "bad_language";
    Throws([&] { font.Shape("text", style); }, "Shaping rejects the same invalid language");
    Throws([&] { editor.GetPreview().SetStyleProperty(StyleProperty::Language, std::string("bad_language")); },
           "Explicit CSS property assignments share validation");
    editor.GetFamilyEditor().GetEditor().SetText("'unfinished");
    Check(editor.GetFamilyEditor().GetEditor().GetValidationState() == ValidationState::Intermediate &&
              editor.GetFont() == previous,
          "An incomplete family draft stays editable without changing the committed font");
}

void CorpusContracts()
{
    const auto cases = Testing::MakeTextCorpus();
    Check(cases.size() == 4 && cases.front().m_Paragraphs.size() == 100000,
          "Corpus has a deterministic large paragraph inventory");
    for (const auto& fixture : cases)
    {
        std::size_t bytes = 0;
        bool paragraphs = true;
        for (const auto& text : fixture.m_Paragraphs)
        {
            bytes += text.size();
            paragraphs &= text.find('\n') == std::string::npos;
        }
        Check(paragraphs, "Fixtures preserve single-paragraph shaping boundaries");
        Check(bytes == fixture.m_Bytes && fixture.m_VisibleCount <= fixture.m_Paragraphs.size(),
              "Corpus metadata owns exact size and viewport bounds");
    }
    FontLimits limits;
    limits.m_MaxShapeBytes = 262144;
    limits.m_MaxShapeEntries = 16;
    FontFace font(ESTUDIOUI_FONT_PATH, limits);
    const auto& fixture = cases.front();
    Style style;
    const auto first = font.GetStats();
    for (std::size_t row = 0; row < 8; ++row)
    {
        (void)font.Shape(fixture.m_Paragraphs[row], style, true);
    }
    const auto shaped = font.GetStats();
    for (std::size_t row = 0; row < 8; ++row)
    {
        (void)font.Shape(fixture.m_Paragraphs[row], style, true);
    }
    Check(font.GetStats().m_Shapes == shaped.m_Shapes && font.GetStats().m_CacheHits >= shaped.m_CacheHits + 8,
          "Repeated visible paragraphs reuse exact shaping independently of total corpus size");
    Check(shaped.m_Shapes - first.m_Shapes == 8, "Only requested paragraphs are shaped");
    for (std::size_t row = 8; row < 256; ++row)
    {
        (void)font.Shape(fixture.m_Paragraphs[row], style, true);
    }
    Check(font.GetStats().m_ShapeBytes <= limits.m_MaxShapeBytes &&
              font.GetStats().m_ShapeEntries <= limits.m_MaxShapeEntries,
          "Scrolling distinct paragraphs keeps shared shape storage bounded");
}
} // namespace

int main()
{
    try
    {
        FontContracts();
        CorpusContracts();
        std::cout << "Text readiness: " << s_Checks << " checks passed\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
