#include "WindowCommandButton.h"

#include <EStudioUI/Painter.h>

#include <algorithm>

namespace EStudioUI::Detail
{
WindowCommandButton::WindowCommandButton(WindowCommand command) : ToolButton(), m_Command(command)
{
}

void WindowCommandButton::SetCommand(WindowCommand command)
{
    VerifyAccess();
    if (m_Command != command)
    {
        m_Command = command;
        Invalidate(Invalidation::Paint);
    }
}

void WindowCommandButton::Paint(Renderer& renderer, FontFace& font, double time, float opacity) const
{
    ToolButton::Paint(renderer, font, time, opacity);
    const auto bounds = GetContentBounds();
    const auto clip = bounds.Intersect(GetClip());
    const float size = std::min({10.0f, bounds.m_Width * .6f, bounds.m_Height * .6f});
    if (size <= 0 || clip.m_Width <= 0 || clip.m_Height <= 0)
    {
        return;
    }
    Painter painter(renderer, font, clip, opacity,
                    {bounds.m_X + (bounds.m_Width - size) / 2, bounds.m_Y + (bounds.m_Height - size) / 2});
    painter.SetPen(GetStyle().m_Color, 1.25f);
    if (m_Command == WindowCommand::Minimize)
    {
        painter.DrawLine({0, size / 2}, {size, size / 2});
    }
    else if (m_Command == WindowCommand::Close)
    {
        painter.DrawLine({0, 0}, {size, size});
        painter.DrawLine({0, size}, {size, 0});
    }
    else if (m_Command == WindowCommand::Maximize)
    {
        painter.SetBrush(Color{0, 0, 0, 0});
        painter.DrawRect({0, 0, size, size});
    }
    else if (m_Command == WindowCommand::Restore)
    {
        painter.SetBrush(Color{0, 0, 0, 0});
        painter.DrawLine({size * .3f, size * .3f}, {size * .3f, 0});
        painter.DrawLine({size * .3f, 0}, {size, 0});
        painter.DrawLine({size, 0}, {size, size * .7f});
        painter.DrawLine({size, size * .7f}, {size * .7f, size * .7f});
        painter.DrawRect({0, size * .3f, size * .7f, size * .7f});
    }
    else if (m_Command == WindowCommand::Float)
    {
        painter.DrawLine({0, 2}, {0, size});
        painter.DrawLine({0, size}, {size - 2, size});
        painter.DrawLine({size - 2, size}, {size - 2, size - 3});
        painter.DrawLine({0, 2}, {3, 2});
        painter.DrawLine({3, size - 3}, {size, 0});
        painter.DrawLine({size - 4, 0}, {size, 0});
        painter.DrawLine({size, 0}, {size, 4});
    }
    else
    {
        painter.DrawLine({1, size / 3}, {size / 2, size * .7f});
        painter.DrawLine({size / 2, size * .7f}, {size - 1, size / 3});
    }
}
} // namespace EStudioUI::Detail
