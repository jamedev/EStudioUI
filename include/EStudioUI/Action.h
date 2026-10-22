#pragma once

#include "Shortcut.h"
#include "State.h"

#include <vector>

namespace EStudioUI
{
//! Coherent shared command presentation and shortcut configuration.
struct ActionState
{
    //! Caption shared by buttons, menus, and future palettes.
    std::string m_Text;

    //! Whether the command may execute.
    bool m_Enabled = true;

    //! Whether associated presentations participate in the UI.
    bool m_Visible = true;

    //! Whether activation toggles persistent checked state.
    bool m_Checkable = false;

    //! Canonical checked state, false for noncheckable actions.
    bool m_Checked = false;

    //! Alternative gestures for the same command.
    std::vector<Shortcut> m_Shortcuts{};

    //! Compares command state for no-op suppression.
    bool operator==(const ActionState&) const = default;
};

//! A reusable owner-side command using the existing transactional state domain.
class Action
{
  public:
    //! Creates a command in an application-provided state domain.
    Action(StateStore& store, std::string text = {});

    //! Releases the command; active execution retains its accepted callback state.
    ~Action();

    //! Actions retain stable identity for associations.
    Action(const Action&) = delete;

    //! Actions retain stable identity for associations.
    Action& operator=(const Action&) = delete;

    //! Exposes typed properties for coherent batches, subscriptions, and worker handles.
    State<ActionState>& Properties();

    //! Returns an owned immutable configuration snapshot.
    std::shared_ptr<const ActionState> GetSnapshot() const;

    //! Returns owner-side endpoint expiry without requiring the state domain to remain open.
    std::stop_token GetStopToken() const;

    //! Captures a lifetime-bound producer handle for queuing commands from any thread.
    UiHandle<Action> GetHandle() const;

    //! Changes the shared caption.
    void SetText(std::string text);

    //! Changes execution eligibility and all associated presentations.
    void SetEnabled(bool enabled);

    //! Changes associated presentation visibility.
    void SetVisible(bool visible);

    //! Changes toggle behavior, clearing checked state when disabled.
    void SetCheckable(bool checkable);

    //! Changes canonical checked state; a true value requires a checkable action.
    void SetChecked(bool checked);

    //! Replaces all shortcut alternatives with one gesture.
    void SetShortcut(Shortcut shortcut);

    //! Replaces shortcut alternatives after validation.
    void SetShortcuts(std::vector<Shortcut> shortcuts);

    //! Executes an eligible command once; recursive execution of this action returns false.
    bool Trigger();

    //! Reports accepted execution after checked state is committed.
    Signal<>& Triggered();

  private:
    //! Retains execution state across callbacks that destroy the public action wrapper.
    struct Data;

    //! Owns properties and callbacks without retaining widget associations.
    std::shared_ptr<Data> m_Data;
};
} // namespace EStudioUI
