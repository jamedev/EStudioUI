#pragma once

#include <EStudioUI/TabWidget.h>
#include <EStudioUI/Window.h>

namespace EStudioUI
{
//! Bounded parameters for the paced, readback-free rendering benchmark.
struct RenderingProfileOptions
{
    //! Frames retained for each workload and redraw mode after warmup.
    int m_Frames = 180;

    //! Steady-state frames excluded from measured percentiles.
    int m_Warmup = 30;

    //! Scripted presentation rate; zero explicitly selects unpaced throughput.
    double m_FrameRate = 60;

    //! Selects all, rich-css, widget-styles, isolated-slider, text, or baseline.
    std::string m_Case = "all";
};

//! Profiles gallery interactions and isolated controls without synchronous pixel readback.
void ProfileRenderingWorkload(Window& window, TabWidget& tabs, const std::filesystem::path& assets,
                              const std::filesystem::path& theme, const std::filesystem::path& output,
                              const RenderingProfileOptions& options);
} // namespace EStudioUI
