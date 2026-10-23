#pragma once

#include "DialogButtonBox.h"
#include "Popup.h"

namespace EStudioUI
{
//! An in-window asynchronous modal/modeless dialog with ordinary composable content.
class Dialog : public Popup
{
  public:
    //! Creates a hidden dialog with a header, content area and OK/Cancel responses.
    explicit Dialog(std::string title = {});

    //! Creates a root-owned dialog whose lifetime follows its attached logical owner.
    static Dialog& Create(Widget& owner, std::string title = {});

    //! Returns the application-owned content container.
    Widget& Content() const;

    //! Returns the ordinary standard response presentations for caption/style customization.
    DialogButtonBox& Buttons() const;

    //! Changes the header and default accessible name.
    void SetTitle(std::string title);

    //! Returns the current header caption.
    const std::string& GetTitle() const;

    //! Replaces standard responses and chooses the first accepting response as the default.
    void SetButtons(std::vector<StandardButton> buttons);

    //! Selects an owned descendant as the Enter action; null disables the default.
    void SetDefaultButton(Button* button);

    //! Observes an owned descendant as initial focus; null chooses the first content control.
    void SetInitialFocus(Widget* widget);

    //! Opens centered without a nested loop; modeless dialogs keep outside pointer interaction available.
    PopupSession Open(bool modal = true, Widget* parentPopup = nullptr);

    //! Completes with an accepted application response; derived dialogs may validate first.
    virtual void Accept(int value = static_cast<int>(StandardButton::Ok));

    //! Completes with a rejected response; the header close button uses Cancel.
    void Reject(int value = static_cast<int>(StandardButton::Cancel));

  protected:
    //! Reports a dialog role and its title without querying native state.
    SemanticInfo DescribeSemantics() const override;

    //! Activates the default on Enter while preserving focused buttons and editor validation.
    bool OnKeyPreview(Key key, bool down, bool repeat, Modifiers modifiers) override;

  private:
    //! Tests physical descendant ownership for focus/default configuration.
    bool Owns(const Widget& widget) const;
    //! Owns the visible header caption through the widget tree.
    Label* m_Title = nullptr;
    //! Owns application content through the widget tree.
    Widget* m_Content = nullptr;
    //! Owns standard response buttons through the widget tree.
    DialogButtonBox* m_Buttons = nullptr;
    //! Observes the Enter action without retaining removed buttons.
    WidgetRef m_Default;
    //! Observes optional initial focus without retaining removed content.
    WidgetRef m_InitialFocus;
};
} // namespace EStudioUI
