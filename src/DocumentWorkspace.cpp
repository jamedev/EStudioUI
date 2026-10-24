#include <EStudioUI/DocumentWorkspace.h>
#include <EStudioUI/ContextMenu.h>
#include <EStudioUI/MainWindow.h>
#include <EStudioUI/MessageBox.h>
#include <EStudioUI/TextBuffer.h>
#include <EStudioUI/UiContext.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <map>
#include <set>
#include <stdexcept>

namespace EStudioUI
{
namespace
{
void ValidateText(const std::string& text, std::size_t limit, const char* description)
{
    if (text.empty() || text.size() > limit || TextBuffer::Sanitize(text) != text)
    {
        throw std::invalid_argument(std::string(description) + " must be bounded, nonempty single-line UTF-8.");
    }
}

bool Within(const Widget* child, const Widget* ancestor)
{
    for (; child; child = child->GetParent())
    {
        if (child == ancestor)
        {
            return true;
        }
    }
    return false;
}

void Reveal(Widget& widget)
{
    const WidgetRef target(&widget);
    for (auto* parent = widget.GetParent(); parent; parent = parent->GetParent())
    {
        if (auto* tabs = dynamic_cast<TabWidget*>(parent))
        {
            const WidgetRef ancestor(tabs);
            for (int index = 0; index < tabs->GetTabCount(); ++index)
            {
                if (Within(target.Get(), &tabs->GetPage(index)))
                {
                    tabs->SetCurrentIndex(index);
                    if (!target.Get() || !ancestor.Get())
                    {
                        return;
                    }
                    break;
                }
            }
        }
    }
}

class DocumentContent final : public Widget
{
  public:
    std::function<void()> m_Activated;

    DocumentContent() : Widget("document-content")
    {
        SetWidth(SizeRule::Fill());
        SetHeight(SizeRule::Fill());
    }

  protected:
    void OnDescendantFocused(Widget&) override
    {
        if (const auto callback = m_Activated)
        {
            callback();
        }
    }
};
} // namespace

struct DocumentWorkspace::Impl : std::enable_shared_from_this<Impl>
{
    struct Stamp
    {
        HistoryCheckpoint m_History;
        std::uint64_t m_Epoch = 0;
        bool operator==(const Stamp&) const = default;
    };

    struct Entry
    {
        DocumentOptions m_Options;
        WidgetRef m_Content, m_Page, m_Confirmation;
        std::uint64_t m_Epoch = 0, m_SaveEpoch = 0;
        bool m_Modified = false, m_Saving = false, m_Removing = false, m_PreparingSave = false;
        std::string m_SaveError;
        DocumentSaveRequest m_Save;
        std::unique_ptr<Window> m_Window;
        std::vector<ScopedConnection> m_Connections;
        std::vector<std::shared_ptr<Action>> m_WindowActions;
        std::uint64_t m_StyleRevision = 0;
    };

    struct CloseBatch
    {
        DocumentCloseSession m_Session;
        std::vector<std::string> m_Ids;
        std::map<std::string, Stamp> m_Approved;
        WidgetRef m_Dialog;
        std::string m_Prompt, m_Waiting;
        bool m_All = false, m_Prepared = false;
    };

    WidgetRef m_Self;
    Window* m_Owner = nullptr;
    std::optional<UiHandle<Window>> m_WindowTarget;
    UiContext* m_Ui = nullptr;
    TabWidget* m_Tabs = nullptr;
    std::map<std::string, std::shared_ptr<Entry>> m_Entries;
    std::vector<std::string> m_Insertion;
    std::vector<std::unique_ptr<Window>> m_Retired;
    std::vector<ScopedConnection> m_Connections;
    std::shared_ptr<Action> m_Undo, m_Redo, m_Save, m_Close;
    std::shared_ptr<CloseBatch> m_Batch;
    std::atomic<bool> m_Ready = false;
    bool m_Initialized = false, m_Closing = false, m_Operating = false, m_Pumping = false, m_Notifying = false;
    std::string m_Current, m_AnnouncedCurrent;
    std::uint64_t m_Revision = 0, m_Notified = 0, m_ThemeRevision = 0;
    Signal<> m_Changed;
    Signal<std::string> m_CurrentChanged;
    WidgetRef m_Menu;
    std::exception_ptr m_CallbackError;
    bool m_WaitingOwnerClose = false;

    DocumentWorkspace* Live() const
    {
        if (m_Closing || (m_WindowTarget && !m_WindowTarget->IsConnected()))
        {
            return nullptr;
        }
        return dynamic_cast<DocumentWorkspace*>(m_Self.Get());
    }

    DocumentWorkspace& Check() const
    {
        auto* self = Live();
        if (!self || !self->GetContext() || (m_Initialized && self->GetContext() != m_Ui))
        {
            throw std::logic_error("Document workspace requires its original live UI owner.");
        }
        self->VerifyStructuralMutation();
        return *self;
    }

    std::shared_ptr<Entry> Find(std::string_view id) const
    {
        const auto found = m_Entries.find(std::string(id));
        if (found == m_Entries.end())
        {
            throw std::out_of_range("Unknown document identity: " + std::string(id));
        }
        return found->second;
    }

    int Index(const Entry& entry) const
    {
        for (int index = 0; index < m_Tabs->GetTabCount(); ++index)
        {
            if (&m_Tabs->GetPage(index) == entry.m_Page.Get())
            {
                return index;
            }
        }
        return -1;
    }

    std::shared_ptr<Entry> At(int index) const
    {
        if (index < 0 || index >= m_Tabs->GetTabCount())
        {
            return {};
        }
        const auto* page = &m_Tabs->GetPage(index);
        for (const auto& [id, entry] : m_Entries)
        {
            (void)id;
            if (entry->m_Page.Get() == page)
            {
                return entry;
            }
        }
        return {};
    }

    bool Modified(const Entry& entry) const
    {
        return entry.m_Modified || (entry.m_Options.m_History && entry.m_Options.m_History->GetState().m_Modified);
    }

    Stamp Capture(const Entry& entry) const
    {
        return {entry.m_Options.m_History ? entry.m_Options.m_History->CaptureCheckpoint() : HistoryCheckpoint{},
                entry.m_Epoch};
    }

    std::vector<std::string> Ids() const
    {
        std::vector<std::string> ids;
        for (int index = 0; index < m_Tabs->GetTabCount(); ++index)
        {
            if (const auto entry = At(index))
            {
                ids.push_back(entry->m_Options.m_Id);
            }
        }
        for (const auto& id : m_Insertion)
        {
            if (const auto found = m_Entries.find(id); found != m_Entries.end() && found->second->m_Window)
            {
                ids.push_back(id);
            }
        }
        return ids;
    }

    void Actions()
    {
        if (!m_Undo || !Live())
        {
            return;
        }
        const auto found = m_Entries.find(m_Current);
        const auto entry = found == m_Entries.end() ? nullptr : found->second;
        const auto state =
            entry && entry->m_Options.m_History ? entry->m_Options.m_History->GetState() : HistoryState{};
        const auto update = [&](const std::shared_ptr<Action>& action, std::string text, bool enabled)
        {
            const auto current = action->GetSnapshot();
            if (current->m_Text == text && current->m_Enabled == enabled)
            {
                return;
            }
            auto candidate = *current;
            candidate.m_Text = std::move(text);
            candidate.m_Enabled = enabled;
            action->Properties().Set(std::move(candidate));
        };
        try
        {
            m_Ui->States().Batch(
                [&]
                {
                    update(m_Undo, state.m_UndoText.empty() ? "Undo" : "Undo " + state.m_UndoText,
                           state.m_UndoCount != 0 && !m_Batch);
                    update(m_Redo, state.m_RedoText.empty() ? "Redo" : "Redo " + state.m_RedoText,
                           state.m_RedoCount != 0 && !m_Batch);
                    update(m_Save, entry && entry->m_Saving ? "Saving…" : "Save",
                           entry && bool(entry->m_Options.m_Save) && !entry->m_Saving && !m_Batch);
                    update(m_Close, "Close document", entry && !m_Batch);
                    for (const auto& [identity, item] : m_Entries)
                    {
                        (void)identity;
                        if (item->m_WindowActions.size() != 4 || !item->m_Window || item->m_Window->IsClosed())
                        {
                            continue;
                        }
                        const auto status =
                            item->m_Options.m_History ? item->m_Options.m_History->GetState() : HistoryState{};
                        update(item->m_WindowActions[0],
                               status.m_UndoText.empty() ? "Undo" : "Undo " + status.m_UndoText,
                               status.m_UndoCount != 0 && !m_Batch);
                        update(item->m_WindowActions[1],
                               status.m_RedoText.empty() ? "Redo" : "Redo " + status.m_RedoText,
                               status.m_RedoCount != 0 && !m_Batch);
                        update(item->m_WindowActions[2], item->m_Saving ? "Saving…" : "Save",
                               bool(item->m_Options.m_Save) && !item->m_Saving && !m_Batch);
                        update(item->m_WindowActions[3], "Close document", !m_Batch);
                    }
                });
        }
        catch (...)
        {
            m_CallbackError = std::current_exception();
        }
    }

    void Label(const std::shared_ptr<Entry>& entry)
    {
        if (!Live() || entry->m_Removing)
        {
            return;
        }
        const bool modified = Modified(*entry);
        if (entry->m_Window && !entry->m_Window->IsClosed())
        {
            entry->m_Window->SetTitle((modified ? "* " : "") + entry->m_Options.m_Title);
        }
        else if (const int index = Index(*entry); index >= 0)
        {
            m_Tabs->SetTabText(index, entry->m_Options.m_Title);
            m_Tabs->SetTabModified(index, modified);
            auto& selector = m_Tabs->GetTabButton(index);
            if (entry->m_Saving)
            {
                selector.AddClass("document-saving");
            }
            else
            {
                selector.RemoveClass("document-saving");
            }
        }
    }

    void Notify()
    {
        if (m_Notifying || m_Operating || !Live())
        {
            return;
        }
        const auto keepAlive = shared_from_this();

        struct Scope
        {
            bool& m_Flag;

            ~Scope()
            {
                m_Flag = false;
            }
        } scope{m_Notifying};

        m_Notifying = true;
        try
        {
            for (unsigned turn = 0; Live() && m_Notified != m_Revision; ++turn)
            {
                if (turn >= 64)
                {
                    throw std::logic_error("Document workspace observers exceeded 64 reentrant changes.");
                }
                m_Notified = m_Revision;
                if (m_AnnouncedCurrent != m_Current)
                {
                    const auto current = m_Current;
                    m_AnnouncedCurrent = current;
                    m_CurrentChanged.Emit(current);
                    if (!Live())
                    {
                        return;
                    }
                }
                m_Changed.Emit();
            }
        }
        catch (...)
        {
            m_CallbackError = std::current_exception();
        }
    }

    template <typename Function> void Mutate(Function function)
    {
        auto lease = Check().RetainOwner();
        if (m_Operating)
        {
            throw std::logic_error("A document ownership operation cannot be entered recursively.");
        }
        {
            struct Scope
            {
                bool& m_Flag;

                ~Scope()
                {
                    m_Flag = false;
                }
            } scope{m_Operating};

            m_Operating = true;
            function();
        }
        if (Live())
        {
            ++m_Revision;
            Actions();
            Notify();
        }
    }

    void Select(const std::shared_ptr<Entry>& entry, bool reveal)
    {
        m_Current = entry->m_Options.m_Id;
        if (entry->m_Window)
        {
            if (reveal)
            {
                entry->m_Window->Restore();
            }
        }
        else if (const int index = Index(*entry); index >= 0)
        {
            m_Tabs->SetCurrentIndex(index);
        }
    }

    void SelectSurvivor()
    {
        if (m_Entries.contains(m_Current))
        {
            return;
        }
        if (const auto entry = At(m_Tabs->GetCurrentIndex()))
        {
            m_Current = entry->m_Options.m_Id;
        }
        else
        {
            m_Current = m_Entries.empty() ? std::string{} : m_Entries.begin()->first;
        }
    }

    void Retire(const std::shared_ptr<Entry>& entry)
    {
        if (!entry->m_Window)
        {
            return;
        }
        entry->m_Removing = true;
        auto host = std::move(entry->m_Window);
        entry->m_WindowActions.clear();
        m_Retired.push_back(std::move(host));
        m_Retired.back()->Close();
        entry->m_Removing = false;
    }

    void Remove(const std::shared_ptr<Entry>& entry)
    {
        entry->m_Removing = true;
        entry->m_Save.Expire();
        const auto index = Index(*entry);
        m_Entries.erase(entry->m_Options.m_Id);
        std::erase(m_Insertion, entry->m_Options.m_Id);
        if (index >= 0)
        {
            m_Tabs->RemoveTab(index);
        }
        else if (auto* content = entry->m_Content.Get())
        {
            if (auto* parent = content->GetParent())
            {
                parent->RemoveChild(*content);
            }
        }
        if (Live())
        {
            Retire(entry);
        }
        SelectSurvivor();
    }

    void Settle(DocumentCloseStatus status, std::string message = {})
    {
        const auto batch = std::exchange(m_Batch, {});
        if (!batch)
        {
            return;
        }
        const auto dialog = batch->m_Dialog;
        batch->m_Dialog = {};
        try
        {
            if (auto* popup = dynamic_cast<Dialog*>(dialog.Get()))
            {
                if (popup->IsOpen())
                {
                    popup->Reject();
                }
            }
        }
        catch (...)
        {
            m_CallbackError = std::current_exception();
        }
        if (Live())
        {
            ++m_Revision;
            Actions();
            Notify();
        }
        batch->m_Session.Complete({status, batch->m_Ids, std::move(message)});
    }

    void Cancel(DocumentCloseStatus status = DocumentCloseStatus::Cancelled, std::string message = {})
    {
        Settle(status, std::move(message));
    }

    void DeactivateTargets(const std::shared_ptr<CloseBatch>& batch)
    {
        std::set<UiContext*> contexts;
        for (const auto& id : batch->m_Ids)
        {
            if (const auto found = m_Entries.find(id); found != m_Entries.end())
            {
                if (auto* content = found->second->m_Content.Get())
                {
                    if (auto* ui = content->GetContext())
                    {
                        contexts.insert(ui);
                    }
                }
            }
        }
        for (auto* ui : contexts)
        {
            ui->Deactivate();
            if (!Live() || m_Batch != batch)
            {
                return;
            }
        }
    }

    DocumentSaveRequest Save(const std::shared_ptr<Entry>& entry)
    {
        Check();
        if (entry->m_Saving)
        {
            return entry->m_Save;
        }
        if (entry->m_PreparingSave)
        {
            throw std::logic_error("An editor cannot recursively start its own document save.");
        }

        struct PreparationScope
        {
            bool& m_Active;

            ~PreparationScope()
            {
                m_Active = false;
            }
        } preparing{entry->m_PreparingSave};

        entry->m_PreparingSave = true;
        Stamp checkpoint;
        std::exception_ptr preparationError;
        try
        {
            if (auto* content = entry->m_Content.Get())
            {
                if (auto* ui = content->GetContext(); ui && Within(ui->GetFocus(), content))
                {
                    ui->SetFocus(nullptr);
                }
            }
            Check();
            checkpoint = Capture(*entry);
        }
        catch (...)
        {
            preparationError = std::current_exception();
            m_CallbackError = preparationError;
        }
        if (!Live())
        {
            return {};
        }
        const auto weak = weak_from_this();
        auto request = DocumentSaveRequest::Create(entry->m_Options.m_Id, checkpoint.m_History, m_Ui->GetDispatcher(),
                                                   [weak]
                                                   {
                                                       if (const auto data = weak.lock())
                                                       {
                                                           data->m_Ready.store(true, std::memory_order_release);
                                                       }
                                                   });
        const auto handler = entry->m_Options.m_Save;
        entry->m_SaveEpoch = entry->m_Epoch;
        entry->m_Saving = true;
        entry->m_SaveError.clear();
        entry->m_Save = request;
        Label(entry);
        ++m_Revision;
        Actions();
        Notify();
        if (!Live())
        {
            return request;
        }
        try
        {
            if (preparationError)
            {
                request.Complete({DocumentSaveStatus::Failed, "The active editor could not commit its value."});
            }
            else if (handler)
            {
                handler(request);
            }
            else
            {
                request.Complete({DocumentSaveStatus::Failed, "No document save handler is installed."});
            }
        }
        catch (const std::exception& error)
        {
            request.Complete(
                {DocumentSaveStatus::Failed, TextBuffer::Sanitize(std::string(error.what()).substr(0, 2048))});
        }
        catch (...)
        {
            request.Complete({DocumentSaveStatus::Failed, "The document save handler failed."});
        }
        return request;
    }

    void Prompt(const std::shared_ptr<CloseBatch>& batch, const std::shared_ptr<Entry>& entry)
    {
        auto* content = entry->m_Content.Get();
        if (!content)
        {
            Settle(DocumentCloseStatus::Failed, "A document view was removed outside its workspace.");
            return;
        }
        Select(entry, true);
        if (!entry->m_Window)
        {
            Reveal(Check());
        }
        if (!Live() || m_Batch != batch || !entry->m_Content.Get())
        {
            return;
        }
        batch->m_Prompt = entry->m_Options.m_Id;
        const auto stamp = Capture(*entry);
        auto* existing = dynamic_cast<MessageBox*>(entry->m_Confirmation.Get());
        if (!existing || existing->GetContext() != entry->m_Content.Get()->GetContext())
        {
            existing = &MessageBox::Create(*entry->m_Content.Get(), {}, {});
            entry->m_Confirmation = WidgetRef(existing);
        }
        auto& dialog = *existing;
        dialog.SetTitle("Save changes to " + entry->m_Options.m_Title + "?");
        dialog.SetMessage("Save your changes before closing this document.");
        dialog.AddClass("document-close-dialog");
        dialog.SetKind(MessageKind::Question);
        dialog.SetButtons({StandardButton::Save, StandardButton::Discard, StandardButton::Cancel});
        dialog.Buttons().GetButton(StandardButton::Save)->SetEnabled(bool(entry->m_Options.m_Save));
        batch->m_Dialog = WidgetRef(&dialog);
        const auto weak = weak_from_this();
        dialog.Open().OnFinished(
            [weak, batch, entry, stamp](PopupResult result)
            {
                const auto data = weak.lock();
                if (!data || !data->Live() || data->m_Batch != batch)
                {
                    return;
                }
                batch->m_Dialog = {};
                batch->m_Prompt.clear();
                if (result.m_Value == static_cast<int>(StandardButton::Discard) &&
                    result.m_Reason == PopupCloseReason::Accepted)
                {
                    batch->m_Approved[entry->m_Options.m_Id] = stamp;
                }
                else if (result.m_Value == static_cast<int>(StandardButton::Save) &&
                         result.m_Reason == PopupCloseReason::Accepted)
                {
                    batch->m_Waiting = entry->m_Options.m_Id;
                    data->Save(entry);
                }
                else
                {
                    data->Settle(DocumentCloseStatus::Cancelled);
                    return;
                }
                data->m_Ready.store(true, std::memory_order_release);
                data->m_Ui->GetDispatcher().Wake();
            });
        ++m_Revision;
        Actions();
        Notify();
    }

    void AdvanceClose()
    {
        const auto batch = m_Batch;
        if (!batch || !Live() || !batch->m_Prompt.empty() || !batch->m_Waiting.empty())
        {
            return;
        }
        if (!batch->m_Prepared)
        {
            batch->m_Prepared = true;
            DeactivateTargets(batch);
        }
        if (!Live() || m_Batch != batch)
        {
            return;
        }
        if (batch->m_All && batch->m_Ids.size() != m_Entries.size())
        {
            Settle(DocumentCloseStatus::Cancelled, "The document set changed during close approval; retry closure.");
            return;
        }
        for (const auto& id : batch->m_Ids)
        {
            const auto found = m_Entries.find(id);
            if (found == m_Entries.end())
            {
                Settle(DocumentCloseStatus::Cancelled, "A requested document no longer exists.");
                return;
            }
            const auto& entry = found->second;
            if (entry->m_Saving)
            {
                batch->m_Waiting = id;
                return;
            }
            const auto stamp = Capture(*entry);
            const auto approval = batch->m_Approved.find(id);
            if (approval != batch->m_Approved.end() && approval->second == stamp)
            {
                continue;
            }
            if (Modified(*entry))
            {
                Prompt(batch, entry);
                return;
            }
            batch->m_Approved[id] = stamp;
        }
        // End active input/composition before the final value comparison. Removing
        // views must not trigger an unobserved final edit from focus/capture loss.
        DeactivateTargets(batch);
        if (!Live() || m_Batch != batch)
        {
            return;
        }
        for (const auto& id : batch->m_Ids)
        {
            if (batch->m_Approved.at(id) != Capture(*Find(id)))
            {
                m_Ready.store(true, std::memory_order_release);
                m_Ui->GetDispatcher().Wake();
                return;
            }
        }
        Mutate(
            [&]
            {
                for (const auto& id : batch->m_Ids)
                {
                    Remove(Find(id));
                    if (!Live())
                    {
                        return;
                    }
                }
            });
        if (Live() && m_Batch == batch)
        {
            Settle(DocumentCloseStatus::Closed);
        }
    }

    void Pump()
    {
        if (!Live() || m_Pumping || !m_Ready.exchange(false, std::memory_order_acq_rel))
        {
            return;
        }
        const auto keepAlive = shared_from_this();

        struct Scope
        {
            bool& m_Flag;

            ~Scope()
            {
                m_Flag = false;
            }
        } scope{m_Pumping};

        m_Pumping = true;
        try
        {
            std::vector<std::shared_ptr<Entry>> entries;
            for (const auto& [id, entry] : m_Entries)
            {
                (void)id;
                if (entry->m_Saving)
                {
                    entries.push_back(entry);
                }
            }
            for (const auto& entry : entries)
            {
                auto result = entry->m_Save.TakeResult();
                if (!result)
                {
                    continue;
                }
                entry->m_Saving = false;
                entry->m_SaveError = result->m_Status == DocumentSaveStatus::Failed
                                         ? (result->m_Message.empty() ? "Saving failed." : result->m_Message)
                                         : std::string{};
                const auto batch = m_Batch;
                if (result->m_Status == DocumentSaveStatus::Saved)
                {
                    if (entry->m_Options.m_History)
                    {
                        entry->m_Options.m_History->MarkSaved(entry->m_Save.GetCheckpoint());
                    }
                    if (!Live())
                    {
                        return;
                    }
                    if (entry->m_SaveEpoch == entry->m_Epoch)
                    {
                        entry->m_Modified = false;
                    }
                }
                Label(entry);
                ++m_Revision;
                Actions();
                Notify();
                if (!Live())
                {
                    return;
                }
                if (batch && m_Batch == batch && batch->m_Waiting == entry->m_Options.m_Id)
                {
                    batch->m_Waiting.clear();
                    if (result->m_Status != DocumentSaveStatus::Saved)
                    {
                        Settle(result->m_Status == DocumentSaveStatus::Cancelled ? DocumentCloseStatus::Cancelled
                                                                                 : DocumentCloseStatus::Failed,
                               result->m_Message);
                    }
                }
            }
            if (Live())
            {
                AdvanceClose();
                std::erase_if(m_Retired, [](const auto& host) { return host->IsClosed(); });
            }
        }
        catch (...)
        {
            m_CallbackError = std::current_exception();
            if (m_Batch)
            {
                Settle(DocumentCloseStatus::Failed,
                       "Document close could not complete; remaining views were retained.");
            }
        }
    }

    DocumentCloseSession BeginClose(std::vector<std::string> ids, bool all)
    {
        Check();
        if (m_Batch)
        {
            auto requested = ids, pending = m_Batch->m_Ids;
            std::sort(requested.begin(), requested.end());
            std::sort(pending.begin(), pending.end());
            if (requested == pending)
            {
                return m_Batch->m_Session;
            }
            auto rejected = DocumentCloseSession::Create();
            rejected.Complete({DocumentCloseStatus::Failed, std::move(ids), "Another close workflow is pending."});
            return rejected;
        }
        auto batch = std::make_shared<CloseBatch>();
        batch->m_Ids = std::move(ids);
        batch->m_All = all;
        batch->m_Session = DocumentCloseSession::Create();
        if (batch->m_Ids.empty())
        {
            batch->m_Session.Complete({DocumentCloseStatus::Closed, {}});
            return batch->m_Session;
        }
        for (const auto& id : batch->m_Ids)
        {
            (void)Find(id);
        }
        m_Batch = batch;
        m_Ready.store(true, std::memory_order_release);
        m_Ui->GetDispatcher().Wake();
        ++m_Revision;
        Actions();
        Notify();
        return batch->m_Session;
    }

    void Shutdown()
    {
        if (m_Closing)
        {
            return;
        }
        m_Closing = true;
        m_Connections.clear();
        const auto batch = std::exchange(m_Batch, {});
        for (const auto& [id, entry] : m_Entries)
        {
            (void)id;
            entry->m_Save.Expire();
            entry->m_Connections.clear();
            entry->m_Removing = true;
            if (entry->m_Window)
            {
                entry->m_Window->Close();
            }
        }
        if (batch)
        {
            batch->m_Session.Complete({DocumentCloseStatus::OwnerClosed, batch->m_Ids});
        }
    }
};

DocumentWorkspace::DocumentWorkspace() : Widget("documentworkspace"), m_Impl(std::make_shared<Impl>())
{
    SetWidth(SizeRule::Fill());
    SetHeight(SizeRule::Fill());
    SetGap(0);
    SetStyleProperty(StyleProperty::Padding, Insets{});
    auto& initial = *m_Impl;
    initial.m_Self = WidgetRef(this);
    initial.m_Tabs = &Add<TabWidget>();
    initial.m_Tabs->AddClass("document-tabs");
    initial.m_Tabs->SetHeight(SizeRule::Fill());
    initial.m_Tabs->SetTabsClosable(true);
    initial.m_Tabs->SetTabsMovable(true);
    const auto weak = std::weak_ptr<Impl>(m_Impl);
    initial.m_Tabs->CurrentChanged().Connect(
        [weak](int index)
        {
            const auto live = weak.lock();
            if (!live || !live->Live() || live->m_Operating)
            {
                return;
            }
            if (const auto entry = live->At(index))
            {
                live->m_Current = entry->m_Options.m_Id;
                ++live->m_Revision;
                live->Actions();
                live->Notify();
            }
        });
    initial.m_Tabs->TabsReordered().Connect(
        [weak](int, int)
        {
            if (const auto live = weak.lock(); live && live->Live() && !live->m_Operating)
            {
                ++live->m_Revision;
                live->Notify();
            }
        });
    initial.m_Tabs->CloseRequested().Connect(
        [weak](int index)
        {
            if (const auto live = weak.lock(); live && live->Live())
            {
                if (const auto entry = live->At(index))
                {
                    live->BeginClose({entry->m_Options.m_Id}, false);
                }
            }
        });
    initial.m_Tabs->DetachRequested().Connect(
        [weak](int index, Point point)
        {
            const auto live = weak.lock();
            if (!live || !live->Live() || !live->m_Owner)
            {
                return;
            }
            const auto entry = live->At(index);
            if (!entry)
            {
                return;
            }
            const auto metrics = live->m_Owner->Platform().GetWindowMetrics();
            const auto bounds = live->m_Owner->GetWindowBounds();
            const auto screen = metrics.m_HasScreenCoordinates ? metrics.ToScreen(point)
                                                               : Point{bounds.m_X + point.m_X, bounds.m_Y + point.m_Y};
            const auto id = entry->m_Options.m_Id;
            live->m_Ui->Defer(
                [weak, id, screen](UiContext&)
                {
                    if (const auto owner = weak.lock(); owner && owner->Live())
                    {
                        owner->Check().DetachDocument(id, Rect{screen.m_X - 80, screen.m_Y - 20, 900, 650});
                    }
                });
        });
    initial.m_Tabs->TabContextMenuRequested().Connect(
        [weak](int index, Point point)
        {
            const auto data = weak.lock();
            if (!data || !data->Live() || data->m_Batch)
            {
                return;
            }
            const auto entry = data->At(index);
            if (!entry)
            {
                return;
            }
            if (auto* old = data->m_Menu.Get())
            {
                data->m_Ui->Root().RemoveChild(*old);
            }
            auto menu = std::make_unique<ContextMenu>();
            const auto id = entry->m_Options.m_Id;
            auto& popup = static_cast<ContextMenu&>(data->m_Ui->AddOwnedPopup(std::move(menu), data->Check()));
            data->m_Menu = WidgetRef(&popup);
            popup.AddAction("Close document")
                .Clicked()
                .Connect(
                    [weak, id]
                    {
                        if (const auto live = weak.lock(); live && live->Live())
                        {
                            live->BeginClose({id}, false);
                        }
                    });
            popup.AddAction("Close all")
                .Clicked()
                .Connect(
                    [weak]
                    {
                        if (const auto live = weak.lock(); live && live->Live())
                        {
                            live->BeginClose(live->Ids(), true);
                        }
                    });
            popup.AddAction("Detach document", data->m_Owner != nullptr)
                .Clicked()
                .Connect(
                    [weak, id]
                    {
                        if (const auto live = weak.lock(); live && live->Live())
                        {
                            live->m_Ui->Defer(
                                [weak, id](UiContext&)
                                {
                                    if (const auto target = weak.lock(); target && target->Live())
                                    {
                                        target->Check().DetachDocument(id);
                                    }
                                });
                        }
                    });
            popup.Open(point);
        });
}

DocumentWorkspace::DocumentWorkspace(Window& owner) : DocumentWorkspace()
{
    owner.VerifyAccess();
    m_Impl->m_Owner = &owner;
    m_Impl->m_WindowTarget = owner.GetHandle();
    m_Impl->m_Tabs->SetTabsDetachable(true);
}

DocumentWorkspace::~DocumentWorkspace()
{
    m_Impl->Shutdown();
}

void DocumentWorkspace::Initialize()
{
    const auto data = m_Impl;
    data->Check();
    if (data->m_Initialized)
    {
        return;
    }
    data->m_Ui = GetContext();
    if (data->m_Owner && &data->m_Owner->Ui() != data->m_Ui)
    {
        throw std::invalid_argument("A document workspace must be attached to its supplied Window.");
    }
    data->m_Initialized = true;
    const auto weak = std::weak_ptr<Impl>(data);
    data->m_Connections.emplace_back(data->m_Ui->IdleTurn().Connect(
        [weak]
        {
            if (const auto live = weak.lock())
            {
                live->Pump();
            }
        }));
    data->m_Undo = data->m_Ui->CreateAction("Undo");
    data->m_Redo = data->m_Ui->CreateAction("Redo");
    data->m_Save = data->m_Ui->CreateAction("Save");
    data->m_Close = data->m_Ui->CreateAction("Close document");
    data->m_Undo->SetShortcut({Key::Z, ShortcutModifier::Primary});
    data->m_Redo->SetShortcuts(
        {{Key::Y, ShortcutModifier::Primary}, {Key::Z, ShortcutModifier::Primary | ShortcutModifier::Shift}});
    data->m_Save->SetShortcut({Key::S, ShortcutModifier::Primary});
    data->m_Close->SetShortcut({Key::W, ShortcutModifier::Primary});
    for (const auto& action : {data->m_Undo, data->m_Redo, data->m_Save, data->m_Close})
    {
        AddAction(action);
    }
    const auto active = [weak]() -> std::pair<std::shared_ptr<Impl>, std::shared_ptr<Impl::Entry>>
    {
        const auto live = weak.lock();
        if (!live || !live->Live())
        {
            return {};
        }
        const auto found = live->m_Entries.find(live->m_Current);
        return {live, found == live->m_Entries.end() ? nullptr : found->second};
    };
    data->m_Undo->Triggered().Connect(
        [active]
        {
            const auto [live, entry] = active();
            if (entry && entry->m_Options.m_History)
            {
                entry->m_Options.m_History->Undo();
            }
        });
    data->m_Redo->Triggered().Connect(
        [active]
        {
            const auto [live, entry] = active();
            if (entry && entry->m_Options.m_History)
            {
                entry->m_Options.m_History->Redo();
            }
        });
    data->m_Save->Triggered().Connect(
        [active]
        {
            const auto [live, entry] = active();
            if (entry)
            {
                live->Save(entry);
            }
        });
    data->m_Close->Triggered().Connect(
        [active]
        {
            const auto [live, entry] = active();
            if (entry)
            {
                live->BeginClose({entry->m_Options.m_Id}, false);
            }
        });
    if (data->m_Owner)
    {
        data->m_Connections.emplace_back(data->m_Owner->CloseRequested().Connect(
            [weak](WindowCloseRequest& request)
            {
                const auto coordinator = weak.lock();
                if (!coordinator || !coordinator->Live() || coordinator->m_Entries.empty())
                {
                    return;
                }
                request.Cancel();
                if (coordinator->m_WaitingOwnerClose)
                {
                    return;
                }
                auto session = coordinator->BeginClose(coordinator->Ids(), true);
                coordinator->m_WaitingOwnerClose = true;
                session.OnFinished(
                    [weak](DocumentCloseResult result)
                    {
                        const auto target = weak.lock();
                        if (!target)
                        {
                            return;
                        }
                        target->m_WaitingOwnerClose = false;
                        if (result.m_Status == DocumentCloseStatus::Closed && target->Live() &&
                            target->m_Entries.empty())
                        {
                            target->m_Owner->Close();
                        }
                    });
            }));
        data->m_Connections.emplace_back(data->m_Owner->Closed().Connect(
            [weak]
            {
                if (const auto live = weak.lock())
                {
                    live->Shutdown();
                }
            }));
        data->m_Connections.emplace_back(data->m_Owner->FramePreparing().Connect(
            [weak]
            {
                const auto live = weak.lock();
                if (!live || !live->Live())
                {
                    return;
                }
                const auto revision = live->m_Ui->GetStyleSheetRevision();
                if (live->m_ThemeRevision == revision)
                {
                    return;
                }
                live->m_ThemeRevision = revision;
                for (const auto& [id, entry] : live->m_Entries)
                {
                    (void)id;
                    if (entry->m_Window && !entry->m_Window->IsClosed() && entry->m_StyleRevision != revision)
                    {
                        entry->m_Window->Ui().CopyStyleSheetFrom(*live->m_Ui);
                        entry->m_StyleRevision = revision;
                    }
                }
            }));
    }
    data->Actions();
}

Widget& DocumentWorkspace::AddDocument(DocumentOptions options)
{
    Initialize();
    ValidateText(options.m_Id, 256, "Document identity");
    ValidateText(options.m_Title, 1024, "Document caption");
    const auto data = m_Impl;
    if (data->m_Entries.contains(options.m_Id))
    {
        throw std::invalid_argument("A document with that stable identity is already open.");
    }
    if (data->m_Entries.size() >= 128)
    {
        throw std::length_error("A workspace supports at most 128 actual document views.");
    }
    auto entry = std::make_shared<Impl::Entry>();
    entry->m_Options = std::move(options);
    WidgetRef content;
    data->Mutate(
        [&]
        {
            auto& page = data->m_Tabs->AddTab(entry->m_Options.m_Title);
            entry->m_Page = WidgetRef(&page);
            auto& view = page.Add<DocumentContent>();
            entry->m_Content = WidgetRef(&view);
            content = entry->m_Content;
            const auto weak = std::weak_ptr<Impl>(data);
            const auto id = entry->m_Options.m_Id;
            view.m_Activated = [weak, id]
            {
                if (const auto live = weak.lock();
                    live && live->Live() && !live->m_Operating && live->m_Entries.contains(id))
                {
                    live->m_Current = id;
                    ++live->m_Revision;
                    live->Actions();
                    live->Notify();
                }
            };
            data->m_Entries.emplace(id, entry);
            data->m_Insertion.push_back(id);
            if (entry->m_Options.m_History)
            {
                entry->m_Connections.emplace_back(entry->m_Options.m_History->Subscribe(
                    [weak, id](HistoryState)
                    {
                        if (const auto live = weak.lock(); live && live->Live())
                        {
                            if (const auto found = live->m_Entries.find(id); found != live->m_Entries.end())
                            {
                                live->Label(found->second);
                                ++live->m_Revision;
                                live->Actions();
                                live->Notify();
                            }
                        }
                    }));
            }
            data->Select(entry, false);
            data->Label(entry);
        });
    if (!content.Get())
    {
        throw std::logic_error("A document-added observer removed the new view.");
    }
    return *content.Get();
}

Widget& DocumentWorkspace::Content(std::string_view id) const
{
    const auto data = m_Impl;
    data->Check();
    auto* content = data->Find(id)->m_Content.Get();
    if (!content)
    {
        throw std::logic_error("Document views must be removed through their workspace.");
    }
    return *content;
}

std::vector<std::string> DocumentWorkspace::GetDocumentIds() const
{
    VerifyAccess();
    return m_Impl->Ids();
}

DocumentInfo DocumentWorkspace::GetDocumentInfo(std::string_view id) const
{
    VerifyAccess();
    const auto entry = m_Impl->Find(id);
    return {entry->m_Options.m_Id, entry->m_Options.m_Title,   m_Impl->Modified(*entry),
            entry->m_Saving,       entry->m_Window != nullptr, entry->m_SaveError};
}

void DocumentWorkspace::SetDocumentTitle(std::string_view id, std::string title)
{
    VerifyStructuralMutation();
    ValidateText(title, 1024, "Document caption");
    const auto data = m_Impl;
    const auto entry = data->Find(id);
    data->Mutate(
        [&]
        {
            entry->m_Options.m_Title = std::move(title);
            data->Label(entry);
        });
}

void DocumentWorkspace::SetDocumentModified(std::string_view id, bool modified)
{
    VerifyStructuralMutation();
    const auto data = m_Impl;
    const auto entry = data->Find(id);
    data->Mutate(
        [&]
        {
            if (modified)
            {
                ++entry->m_Epoch;
            }
            entry->m_Modified = modified;
            data->Label(entry);
        });
}

void DocumentWorkspace::SetSaveHandler(std::string_view id, std::function<void(DocumentSaveRequest)> handler)
{
    VerifyStructuralMutation();
    const auto data = m_Impl;
    const auto entry = data->Find(id);
    data->Mutate([&] { entry->m_Options.m_Save = std::move(handler); });
}

void DocumentWorkspace::SetCurrentDocument(std::string_view id)
{
    VerifyStructuralMutation();
    const auto data = m_Impl;
    const auto entry = data->Find(id);
    data->Mutate([&] { data->Select(entry, true); });
}

std::string DocumentWorkspace::GetCurrentDocument() const
{
    VerifyAccess();
    return m_Impl->m_Current;
}

void DocumentWorkspace::MoveDocument(std::string_view id, int destination)
{
    VerifyStructuralMutation();
    const auto data = m_Impl;
    const auto entry = data->Find(id);
    data->Mutate(
        [&]
        {
            const int from = data->Index(*entry);
            if (from < 0)
            {
                throw std::logic_error("Attach a detached document before reordering its tab.");
            }
            data->m_Tabs->MoveTab(from, destination);
        });
}

Window& DocumentWorkspace::DetachDocument(std::string_view id, std::optional<Rect> desktopBounds)
{
    VerifyStructuralMutation();
    const auto data = m_Impl;
    auto entry = data->Find(id);
    if (!data->m_Owner)
    {
        throw std::logic_error("Native document detach requires a DocumentWorkspace constructed with its Window.");
    }
    if (data->m_Batch)
    {
        throw std::logic_error("Finish or cancel close approval before transferring document ownership.");
    }
    data->Mutate(
        [&]
        {
            if (entry->m_Window)
            {
                return;
            }
            const int index = data->Index(*entry);
            if (index < 0 || !entry->m_Content.Get())
            {
                throw std::logic_error("The document's persistent tab content has expired.");
            }
            const auto parentBounds = data->m_Owner->GetWindowBounds();
            const auto bounds = desktopBounds.value_or(Rect{parentBounds.m_X + 60, parentBounds.m_Y + 60, 900, 650});
            if (!std::isfinite(bounds.m_X) || !std::isfinite(bounds.m_Y) || !std::isfinite(bounds.m_Width) ||
                !std::isfinite(bounds.m_Height) || bounds.m_Width < 160 || bounds.m_Height < 120 ||
                bounds.m_Width > 16384 || bounds.m_Height > 16384)
            {
                throw std::invalid_argument("Detached bounds require finite desktop size 160..16384 by 120..16384.");
            }
            WindowOptions options;
            options.m_Title = entry->m_Options.m_Title;
            options.m_Width = static_cast<int>(bounds.m_Width);
            options.m_Height = static_cast<int>(bounds.m_Height);
            auto host = data->m_Owner->CreateOwnedWindow(options);
            data->Check();
            host->SetWindowBounds(bounds);
            data->Check();
            auto& shell = host->Ui().Root().Add<MainWindow>(*host);
            auto& tools = shell.AddToolBar();
            auto& attach = tools.AddButton("Attach document");
            const auto weak = std::weak_ptr<Impl>(data);
            const auto identity = entry->m_Options.m_Id;
            attach.Clicked().Connect(
                [weak, identity]
                {
                    if (const auto live = weak.lock(); live && live->Live())
                    {
                        live->m_Ui->Defer(
                            [weak, identity](UiContext&)
                            {
                                if (const auto owner = weak.lock();
                                    owner && owner->Live() && !owner->m_Batch && owner->m_Entries.contains(identity))
                                {
                                    owner->Check().AttachDocument(identity);
                                }
                            });
                    }
                });
            for (int command = 0; command < 4; ++command)
            {
                auto action = host->Ui().CreateAction(command == 0   ? "Undo"
                                                      : command == 1 ? "Redo"
                                                      : command == 2 ? "Save"
                                                                     : "Close document");
                action->SetShortcut({command == 0   ? Key::Z
                                     : command == 1 ? Key::Y
                                     : command == 2 ? Key::S
                                                    : Key::W,
                                     ShortcutModifier::Primary});
                shell.AddAction(action);
                tools.AddAction(action);
                action->Triggered().Connect(
                    [weak, identity, command]
                    {
                        if (const auto live = weak.lock(); live && live->Live())
                        {
                            const auto item = live->Find(identity);
                            if (command == 0 && item->m_Options.m_History)
                            {
                                item->m_Options.m_History->Undo();
                            }
                            else if (command == 1 && item->m_Options.m_History)
                            {
                                item->m_Options.m_History->Redo();
                            }
                            else if (command == 2)
                            {
                                live->Save(item);
                            }
                            else if (command == 3)
                            {
                                live->BeginClose({identity}, false);
                            }
                        }
                    });
                entry->m_WindowActions.push_back(action);
            }
            host->CloseRequested().Connect(
                [weak, identity](WindowCloseRequest& request)
                {
                    if (const auto live = weak.lock(); live && live->Live())
                    {
                        if (const auto found = live->m_Entries.find(identity);
                            found != live->m_Entries.end() && found->second->m_Window && !found->second->m_Removing)
                        {
                            request.Cancel();
                            live->BeginClose({identity}, false);
                        }
                    }
                });
            entry->m_Content.Get()->ReparentTo(shell.Central());
            data->Check();
            data->m_Tabs->RemoveTab(index);
            entry->m_Page = {};
            entry->m_Window = std::move(host);
            entry->m_StyleRevision = data->m_Ui->GetStyleSheetRevision();
            data->m_Current = identity;
            data->Label(entry);
        });
    if (!entry->m_Window)
    {
        throw std::logic_error("A detach observer removed the document host.");
    }
    return *entry->m_Window;
}

void DocumentWorkspace::AttachDocument(std::string_view id, int index)
{
    VerifyStructuralMutation();
    const auto data = m_Impl;
    const auto entry = data->Find(id);
    if (data->m_Batch)
    {
        throw std::logic_error("Finish or cancel close approval before transferring document ownership.");
    }
    data->Mutate(
        [&]
        {
            if (!entry->m_Window)
            {
                return;
            }
            if (index < -1 || index > data->m_Tabs->GetTabCount())
            {
                throw std::out_of_range("Invalid attached document insertion index.");
            }
            auto& page = data->m_Tabs->AddTab(entry->m_Options.m_Title);
            entry->m_Content.Get()->ReparentTo(page);
            data->Check();
            entry->m_Page = WidgetRef(&page);
            const int last = data->m_Tabs->GetTabCount() - 1;
            if (index >= 0 && index < last)
            {
                data->m_Tabs->MoveTab(last, index);
            }
            data->Retire(entry);
            data->Select(entry, false);
            data->Label(entry);
            data->m_Ready.store(true, std::memory_order_release);
        });
}

Window* DocumentWorkspace::GetFloatingWindow(std::string_view id) const
{
    VerifyAccess();
    return m_Impl->Find(id)->m_Window.get();
}

DocumentSaveRequest DocumentWorkspace::SaveDocument(std::string_view id)
{
    Initialize();
    const auto data = m_Impl;
    return data->Save(data->Find(id));
}

DocumentCloseSession DocumentWorkspace::RequestClose(std::string_view id)
{
    Initialize();
    const auto data = m_Impl;
    const auto entry = data->Find(id);
    return data->BeginClose({entry->m_Options.m_Id}, false);
}

DocumentCloseSession DocumentWorkspace::RequestCloseAll()
{
    Initialize();
    const auto data = m_Impl;
    return data->BeginClose(data->Ids(), true);
}

void DocumentWorkspace::CancelClose()
{
    const auto data = m_Impl;
    data->Check();
    data->Cancel();
}

std::shared_ptr<Action> DocumentWorkspace::UndoAction()
{
    Initialize();
    return m_Impl->m_Undo;
}

std::shared_ptr<Action> DocumentWorkspace::RedoAction()
{
    Initialize();
    return m_Impl->m_Redo;
}

std::shared_ptr<Action> DocumentWorkspace::SaveAction()
{
    Initialize();
    return m_Impl->m_Save;
}

std::shared_ptr<Action> DocumentWorkspace::CloseAction()
{
    Initialize();
    return m_Impl->m_Close;
}

const TabWidget& DocumentWorkspace::Tabs() const
{
    VerifyAccess();
    return *m_Impl->m_Tabs;
}

std::exception_ptr DocumentWorkspace::GetCallbackError() const
{
    VerifyAccess();
    return m_Impl->m_CallbackError;
}

Signal<>& DocumentWorkspace::Changed()
{
    VerifyAccess();
    return m_Impl->m_Changed;
}

Signal<std::string>& DocumentWorkspace::CurrentChanged()
{
    VerifyAccess();
    return m_Impl->m_CurrentChanged;
}
} // namespace EStudioUI
