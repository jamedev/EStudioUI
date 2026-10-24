#include <EStudioUI/TextDocument.h>

#include <stdexcept>
#include <stop_token>

namespace EStudioUI
{
struct TextDocument::Data : std::enable_shared_from_this<Data>
{
    StateStore* m_Store;
    DocumentLimits m_Limits;
    UiDispatcher m_Dispatcher;
    State<DocumentSnapshot> m_State;
    UndoHistory m_History;
    TextDocument* m_Owner;
    std::uint64_t m_Target = UiDispatcher::AllocateTargetKey();
    std::stop_source m_Lifetime;
    std::unique_ptr<std::stop_callback<std::function<void()>>> m_DomainStop;

    Data(StateStore& store, DocumentLimits limits, HistoryLimits historyLimits, TextDocument* owner,
         DocumentSnapshot initial)
        : m_Store(&store), m_Limits(limits), m_Dispatcher(store.GetDispatcher()), m_State(store, std::move(initial)),
          m_History(store, historyLimits), m_Owner(owner)
    {
    }

    DocumentSnapshot Read() const
    {
        // State's weak domain guard diagnoses a destroyed StateStore before any
        // access through our nonowning owner-thread store pointer.
        auto snapshot = m_State.Get();
        if (!m_Owner)
        {
            throw std::logic_error("The text document owner has expired.");
        }
        return snapshot;
    }

    DocumentSnapshot Edit()
    {
        auto snapshot = Read();
        if (m_Store->IsNotifying())
        {
            throw std::logic_error("Document edits during state notifications must be posted through GetHandle().");
        }
        // Preserve StateStore's validation/reconciliation/UI-phase preconditions
        // even when the requested document operation later proves a no-op.
        (void)m_State.Set(snapshot);
        return snapshot;
    }

    void Execute(DocumentSnapshot before, DocumentSnapshot after, std::string label, std::size_t bytes)
    {
        const auto apply = [weak = weak_from_this()](const DocumentSnapshot& value)
        {
            const auto data = weak.lock();
            if (!data)
            {
                throw std::logic_error("The text document command receiver has expired.");
            }
            (void)data->Edit();
            data->m_State.Set(value);
        };
        m_History.Execute(
            std::move(label), [apply, after] { apply(after); }, [apply, before] { apply(before); }, bytes);
    }
};

TextDocument::TextDocument(StateStore& store, DocumentLimits limits, HistoryLimits historyLimits)
    : m_Data(std::make_shared<Data>(store, limits, historyLimits, this, DocumentSnapshot::Create(0, {}, limits)))
{
    const auto endpoint = m_Data->m_State.GetHandle();
    m_Data->m_DomainStop =
        std::make_unique<std::stop_callback<std::function<void()>>>(endpoint.GetTargetStopToken(),
                                                                    [weak = std::weak_ptr<Data>(m_Data)]
                                                                    {
                                                                        if (const auto data = weak.lock())
                                                                        {
                                                                            data->m_Lifetime.request_stop();
                                                                        }
                                                                    });
}

TextDocument::~TextDocument()
{
    if (!m_Data->m_Dispatcher.IsOwnerThread())
    {
        std::terminate();
    }
    m_Data->m_Owner = nullptr;
    m_Data->m_Lifetime.request_stop();
}

DocumentSnapshot TextDocument::GetSnapshot() const
{
    const auto data = m_Data;
    return data->Read();
}

DocumentLimits TextDocument::GetLimits() const
{
    const auto data = m_Data;
    (void)data->Read();
    return data->m_Limits;
}

bool TextDocument::Replace(DocumentPosition first, DocumentPosition last, std::string_view text, std::string label,
                           std::optional<DocumentSnapshot> expected)
{
    const auto data = m_Data;
    const auto before = data->Edit();
    if (expected && *expected != before)
    {
        throw std::invalid_argument("Document replacement was prepared from a stale or foreign snapshot.");
    }
    std::size_t bytes = 0;
    auto next = before.Replace(first, last, text, data->m_Limits, bytes);
    if (next == before)
    {
        return false;
    }
    data->Execute(before, std::move(next), std::move(label), bytes);
    return true;
}

bool TextDocument::SetText(std::string_view text, std::string label)
{
    const auto data = m_Data;
    const auto before = data->Edit();
    const auto last = before.GetParagraphCount() - 1;
    std::size_t bytes = 0;
    auto next =
        before.Replace(before.GetPosition(0), before.GetPosition(last, before.GetParagraph(last)->m_Text.size()), text,
                       data->m_Limits, bytes);
    if (next == before)
    {
        return false;
    }
    data->Execute(before, std::move(next), std::move(label), bytes);
    return true;
}

void TextDocument::Reset(std::string_view text, bool markSaved)
{
    const auto data = m_Data;
    const auto before = data->Edit();
    auto next = DocumentSnapshot::Create(before.GetDocumentId(), text, data->m_Limits);
    data->m_Store->Batch(
        [data, before, next = std::move(next), markSaved]
        {
            // Advance the value position even when loading an unsaved document, then
            // discard command retention. Both candidates roll back on any veto.
            data->Execute(before, next, "Load document", 0);
            data->m_History.Clear(markSaved);
        });
}

UndoHistory& TextDocument::GetHistory()
{
    const auto data = m_Data;
    (void)data->Read();
    return data->m_History;
}

Connection TextDocument::Subscribe(std::function<void(const StateChange<DocumentSnapshot>&)> callback)
{
    const auto data = m_Data;
    (void)data->Read();
    if (!callback)
    {
        throw std::invalid_argument("Document observers must be callable.");
    }
    return data->m_State.Subscribe(
        [weak = std::weak_ptr<Data>(data), callback = std::move(callback)](const StateChange<DocumentSnapshot>& change)
        {
            if (const auto current = weak.lock(); current && current->m_Owner)
            {
                callback(change);
            }
        });
}

Connection TextDocument::AddValidator(std::function<void(const DocumentSnapshot&)> validator)
{
    const auto data = m_Data;
    (void)data->Read();
    return data->m_State.AddValidator(std::move(validator));
}

UiHandle<TextDocument> TextDocument::GetHandle() const
{
    const auto data = m_Data;
    (void)data->Read();
    const auto endpoint = data->m_State.GetHandle();
    return UiHandle<TextDocument>::ForTarget(
        data->m_State.GetDispatcher(), data->m_Target,
        [weak = std::weak_ptr<Data>(data), endpoint]() -> TextDocument*
        {
            const auto current = weak.lock();
            return current && endpoint.IsConnected() ? current->m_Owner : nullptr;
        },
        data->m_Lifetime.get_token());
}
} // namespace EStudioUI
