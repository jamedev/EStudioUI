#pragma once

#include <EStudioUI/ToolButton.h>

namespace EStudioUI::Detail
{
//! GPU-drawn command symbols independent of the selected font's glyph coverage.
enum class WindowCommand
{
    //! Native minimize command.
    Minimize,

    //! Native maximize/restore command.
    Maximize,

    //! Native or panel close command.
    Close,

    //! Panel dock/float command.
    Float,

    //! Expand a compact picker or recent-choice menu.
    DropDown
};

//! An ordinary CSS tool button with a small analytic GPU symbol.
class WindowCommandButton final : public ToolButton
{
  public:
    //! Selects a symbol while retaining normal button/action/input semantics.
    explicit WindowCommandButton(WindowCommand command);

  protected:
    //! Appends a bounded symbol to the ordinary CSS background and decorations.
    void Paint(Renderer& renderer, FontFace& font, double time, float opacity) const override;

  private:
    //! Immutable symbol choice.
    WindowCommand m_Command;
};
} // namespace EStudioUI::Detail
