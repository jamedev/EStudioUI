#include <EStudioUI/TextInputClient.h>
#include <EStudioUI/ClipboardData.h>

#include <stdexcept>

namespace EStudioUI
{
TextInputClient::~TextInputClient() = default;

void ValidateTextComposition(const TextCompositionEvent& event)
{
    if (event.m_Kind < TextCompositionKind::Preedit || event.m_Kind > TextCompositionKind::Cancel)
        throw std::invalid_argument("Unknown text composition kind.");
    if (event.m_Text.size() > 65536)
        throw std::length_error("A native composition update is limited to 64 KiB.");
    ValidatePlatformText(event.m_Text);
    if (event.m_Kind == TextCompositionKind::Preedit &&
        (event.m_Caret > event.m_Text.size() ||
         (event.m_Caret < event.m_Text.size() &&
          (static_cast<unsigned char>(event.m_Text[event.m_Caret]) & 0xC0U) == 0x80U)))
        throw std::invalid_argument("The composition caret must be a UTF-8 code-point boundary.");
    if (event.m_Kind == TextCompositionKind::Cancel && !event.m_Text.empty())
        throw std::invalid_argument("A composition cancellation cannot contain text.");
}
} // namespace EStudioUI
