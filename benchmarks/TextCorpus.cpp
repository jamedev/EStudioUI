#include "TextCorpus.h"
#include <algorithm>
#include <stdexcept>

namespace EStudioUI::Testing
{
std::string MakeEditParagraph(std::size_t bytes)
{
    if (bytes > 1048576)
    {
        throw std::length_error("Text fixture paragraphs are limited to 1 MiB.");
    }
    const std::string seed = "Beam A: displacement=0.125 mm; stress=123.75 MPa; load case=42. ";
    std::string text;
    text.reserve(bytes);
    while (text.size() < bytes)
    {
        text.append(seed, 0, std::min(seed.size(), bytes - text.size()));
    }
    return text;
}

std::vector<TextCorpusCase> MakeTextCorpus()
{
    std::vector<TextCorpusCase> cases;
    TextCorpusCase many{"many-short-paragraphs", {}, 32, 0};
    many.m_Paragraphs.reserve(100000);
    for (std::size_t i = 0; i < 100000; ++i)
    {
        auto text = "Paragraph " + std::to_string(i) + ": beam / load / mesh / result 123.456 mm.";
        many.m_Bytes += text.size();
        many.m_Paragraphs.push_back(std::move(text));
    }
    cases.push_back(std::move(many));
    TextCorpusCase longParagraph{"long-paragraph-64k", {MakeEditParagraph(65536)}, 1, 65536};
    cases.push_back(std::move(longParagraph));
    TextCorpusCase mixed{"mixed-direction-and-fallback", {}, 16, 0};
    for (int i = 0; i < 128; ++i)
    {
        auto text = "Load Δ " + std::to_string(i) + " / العربية / עברית / नमस्ते / e\u0301 / 123.45 mm";
        mixed.m_Bytes += text.size();
        mixed.m_Paragraphs.push_back(std::move(text));
    }
    cases.push_back(std::move(mixed));
    TextCorpusCase wrap{"wrapping-paragraphs", {}, 8, 0};
    for (int i = 0; i < 32; ++i)
    {
        auto text = MakeEditParagraph(512) + "אבג العربية नमस्ते";
        wrap.m_Bytes += text.size();
        wrap.m_Paragraphs.push_back(std::move(text));
    }
    cases.push_back(std::move(wrap));
    return cases;
}
} // namespace EStudioUI::Testing
