#pragma once

#include "WindowsSupport.h"
#include <EStudioUI/ClipboardData.h>

#ifdef _WIN32
namespace EStudioUI::Detail
{
//! Copies supported OS clipboard formats while bounding all native allocations.
ClipboardResult ReadWindowsClipboard(HWND owner);

//! Prepares owned native formats before replacing the OS clipboard.
PlatformResult WriteWindowsClipboard(HWND owner, const ClipboardData& data);
} // namespace EStudioUI::Detail
#endif
