#pragma once

#include <EStudioUI/PlatformServices.h>

namespace EStudioUI::Detail
{
//! Creates the native adapter for an already initialized window host.
std::unique_ptr<PlatformServices> CreateNativePlatformServices(PlatformHost host);
} // namespace EStudioUI::Detail
