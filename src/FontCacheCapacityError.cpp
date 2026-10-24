#include <EStudioUI/FontCacheCapacityError.h>
#include <string>

namespace EStudioUI
{
namespace
{
std::string Message(FontCacheCapacityReason reason, std::size_t required, std::size_t limit, std::size_t glyphs)
{
    const auto kind = reason == FontCacheCapacityReason::SingleGlyph ? "One visible glyph"
                      : reason == FontCacheCapacityReason::Hardware  ? "Visible glyph texture buffers"
                                                                     : "The visible glyph working set";
    return std::string(kind) + " requires " + std::to_string(required) + " bytes and " + std::to_string(glyphs) +
           " glyph identities; the configured font cache limit is " + std::to_string(limit) +
           " bytes (65,536 identities maximum; hardware texture-buffer limits also apply). Abort the frame and "
           "increase the budget or reduce visible content.";
}
} // namespace

FontCacheCapacityError::FontCacheCapacityError(FontCacheCapacityReason reason, std::size_t requiredBytes,
                                               std::size_t byteLimit, std::size_t requiredGlyphs)
    : std::length_error(Message(reason, requiredBytes, byteLimit, requiredGlyphs)), m_Reason(reason),
      m_RequiredBytes(requiredBytes), m_ByteLimit(byteLimit), m_RequiredGlyphs(requiredGlyphs)
{
}

FontCacheCapacityReason FontCacheCapacityError::GetReason() const noexcept
{
    return m_Reason;
}

std::size_t FontCacheCapacityError::GetRequiredBytes() const noexcept
{
    return m_RequiredBytes;
}

std::size_t FontCacheCapacityError::GetByteLimit() const noexcept
{
    return m_ByteLimit;
}

std::size_t FontCacheCapacityError::GetRequiredGlyphs() const noexcept
{
    return m_RequiredGlyphs;
}
} // namespace EStudioUI
