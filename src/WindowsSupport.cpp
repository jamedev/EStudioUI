#include "WindowsSupport.h"

#ifdef _WIN32
#include <EStudioUI/ClipboardData.h>

#include <stdexcept>

namespace EStudioUI::Detail
{
std::wstring ToWindowsText(std::string_view text)
{
    ValidatePlatformText(text);
    if (text.empty())
        return {};
    const int count =
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (!count)
        throw std::invalid_argument("UTF-8 cannot be converted to Windows text.");
    std::wstring result(static_cast<std::size_t>(count), L'\0');
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), result.data(),
                             count))
        throw std::runtime_error("Windows UTF-8 conversion failed.");
    return result;
}

std::string FromWindowsText(std::wstring_view text)
{
    if (text.size() > ClipboardData::s_MaxBytes)
        throw std::length_error("Windows text exceeds the native-service payload budget.");
    if (text.empty())
        return {};
    const int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()),
                                          nullptr, 0, nullptr, nullptr);
    if (count <= 0 || count > static_cast<int>(ClipboardData::s_MaxBytes))
        throw std::invalid_argument("Windows text contains invalid UTF-16 or exceeds its UTF-8 budget.");
    std::string result(static_cast<std::size_t>(count), '\0');
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), result.data(),
                             count, nullptr, nullptr))
        throw std::runtime_error("Windows text conversion failed.");
    ValidatePlatformText(result);
    return result;
}

PlatformResult WindowsFailure(HRESULT code, std::string_view operation)
{
    std::string message(operation);
    wchar_t* text = nullptr;
    const DWORD count =
        FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                       nullptr, static_cast<DWORD>(code), 0, reinterpret_cast<wchar_t*>(&text), 0, nullptr);
    if (count && text)
    {
        try
        {
            message += ": " + FromWindowsText({text, count});
            while (!message.empty() && (message.back() == '\n' || message.back() == '\r'))
                message.pop_back();
        }
        catch (...)
        {
        }
    }
    if (text)
        LocalFree(text);
    return {PlatformStatus::Failed, std::move(message), code};
}
} // namespace EStudioUI::Detail
#endif
