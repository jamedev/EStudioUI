#pragma once

#include "FontFace.h"
#include "ToolButton.h"

#include <array>

namespace EStudioUI
{
//! A command-menu presentation with GPU text icons, check marks, hints and submenu arrows.
class MenuItem final : public ToolButton
{
  public:
    //! Creates a captioned menu item using the ordinary button activation path.
    explicit MenuItem(std::string text = {});

    //! Releases the menu bar's persistent command association when this presentation is removed.
    ~MenuItem() override;

    //! Binds a shared command and keeps menu-bar shortcuts coherent before/after presentation.
    void SetAction(std::shared_ptr<Action> action) override;

    //! Sets an optional UTF-8 glyph icon, limited to 256 bytes; empty removes it.
    void SetIcon(std::string icon);

    //! Returns the configured glyph icon.
    const std::string& GetIcon() const;

    //! Sets a display-only hint; associated actions refresh it from their first shortcut.
    void SetShortcutHint(std::string hint);

    //! Returns the current display hint without parsing or shaping.
    const std::string& GetShortcutHint() const;

  protected:
    //! Refreshes shortcut presentation after the canonical action has committed.
    void OnActionChanged() override;
    //! Releases retained text layouts after a font registry change.
    void OnFontsChanged() noexcept override;
    //! Measures the caption, fixed leading/trailing slots and optional hint.
    Point Measure(FontFace& font) const override;
    //! Shares painted text baselines with layout.
    MeasureResult MeasureConstrained(FontFace& font, const MeasureConstraints& constraints) const override;
    //! Paints precomputed CSS parts and cached glyph geometry.
    void Paint(Renderer& renderer, FontFace& font, double time, float opacity) const override;
    //! Reports a menu-item role and canonical toggle/submenu actions.
    SemanticInfo DescribeSemantics() const override;
    //! Uses the same submenu button path for semantic expand/collapse.
    SemanticStatus OnSemanticAction(const SemanticCommand& command) override;

  private:
    friend class ContextMenu;
    //! Observes a menu bar that exposes this command independently of popup visibility.
    WidgetRef m_MenuBar;
    //! Returns one cached caption/icon/hint layout, shaping only after content/font changes.
    const TextLayout& Text(FontFace& font, std::size_t part) const;
    //! Stores the optional icon glyphs.
    std::string m_Icon;
    //! Stores the already formatted shortcut hint.
    std::string m_Hint;
    //! Enables the analytic trailing submenu chevron.
    bool m_HasSubmenu = false;
    //! Observes the attached submenu for semantic collapse.
    WidgetRef m_Submenu;
    //! Retains three text layouts independently of renderer/font-cache eviction.
    mutable std::array<TextLayout, 3> m_Layouts;
    //! Identifies content changes without shaping during unchanged paints.
    mutable std::array<std::string, 3> m_LayoutTexts;
};
} // namespace EStudioUI
