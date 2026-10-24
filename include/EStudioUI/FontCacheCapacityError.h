#pragma once
#include <cstddef>
#include <stdexcept>

namespace EStudioUI
{
//! Identifies the configured or hardware boundary preventing one visible text frame.
enum class FontCacheCapacityReason
{
    //! A single outline exceeds the configured byte budget.
    SingleGlyph,
    //! All glyphs already referenced by this frame must remain resident.
    VisibleWorkingSet,
    //! The visible set exceeds the supported glyph identity count.
    GlyphCount,
    //! One hardware texture buffer cannot address the required data.
    Hardware
};

//! Reports a text-frame admission failure before GPU submission; AbortFrame permits explicit recovery.
class FontCacheCapacityError : public std::length_error
{
  public:
    //! Owns quantitative diagnostics without retaining the renderer or font resources.
    FontCacheCapacityError(FontCacheCapacityReason reason, std::size_t requiredBytes, std::size_t byteLimit,
                           std::size_t requiredGlyphs);

    //! Returns the limiting resource family.
    FontCacheCapacityReason GetReason() const noexcept;

    //! Returns bytes required by the visible outlines admitted so far and the rejected outline.
    std::size_t GetRequiredBytes() const noexcept;

    //! Returns the configured hard byte limit.
    std::size_t GetByteLimit() const noexcept;

    //! Returns required visible actual-font/glyph identities.
    std::size_t GetRequiredGlyphs() const noexcept;

  private:
    //! Limiting resource.
    FontCacheCapacityReason m_Reason;

    //! Required outline/band bytes.
    std::size_t m_RequiredBytes;

    //! Configured maximum bytes.
    std::size_t m_ByteLimit;

    //! Required visible glyph identities.
    std::size_t m_RequiredGlyphs;
};
} // namespace EStudioUI
