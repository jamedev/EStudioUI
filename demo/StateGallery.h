#pragma once

#include <EStudioUI/Bindings.h>
#include <EStudioUI/State.h>
#include <EStudioUI/Window.h>

#include <atomic>
#include <thread>

namespace EStudioUI
{
//! Demonstrates shared typed values, coherent transactions, and worker publication.
class StateGallery
{
  public:
    //! Builds the live state example with the containing context's lazy state store.
    StateGallery(Window& window, Widget& page, std::filesystem::path assets);

    //! Cooperatively stops the value-only producer before its states are released.
    ~StateGallery();

    //! Exercises the public state/binding API and captures the actual rendered result.
    void Verify(const std::filesystem::path& output);

  private:
    //! Commits several related values in a single coherent revision.
    void ApplyPreset();

    //! Demonstrates atomic rollback when any final candidate fails validation.
    void RejectPreset();

    //! Starts a replaceable producer that only captures a typed state handle and owned values.
    void StartProgress(bool burst = false);

    //! Recomputes the multi-value readout once per coherent revision.
    void RefreshSummary(std::uint64_t revision);

    //! Updates the weakly observed status caption while the receiving page is live.
    void SetStatus(std::string message);

    //! Observes the containing native window.
    Window& m_Window;

    //! Locates the installed font for native window lifetime regression probes.
    std::filesystem::path m_Assets;

    //! Observes the owner domain, which outlives this gallery's typed values.
    StateStore& m_Store;

    //! Shares one validated scalar between multiple independent views.
    State<double> m_Level;

    //! Shares one UTF-8 name between its editor and readout.
    State<std::string> m_Name;

    //! Enables both scalar editors through one boolean value.
    State<bool> m_Enabled;

    //! Controls one optional readout through a separate boolean value.
    State<bool> m_Visible;

    //! Receives coalesced producer updates independently of the edit controls.
    State<double> m_Progress;

    //! Observes the scalar slider without extending its widget lifetime.
    WidgetRef m_Slider;

    //! Observes the numeric editor without extending its widget lifetime.
    WidgetRef m_Number;

    //! Observes the editable text source without extending its widget lifetime.
    WidgetRef m_Editor;

    //! Observes the text mirror without extending its widget lifetime.
    WidgetRef m_Echo;

    //! Observes the derived scalar readout without extending its widget lifetime.
    WidgetRef m_LevelReadout;

    //! Observes the visibility-bound text without extending its widget lifetime.
    WidgetRef m_Optional;

    //! Observes the enabled-state toggle without extending its widget lifetime.
    WidgetRef m_EnableToggle;

    //! Observes the visibility-state toggle without extending its widget lifetime.
    WidgetRef m_VisibleToggle;

    //! Observes the coherent multi-value summary without extending its widget lifetime.
    WidgetRef m_Summary;

    //! Observes the transaction counters without extending their widget lifetime.
    WidgetRef m_Counters;

    //! Observes an unrelated sibling used by locality verification.
    WidgetRef m_Isolated;

    //! Observes the delivery status caption without extending its widget lifetime.
    WidgetRef m_Status;

    //! Keeps editable-text binding failures inspectable for the verification example.
    Binding m_TextBinding;

    //! Counts value notifications, independently of render invalidation.
    std::size_t m_Notifications = 0;

    //! Counts coherent derived recomputations, with one per observed revision.
    std::size_t m_DerivedUpdates = 0;

    //! Suppresses redundant derived work when several values share a commit.
    std::uint64_t m_SummaryRevision = 0;

    //! Owns a single cooperative producer; no worker captures a widget or this gallery.
    std::jthread m_Worker;

    //! Counts posted publications without reading owner-side state from the worker.
    std::shared_ptr<std::atomic<std::size_t>> m_Publications = std::make_shared<std::atomic<std::size_t>>(0);

    //! Holds the final receipt, read only after the producer is joined.
    std::shared_ptr<DispatchReceipt> m_LastPublication = std::make_shared<DispatchReceipt>();
};
} // namespace EStudioUI
