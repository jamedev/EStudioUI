#pragma once

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <EStudioUI/PlatformTypes.h>

namespace EStudioUI::Detail
{
//! Converts validated UTF-8 into owned UTF-16.
std::wstring ToWindowsText(std::string_view text);

//! Converts bounded UTF-16 into validated owned UTF-8.
std::string FromWindowsText(std::wstring_view text);

//! Owns an OS error diagnostic without throwing for an unrecognized error code.
PlatformResult WindowsFailure(HRESULT code, std::string_view operation);
} // namespace EStudioUI::Detail
#endif
