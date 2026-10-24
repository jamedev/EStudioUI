#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace EStudioUI
{
//! Exact requested typography, retained with a shaped result for cache validation.
struct FontSelection
{
    //! Ordered CSS family list.
    std::string m_Family = "sans-serif";
    //! Logical pixel size, independent of framebuffer scaling.
    float m_Size = 16;
    //! Requested CSS weight.
    float m_Weight = 400;
    //! Normal, italic or oblique presentation.
    std::string m_Style = "normal";
    //! Auto, ltr or rtl paragraph direction.
    std::string m_Direction = "auto";
    //! BCP-47 language hint passed to shaping and line breaking.
    std::string m_Language = "und";
    //! Validates the complete supported typography contract without loading fonts.
    void Validate() const;

    //! Tests a bounded UTF-8 list of one through sixteen quoted or unquoted families.
    static bool IsValidFamilyList(std::string_view families) noexcept;

    //! Tests a bounded ASCII language hint shared by CSS and shaping.
    static bool IsValidLanguage(std::string_view language) noexcept;

    //! Maximum accepted family-list bytes.
    static constexpr std::size_t s_MaxFamilyBytes = 1024;

    //! Maximum accepted language-hint bytes.
    static constexpr std::size_t s_MaxLanguageBytes = 63;

    //! Compares all shaping inputs without relying on hash equality.
    bool operator==(const FontSelection&) const = default;
};

} // namespace EStudioUI
