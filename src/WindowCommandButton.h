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

    //! Native restore command presentation.
    Restore,

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
    //! Selects a symbol and optionally excludes native caption controls from client tab traversal.
    explicit WindowCommandButton(WindowCommand command, bool nativeCaption = false);

    //! Replaces a symbol with paint-only invalidation.
    void SetCommand(WindowCommand command);

  protected:
    //! Keeps native caption controls outside client keyboard navigation.
    bool IsFocusable() const override;

    //! Appends a bounded symbol to the ordinary CSS background and decorations.
    void Paint(Renderer& renderer, FontFace& font, double time, float opacity) const override;

  private:
    //! Current symbol choice.
    WindowCommand m_Command;

    //! Identifies native caption presentation independently of CSS classes.
    bool m_NativeCaption = false;
};
} // namespace EStudioUI::Detail
