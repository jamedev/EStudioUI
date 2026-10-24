#include "FontResource.h"
#include <EStudioUI/TextBuffer.h>

#include <SheenBidi/SheenBidi.h>
#include <hb-ot.h>
#include <linebreak.h>
#include <wordbreak.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <mutex>
#include <stdexcept>
#include <tuple>

namespace EStudioUI::Detail
{
namespace
{
using Algorithm = std::unique_ptr<std::remove_pointer_t<SBAlgorithmRef>, decltype(&SBAlgorithmRelease)>;
using Paragraph = std::unique_ptr<std::remove_pointer_t<SBParagraphRef>, decltype(&SBParagraphRelease)>;
using Line = std::unique_ptr<std::remove_pointer_t<SBLineRef>, decltype(&SBLineRelease)>;
using Scripts = std::unique_ptr<std::remove_pointer_t<SBScriptLocatorRef>, decltype(&SBScriptLocatorRelease)>;

struct Segment
{
    std::size_t m_First = 0;
    std::size_t m_Last = 0;
    hb_script_t m_Script = HB_SCRIPT_COMMON;
    std::shared_ptr<FontResource> m_Font;
};

void IndexCarets(TextLayout& layout)
{
    std::sort(layout.m_Spans.begin(), layout.m_Spans.end(),
              [](const auto& a, const auto& b) { return a.m_First < b.m_First; });
    layout.m_Carets.assign(layout.m_Offsets.size(), 0);
    for (const auto& span : layout.m_Spans)
    {
        const float start = span.m_RightToLeft ? span.m_Right : span.m_Left;
        const float end = span.m_RightToLeft ? span.m_Left : span.m_Right;
        layout.m_VisualCarets.push_back({{span.m_First, false}, start});
        layout.m_VisualCarets.push_back({{span.m_Last, true}, end});
    }
    if (layout.m_VisualCarets.empty())
        layout.m_VisualCarets.push_back({{layout.m_Offsets.front(), false}, 0});
    std::sort(layout.m_VisualCarets.begin(), layout.m_VisualCarets.end(),
              [](const auto& a, const auto& b)
              {
                  return std::tuple{a.m_X, a.m_Caret.m_Offset, a.m_Caret.m_Trailing} <
                         std::tuple{b.m_X, b.m_Caret.m_Offset, b.m_Caret.m_Trailing};
              });
    layout.m_VisualCarets.erase(std::unique(layout.m_VisualCarets.begin(), layout.m_VisualCarets.end(),
                                            [](const auto& a, const auto& b)
                                            { return a.m_X == b.m_X && a.m_Caret.m_Offset == b.m_Caret.m_Offset; }),
                                layout.m_VisualCarets.end());
    for (const auto& span : layout.m_Spans)
    {
        const auto index =
            static_cast<std::size_t>(std::lower_bound(layout.m_Offsets.begin(), layout.m_Offsets.end(), span.m_First) -
                                     layout.m_Offsets.begin());
        layout.m_Carets[index] = span.m_RightToLeft ? span.m_Right : span.m_Left;
        if (index + 1 < layout.m_Carets.size())
            layout.m_Carets[index + 1] = span.m_RightToLeft ? span.m_Left : span.m_Right;
    }
}

std::uint32_t FontSlot(TextLayout& layout, const std::shared_ptr<FontResource>& font)
{
    const auto found = std::find(layout.m_Fonts.begin(), layout.m_Fonts.end(), font);
    if (found != layout.m_Fonts.end())
        return static_cast<std::uint32_t>(found - layout.m_Fonts.begin());
    const auto slot = static_cast<std::uint32_t>(layout.m_Fonts.size());
    layout.m_Fonts.push_back(font);
    const float scale = layout.m_FontSize / static_cast<float>(font->m_Face->units_per_EM);
    layout.m_Ascender = std::max(layout.m_Ascender, static_cast<float>(font->m_Face->ascender) * scale);
    layout.m_Descender = std::max(layout.m_Descender, -static_cast<float>(font->m_Face->descender) * scale);
    return slot;
}

void ShapeSegment(TextLayout& layout, std::string_view line, std::size_t lineStart, const Segment& segment, bool rtl,
                  bool editable, const std::vector<std::size_t>& boundaries)
{
    const auto& font = segment.m_Font;
    const float scale = layout.m_FontSize / static_cast<float>(font->m_Face->units_per_EM);
    const auto slot = FontSlot(layout, font);
    std::unique_ptr<hb_buffer_t, decltype(&hb_buffer_destroy)> buffer(hb_buffer_create(), hb_buffer_destroy);
    hb_buffer_add_utf8(buffer.get(), line.data(), static_cast<int>(line.size()),
                       static_cast<unsigned>(segment.m_First - lineStart),
                       static_cast<int>(segment.m_Last - segment.m_First));
    hb_buffer_set_direction(buffer.get(), rtl ? HB_DIRECTION_RTL : HB_DIRECTION_LTR);
    hb_buffer_set_script(buffer.get(), segment.m_Script);
    hb_buffer_set_language(buffer.get(),
                           hb_language_from_string(layout.m_Selection.m_Language.data(),
                                                   static_cast<int>(layout.m_Selection.m_Language.size())));
    hb_buffer_set_cluster_level(buffer.get(), HB_BUFFER_CLUSTER_LEVEL_MONOTONE_GRAPHEMES);
    hb_feature_t features[2]{};
    hb_feature_from_string("liga=0", -1, &features[0]);
    hb_feature_from_string("clig=0", -1, &features[1]);
    hb_shape(font->m_Font, buffer.get(), editable ? features : nullptr, editable ? 2u : 0u);
    if (!hb_buffer_allocation_successful(buffer.get()))
        throw std::bad_alloc();
    unsigned count = 0;
    const auto* info = hb_buffer_get_glyph_infos(buffer.get(), &count);
    const auto* positions = hb_buffer_get_glyph_positions(buffer.get(), &count);

    struct Cluster
    {
        float m_Left = 0;
        float m_Right = 0;
        unsigned m_Glyph = 0;
        unsigned m_Count = 0;
    };

    std::map<std::size_t, Cluster> clusters;
    for (unsigned i = 0; i < count; ++i)
    {
        const auto offset = static_cast<std::size_t>(info[i].cluster) + lineStart;
        const float before = layout.m_Width;
        const float advance = static_cast<float>(positions[i].x_advance) * scale;
        layout.m_Glyphs.push_back({info[i].codepoint, before + static_cast<float>(positions[i].x_offset) * scale,
                                   static_cast<float>(positions[i].y_offset) * scale, slot});
        layout.m_Width += advance;
        layout.m_MissingGlyphs += info[i].codepoint == 0 ? 1u : 0u;
        auto [entry, inserted] = clusters.try_emplace(offset, Cluster{before, layout.m_Width, info[i].codepoint, 0});
        if (!inserted)
        {
            entry->second.m_Left = std::min(entry->second.m_Left, before);
            entry->second.m_Right = std::max(entry->second.m_Right, layout.m_Width);
        }
        ++entry->second.m_Count;
    }
    if (clusters.empty())
        clusters.emplace(segment.m_First, Cluster{layout.m_Width, layout.m_Width, 0, 0});
    for (auto cluster = clusters.begin(); cluster != clusters.end(); ++cluster)
    {
        const auto next = std::next(cluster);
        const auto first = std::lower_bound(boundaries.begin(), boundaries.end(), cluster->first);
        const auto last = std::lower_bound(boundaries.begin(), boundaries.end(),
                                           next == clusters.end() ? segment.m_Last : next->first);
        const auto parts = static_cast<std::size_t>(last - first);
        if (!parts)
            continue;
        const float left = cluster->second.m_Left, right = cluster->second.m_Right;
        std::vector<float> edges(parts + 1);
        for (std::size_t i = 0; i <= parts; ++i)
            edges[i] = left + (right - left) * static_cast<float>(i) / static_cast<float>(parts);
        if (parts > 1 && cluster->second.m_Count == 1)
        {
            std::vector<hb_position_t> carets(parts - 1);
            auto available = static_cast<unsigned>(carets.size());
            hb_ot_layout_get_ligature_carets(font->m_Font, rtl ? HB_DIRECTION_RTL : HB_DIRECTION_LTR,
                                             cluster->second.m_Glyph, 0, &available, carets.data());
            if (available == carets.size())
            {
                std::sort(carets.begin(), carets.end());
                for (std::size_t i = 0; i < carets.size(); ++i)
                    edges[i + 1] = std::clamp(left + std::abs(static_cast<float>(carets[i])) * scale, left, right);
                std::sort(edges.begin(), edges.end());
            }
        }
        for (std::size_t i = 0; i < parts; ++i)
        {
            const auto visual = rtl ? parts - i - 1 : i;
            layout.m_Spans.push_back({first[static_cast<std::ptrdiff_t>(i)], first[static_cast<std::ptrdiff_t>(i + 1)],
                                      edges[visual], edges[visual + 1], rtl});
        }
    }
}

TextLayout ShapeLine(std::string_view text, const FontSelection& selection, bool editable, SBParagraphRef paragraph,
                     const std::vector<Segment>& segments, const std::vector<std::size_t>& boundaries,
                     const std::vector<std::size_t>& words, std::size_t first, std::size_t last)
{
    TextLayout result;
    result.m_Selection = selection;
    result.m_FontSize = selection.m_Size;
    result.m_RightToLeft = (SBParagraphGetBaseLevel(paragraph) & 1u) != 0;
    result.m_Offsets.assign(std::lower_bound(boundaries.begin(), boundaries.end(), first),
                            std::upper_bound(boundaries.begin(), boundaries.end(), last));
    result.m_WordOffsets.assign(std::lower_bound(words.begin(), words.end(), first),
                                std::upper_bound(words.begin(), words.end(), last));
    Line line(SBParagraphCreateLine(paragraph, first, last - first), SBLineRelease);
    if (!line)
        throw std::runtime_error("Cannot resolve the bidirectional text line.");
    if (SBLineGetRunCount(line.get()) > 4096)
        throw std::length_error("One control line is limited to 4,096 bidirectional runs.");
    const auto* runs = SBLineGetRunsPtr(line.get());
    for (SBUInteger i = 0; i < SBLineGetRunCount(line.get()); ++i)
    {
        const auto& run = runs[i];
        std::vector<Segment> pieces;
        const auto begin =
            std::lower_bound(segments.begin(), segments.end(), run.offset,
                             [](const Segment& segment, std::size_t offset) { return segment.m_Last <= offset; });
        for (auto at = begin; at != segments.end() && at->m_First < run.offset + run.length; ++at)
        {
            const auto& segment = *at;
            auto piece = segment;
            piece.m_First = std::max<std::size_t>(piece.m_First, run.offset);
            piece.m_Last = std::min<std::size_t>(piece.m_Last, run.offset + run.length);
            pieces.push_back(std::move(piece));
        }
        const bool rtl = (run.level & 1u) != 0;
        if (rtl)
            std::reverse(pieces.begin(), pieces.end());
        for (const auto& piece : pieces)
            ShapeSegment(result, text.substr(first, last - first), first, piece, rtl, editable, boundaries);
    }
    if (result.m_Fonts.empty())
        FontSlot(result, segments.front().m_Font);
    IndexCarets(result);
    return result;
}
} // namespace

std::vector<TextLayout> ShapeControlText(std::string_view text, const FontSelection& selection, bool editable,
                                         const std::vector<std::shared_ptr<FontResource>>& fonts,
                                         std::optional<float> width)
{
    ValidateControlText(text);
    selection.Validate();
    if (width && (!std::isfinite(*width) || *width < 0))
    {
        throw std::invalid_argument("Wrapping width must be finite and nonnegative.");
    }
    if (text.empty())
    {
        TextLayout empty;
        empty.m_Selection = selection;
        empty.m_FontSize = selection.m_Size;
        empty.m_Offsets = {0};
        empty.m_RightToLeft = selection.m_Direction == "rtl";
        FontSlot(empty, fonts.front());
        IndexCarets(empty);
        return {std::move(empty)};
    }
    static std::once_flag initialized;
    std::call_once(initialized,
                   []
                   {
                       init_linebreak();
                       init_wordbreak();
                   });
    const auto boundaries = TextBuffer::Graphemes(text);
    std::vector<char> wordBreaks(text.size()), lineBreaks(text.size());
    set_wordbreaks_utf8(reinterpret_cast<const utf8_t*>(text.data()), text.size(), selection.m_Language.c_str(),
                        wordBreaks.data());
    set_linebreaks_utf8(reinterpret_cast<const utf8_t*>(text.data()), text.size(), selection.m_Language.c_str(),
                        lineBreaks.data());
    std::vector<std::size_t> words{0};
    for (const auto offset : boundaries)
        if (offset && wordBreaks[offset - 1] == WORDBREAK_BREAK)
            words.push_back(offset);
    if (words.back() != text.size())
        words.push_back(text.size());
    const SBCodepointSequence sequence{SBStringEncodingUTF8, text.data(), text.size()};
    Algorithm algorithm(SBAlgorithmCreate(&sequence), SBAlgorithmRelease);
    if (!algorithm)
        throw std::runtime_error("Cannot allocate Unicode bidirectional analysis.");
    const auto base = static_cast<SBLevel>(selection.m_Direction == "rtl"   ? 1
                                           : selection.m_Direction == "ltr" ? 0
                                                                            : SBLevelDefaultLTR);
    Paragraph paragraph(SBAlgorithmCreateParagraph(algorithm.get(), 0, text.size(), base), SBParagraphRelease);
    if (!paragraph || SBParagraphGetLength(paragraph.get()) != text.size())
        throw std::invalid_argument("Control shaping accepts one Unicode paragraph.");
    Scripts scripts(SBScriptLocatorCreate(), SBScriptLocatorRelease);
    if (!scripts)
        throw std::runtime_error("Cannot allocate script analysis.");
    SBScriptLocatorLoadCodepoints(scripts.get(), &sequence);
    std::vector<std::tuple<std::size_t, std::size_t, hb_script_t>> scriptRuns;
    while (SBScriptLocatorMoveNext(scripts.get()))
    {
        const auto* agent = SBScriptLocatorGetAgent(scripts.get());
        scriptRuns.emplace_back(agent->offset, agent->offset + agent->length,
                                hb_script_from_iso15924_tag(SBScriptGetUnicodeTag(agent->script)));
    }
    std::vector<Segment> segments;
    std::size_t script = 0;
    for (std::size_t i = 0; i + 1 < boundaries.size(); ++i)
    {
        const auto first = boundaries[i], last = boundaries[i + 1];
        while (script + 1 < scriptRuns.size() && std::get<1>(scriptRuns[script]) <= first)
            ++script;
        const auto tag = scriptRuns.empty() ? HB_SCRIPT_COMMON : std::get<2>(scriptRuns[script]);
        auto font = fonts.front();
        for (const auto& candidate : fonts)
            if (candidate->Covers(text.substr(first, last - first)))
            {
                font = candidate;
                break;
            }
        if (!segments.empty() && segments.back().m_Font == font && segments.back().m_Script == tag)
            segments.back().m_Last = last;
        else
            segments.push_back({first, last, tag, std::move(font)});
    }
    if (segments.size() > 4096)
        throw std::length_error("One control paragraph is limited to 4,096 font/script segments.");
    auto natural = ShapeLine(text, selection, editable, paragraph.get(), segments, boundaries, words, 0, text.size());
    if (!width || natural.m_Width <= *width)
        return {std::move(natural)};
    std::vector<float> advances(boundaries.size(), 0);
    for (const auto& span : natural.m_Spans)
    {
        const auto at = static_cast<std::size_t>(std::lower_bound(boundaries.begin(), boundaries.end(), span.m_First) -
                                                 boundaries.begin());
        advances[at + 1] = span.m_Right - span.m_Left;
    }
    std::vector<TextLayout> lines;
    for (std::size_t first = 0; first + 1 < boundaries.size();)
    {
        std::size_t end = first + 1;
        float used = advances[end];
        while (end + 1 < boundaries.size() && used + advances[end + 1] <= *width + 0.001f)
            used += advances[++end];
        if (end + 1 < boundaries.size())
            for (auto at = end; at > first; --at)
                if (lineBreaks[boundaries[at] - 1] == LINEBREAK_ALLOWBREAK ||
                    lineBreaks[boundaries[at] - 1] == LINEBREAK_MUSTBREAK)
                {
                    end = at;
                    break;
                }
        auto visibleEnd = end;
        while (visibleEnd > first + 1 && text[boundaries[visibleEnd - 1]] == ' ')
            --visibleEnd;
        auto line = ShapeLine(text, selection, editable, paragraph.get(), segments, boundaries, words,
                              boundaries[first], boundaries[visibleEnd]);
        while (line.m_Width > *width + 0.01f && visibleEnd > first + 1)
        {
            end = --visibleEnd;
            line = ShapeLine(text, selection, editable, paragraph.get(), segments, boundaries, words, boundaries[first],
                             boundaries[visibleEnd]);
        }
        if (lines.size() >= 4096)
            throw std::length_error("One wrapped control paragraph is limited to 4,096 lines.");
        lines.push_back(std::move(line));
        first = end;
        while (first + 1 < boundaries.size() && text[boundaries[first]] == ' ')
            ++first;
    }
    return lines;
}
} // namespace EStudioUI::Detail
