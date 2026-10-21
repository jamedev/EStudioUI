#pragma once

#include <EStudioUI/TextBox.h>
#include <EStudioUI/Label.h>
#include <EStudioUI/ToolButton.h>
#include <EStudioUI/Window.h>

namespace EStudioUI
{
//! Interactive CSS and typed-property animation examples with real framebuffer checks.
class AnimationGallery
{
  public:
    //! Builds an animation playground using editable endpoint CSS assets.
    AnimationGallery(Window& window, Widget& page, const std::filesystem::path& assets);

    //! Disconnects scheduler callbacks before releasing demo state.
    ~AnimationGallery();

    //! Samples paused timelines deterministically and captures actual rendered frames.
    void Verify(const std::filesystem::path& output);

  private:
    //! Reads and validates the duration, easing, and repetition controls.
    bool ReadOptions(AnimationOptions& options);

    //! Starts an animation between the two editable CSS endpoint files.
    void PlayStyle(const AnimationOptions& options);

    //! Starts synchronized translation, colors, absolute bounds, and a custom magnitude.
    void PlayProperties(const AnimationOptions& options);

    //! Restores the CSS start endpoint and removes persistent presentation overrides.
    void Reset();

    //! Cycles the selected easing curve.
    void NextCurve();

    //! Reads a UTF-8 demo asset or throws a diagnostic.
    std::string ReadAsset(const std::string& name) const;

    //! Observes the containing window.
    Window& m_Window;

    //! Locates the base theme and animation endpoint files.
    std::filesystem::path m_Assets;

    //! Observes the animated card.
    Widget* m_Card = nullptr;

    //! Observes the working button within the animated card.
    Button* m_Button = nullptr;

    //! Observes the editable animated text field.
    TextBox* m_Editor = nullptr;

    //! Observes the absolute-geometry example.
    Label* m_Badge = nullptr;

    //! Observes the custom scalar readout.
    Label* m_Counter = nullptr;

    //! Observes the status line.
    Label* m_Status = nullptr;

    //! Observes the duration input in seconds.
    TextBox* m_Duration = nullptr;

    //! Observes the easing selector.
    ToolButton* m_CurveButton = nullptr;

    //! Observes the repeat/alternate toggle.
    ToolButton* m_Repeat = nullptr;

    //! Stores the current timeline identity.
    AnimationId m_Id = 0;

    //! Stores the selected curve index.
    unsigned m_Curve = 0;

    //! Counts button activations during animation.
    int m_Clicks = 0;

    //! Disconnects completion notifications at destruction.
    Connection m_Finished;
};
} // namespace EStudioUI
