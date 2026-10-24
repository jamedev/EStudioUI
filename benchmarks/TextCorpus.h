#pragma once
#include <cstddef>
#include <string>
#include <vector>

namespace EStudioUI::Testing
{
//! A deterministic UTF-8 paragraph fixture, independent of widgets and graphics.
struct TextCorpusCase
{
    //! Stable ASCII case identifier.
    std::string m_Id;
    //! Owned single-paragraph inputs, with no line separators.
    std::vector<std::string> m_Paragraphs;
    //! Number of paragraphs intentionally visible in the viewport fixture.
    std::size_t m_VisibleCount = 1;
    //! Total stored UTF-8 bytes.
    std::size_t m_Bytes = 0;
};

//! Builds fixed many-paragraph, long-paragraph and mixed-script fixtures without random or external data.
std::vector<TextCorpusCase> MakeTextCorpus();

//! Returns a deterministic owning ASCII edit target of the requested bounded size.
std::string MakeEditParagraph(std::size_t bytes);
} // namespace EStudioUI::Testing
