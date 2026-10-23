#pragma once
#include "CheckBox.h"

namespace EStudioUI
{
//! A titled frame with an optional checkable header and ordinary content layout.
class GroupBox final : public Widget
{
  public:
    //! Creates an enabled noncheckable group.
    explicit GroupBox(std::string title = {});
    //! Returns the ordinary child container.
    Widget& Content() const;
    //! Changes the visible and accessible title.
    void SetTitle(std::string title);
    //! Shows or hides the interactive title.
    void SetCheckable(bool checkable);
    //! Returns the title interaction policy.
    bool IsCheckable() const;
    //! Enables/disables content while preserving header interaction.
    void SetChecked(bool checked);
    //! Returns persistent header state.
    bool IsChecked() const;
    //! Reports committed header toggles.
    Signal<bool>& Toggled();

  protected:
    //! Describes the titled group.
    SemanticInfo DescribeSemantics() const override;

  private:
    //! Observes the persistent content container.
    Widget* m_Content = nullptr;
    //! Observes the interactive header.
    CheckBox* m_Check = nullptr;
    //! Observes the noninteractive header.
    Label* m_Title = nullptr;
    //! Enables the checkable presentation.
    bool m_Checkable = false;
};
} // namespace EStudioUI
