#include <EStudioUI/FontSelection.h>
#include <utf8proc.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace EStudioUI
{
bool FontSelection::IsValidFamilyList(std::string_view families) noexcept
{
    if (families.empty() || families.size() > s_MaxFamilyBytes)
    {
        return false;
    }
    for (std::size_t offset = 0; offset < families.size();)
    {
        utf8proc_int32_t codepoint = 0;
        const auto size = utf8proc_iterate(reinterpret_cast<const utf8proc_uint8_t*>(families.data() + offset),
                                           static_cast<utf8proc_ssize_t>(families.size() - offset), &codepoint);
        if (size <= 0 || codepoint == 0 || (codepoint < 32 && codepoint != 9) || codepoint == 127)
        {
            return false;
        }
        offset += static_cast<std::size_t>(size);
    }
    std::size_t first = 0, count = 0;
    char quote = 0;
    for (std::size_t i = 0; i <= families.size(); ++i)
    {
        const auto c = i == families.size() ? ',' : families[i];
        if ((c == '\'' || c == '"') && (!quote || quote == c))
        {
            quote = quote ? 0 : c;
        }
        else if (c == ',' && !quote)
        {
            if (families.substr(first, i - first).find_first_not_of(" \t\"'") == std::string_view::npos || ++count > 16)
            {
                return false;
            }
            first = i + 1;
        }
    }
    return !quote && count > 0;
}

bool FontSelection::IsValidLanguage(std::string_view language) noexcept
{
    return !language.empty() && language.size() <= s_MaxLanguageBytes &&
           std::all_of(
               language.begin(), language.end(), [](unsigned char c)
               { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-'; });
}

void FontSelection::Validate() const
{
    if (!IsValidFamilyList(m_Family))
    {
        throw std::invalid_argument("Font families require a valid UTF-8 list of 1–16 names and at most 1024 bytes.");
    }
    if (!IsValidLanguage(m_Language))
    {
        throw std::invalid_argument("Font language requires 1–63 ASCII letters, digits or hyphens.");
    }
    if (!std::isfinite(m_Size) || m_Size < 0 || m_Size > 4096)
    {
        throw std::invalid_argument("Font size must be finite and between zero and 4096 logical pixels.");
    }
    if (!std::isfinite(m_Weight) || m_Weight < 1 || m_Weight > 1000)
    {
        throw std::invalid_argument("Font weight must be finite and between 1 and 1000.");
    }
    if (m_Style != "normal" && m_Style != "italic" && m_Style != "oblique")
    {
        throw std::invalid_argument("Font style must be normal, italic or oblique.");
    }
    if (m_Direction != "auto" && m_Direction != "ltr" && m_Direction != "rtl")
    {
        throw std::invalid_argument("Text direction must be auto, ltr or rtl.");
    }
}
} // namespace EStudioUI
