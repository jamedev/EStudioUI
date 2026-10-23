#pragma once

#include "Dialog.h"
#include "InputDialogSession.h"
#include "TextBox.h"

namespace EStudioUI
{
//! A reusable validated single-line prompt with an owned asynchronous text result.
class InputDialog final : public Dialog
{
  public:
    //! Creates a hidden prompt reusing the ordinary TextBox validator/composition pipeline.
    InputDialog(std::string title = {}, std::string prompt = {}, std::string text = {});

    //! Creates a root-owned prompt whose lifetime follows its attached logical owner.
    static InputDialog& Create(Widget& owner, std::string title, std::string prompt, std::string text = {});

    //! Returns the ordinary editor for value, validator, placeholder and selection configuration.
    TextBox& Editor() const;

    //! Replaces the wrapping prompt and editor accessible name.
    void SetPrompt(std::string prompt);

    //! Opens centered and returns a typed result; cancellation never returns partial text.
    InputDialogSession Open(bool modal = true, Widget* parentPopup = nullptr);

    //! Accepts only an Acceptable value, snapshotting text before completion callbacks run.
    void Accept(int value = static_cast<int>(StandardButton::Ok)) override;

  private:
    //! Owns the wrapping prompt through Content().
    Label* m_Prompt = nullptr;
    //! Owns canonical input through Content().
    TextBox* m_Editor = nullptr;
    //! Shares only this presentation's accepted bytes with returned result handles.
    std::shared_ptr<std::optional<std::string>> m_ResultText;
};
} // namespace EStudioUI
