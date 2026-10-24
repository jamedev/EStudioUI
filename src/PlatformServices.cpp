#include <EStudioUI/PlatformServices.h>
#include <EStudioUI/InputEvent.h>
#include <EStudioUI/ThreadAffinity.h>

#include <algorithm>
#include <atomic>
#include <mutex>
#include <stdexcept>

namespace EStudioUI
{
namespace
{
PlatformResult Closed()
{
    return {PlatformStatus::Closed, "The platform-service owner has closed."};
}

PlatformResult Unsupported()
{
    return {PlatformStatus::Unsupported, "This operation is unavailable on the current platform adapter."};
}

void CanonicalFormats(ClipboardData& data)
{
    const bool needsCaseFold = std::any_of(
        data.m_Custom.begin(), data.m_Custom.end(), [](const auto& entry)
        { return std::any_of(entry.first.begin(), entry.first.end(), [](char c) { return c >= 'A' && c <= 'Z'; }); });
    if (!needsCaseFold)
        return;
    decltype(data.m_Custom) canonical;
    for (auto& [name, bytes] : data.m_Custom)
    {
        auto folded = name;
        for (auto& c : folded)
            if (c >= 'A' && c <= 'Z')
                c = static_cast<char>(c + ('a' - 'A'));
        canonical.emplace(std::move(folded), std::move(bytes));
    }
    data.m_Custom = std::move(canonical);
}

std::string CanonicalText(std::string_view text)
{
    std::string result;
    result.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i)
    {
        if (text[i] == '\r')
        {
            result += '\n';
            if (i + 1 < text.size() && text[i + 1] == '\n')
                ++i;
        }
        else
            result += text[i];
    }
    return result;
}
} // namespace

struct PlatformServices::Data : std::enable_shared_from_this<Data>
{
    struct Slot
    {
        std::mutex m_Mutex;
        std::optional<FileDialogResult> m_Result;
    };

    struct Pending
    {
        FileDialogSession m_Session;
        std::shared_ptr<Slot> m_Slot;
        bool m_Multiple = false;
    };

    ThreadAffinity m_Affinity;
    PlatformHost m_Host;
    std::function<void()> m_Guard;
    std::atomic<bool> m_Closed = false;
    PlatformResult m_LastResult;
    std::vector<Pending> m_Pending;
    bool m_Pumping = false;
    bool m_ServicePumping = false;
    Signal<const SystemAppearance&> m_AppearanceChanged;
    const std::string m_PublicationKey = "platform-results:" + std::to_string(UiDispatcher::AllocateTargetKey());

    explicit Data(PlatformHost host) : m_Host(std::move(host))
    {
        m_Host.m_Dispatcher.VerifyAccess();
    }

    void Verify(bool operation = false) const
    {
        m_Affinity.VerifyAccess("Platform-service access");
        if (operation && m_Guard)
            m_Guard();
    }

    template <typename Operation> PlatformResult InvokeHost(Operation operation)
    {
        try
        {
            return m_LastResult = operation();
        }
        catch (const std::exception& error)
        {
            return m_LastResult = {PlatformStatus::Failed, error.what()};
        }
        catch (...)
        {
            return m_LastResult = {PlatformStatus::Failed, "The native window operation failed."};
        }
    }

    void Pump()
    {
        Verify(true);
        if (Detail::IsDeliveringInput())
            throw std::logic_error("Native results cannot be pumped during input; use the ordinary owner turn.");
        if (m_Pumping)
            throw std::logic_error("Native result delivery cannot be pumped recursively.");

        struct Scope
        {
            bool& m_Active;

            explicit Scope(bool& active) : m_Active(active)
            {
                m_Active = true;
            }

            ~Scope()
            {
                m_Active = false;
            }
        } pumping(m_Pumping);

        const auto ownerLease = m_Host.m_OwnerLifetime.lock();
        if (m_Host.m_Dispatcher.IsClosed() && !m_Closed.exchange(true))
        {
            const auto pending = std::move(m_Pending);
            for (const auto& entry : pending)
                entry.m_Session.Complete({FileDialogStatus::OwnerClosed});
            return;
        }
        std::vector<std::pair<FileDialogSession, FileDialogResult>> ready;
        std::erase_if(m_Pending,
                      [&](Pending& pending)
                      {
                          if (!pending.m_Session.IsPending())
                              return true;
                          const std::lock_guard lock(pending.m_Slot->m_Mutex);
                          if (!pending.m_Slot->m_Result)
                              return false;
                          auto result = std::move(*pending.m_Slot->m_Result);
                          if (!pending.m_Multiple && result.m_Paths.size() > 1)
                              result = {
                                  FileDialogStatus::Failed, {}, "A single-selection picker returned several paths."};
                          ready.emplace_back(pending.m_Session, std::move(result));
                          return true;
                      });
        for (auto& [session, result] : ready)
            session.Complete(m_Closed ? FileDialogResult{FileDialogStatus::OwnerClosed} : std::move(result));
    }
};

PlatformServices::PlatformServices(PlatformHost host) : m_Data(std::make_shared<Data>(std::move(host)))
{
}

PlatformServices::~PlatformServices()
{
    if (!m_Data->m_Affinity.IsOwnerThread())
        std::terminate();
    Close();
}

void PlatformServices::SetOperationGuard(std::function<void()> guard, std::weak_ptr<void> owner)
{
    m_Data->Verify();
    m_Data->m_Guard = std::move(guard);
    m_Data->m_Host.m_OwnerLifetime = std::move(owner);
}

void PlatformServices::RecordNativeResult(PlatformResult result)
{
    m_Data->Verify();
    m_Data->m_LastResult = std::move(result);
}

PlatformCapabilities PlatformServices::GetCapabilities() const
{
    const auto data = m_Data;
    data->Verify();
    auto result = data->m_Host.m_Capabilities;
    const auto adapter = DescribeCapabilities();
    result.m_Adapter = adapter.m_Adapter;
    for (std::size_t i = 0; i < result.m_Features.size(); ++i)
        if (adapter.m_Features[i] != PlatformSupport::Unsupported)
            result.m_Features[i] = adapter.m_Features[i];
    return result;
}

ClipboardResult PlatformServices::ReadClipboard()
{
    const auto data = m_Data;
    data->Verify(true);
    const auto ownerLease = data->m_Host.m_OwnerLifetime.lock();
    ClipboardResult result;
    try
    {
        result = data->m_Closed || data->m_Host.m_Dispatcher.IsClosed() ? ClipboardResult{Closed(), {}}
                                                                        : ReadClipboardCore();
        if (result.m_Result)
        {
            result.m_Data.Validate();
            CanonicalFormats(result.m_Data);
            if (result.m_Data.m_Text)
                result.m_Data.m_Text = CanonicalText(*result.m_Data.m_Text);
        }
    }
    catch (const std::exception& error)
    {
        result = {{PlatformStatus::Failed, error.what()}, {}};
    }
    data->m_LastResult = result.m_Result;
    return result;
}

PlatformResult PlatformServices::WriteClipboard(const ClipboardData& content)
{
    const auto data = m_Data;
    data->Verify(true);
    const auto ownerLease = data->m_Host.m_OwnerLifetime.lock();
    if (data->m_Closed || data->m_Host.m_Dispatcher.IsClosed())
        return data->m_LastResult = Closed();
    content.Validate();
    auto prepared = content;
    CanonicalFormats(prepared);
    if (prepared.m_Text)
        prepared.m_Text = CanonicalText(*prepared.m_Text);
    try
    {
        return data->m_LastResult = WriteClipboardCore(prepared);
    }
    catch (const std::exception& error)
    {
        return data->m_LastResult = {PlatformStatus::Failed, error.what()};
    }
}

PlatformResult PlatformServices::SetClipboardText(std::string text)
{
    ClipboardData content;
    content.m_Text = std::move(text);
    return WriteClipboard(content);
}

FileDialogSession PlatformServices::ShowFileDialog(FileDialogOptions options)
{
    const auto data = m_Data;
    data->Verify(true);
    const auto ownerLease = data->m_Host.m_OwnerLifetime.lock();
    auto session = FileDialogSession::Create();
    if (data->m_Closed || data->m_Host.m_Dispatcher.IsClosed())
    {
        session.Complete({FileDialogStatus::OwnerClosed});
        return session;
    }
    options.Validate();
    std::erase_if(data->m_Pending, [](const auto& pending) { return !pending.m_Session.IsPending(); });
    if (data->m_Pending.size() >= 8)
    {
        session.Complete({FileDialogStatus::Busy, {}, "The adapter already has eight pending picker sessions."});
        return session;
    }
    const auto slot = std::make_shared<Data::Slot>();
    data->m_Pending.push_back({session, slot, options.m_Multiple});
    const std::weak_ptr<Data> weak = data;
    const auto complete = [weak, slot](FileDialogResult result)
    {
        const auto live = weak.lock();
        if (!live || live->m_Closed.load())
            return;
        {
            const std::lock_guard lock(slot->m_Mutex);
            if (slot->m_Result)
                return;
            slot->m_Result = std::move(result);
        }
        // A completion owns a reserved bounded slot. The queue record is a wake
        // hint; Pump also visits these slots if producer saturation rejects it.
        try
        {
            live->m_Host.m_Dispatcher.PostLatest(live->m_PublicationKey,
                                                 [weak]
                                                 {
                                                     if (const auto owner = weak.lock())
                                                         owner->Pump();
                                                 });
        }
        catch (...)
        {
            // The reserved slot still owns this result; the native loop's next
            // ordinary Pump delivers it even if allocating the wake hint failed.
        }
    };
    try
    {
        BeginFileDialog(options, session.GetStopToken(), complete);
    }
    catch (const std::exception& error)
    {
        complete({FileDialogStatus::Failed, {}, error.what()});
    }
    catch (...)
    {
        complete({FileDialogStatus::Failed, {}, "The native picker adapter failed."});
    }
    return session;
}

WindowMetrics PlatformServices::GetWindowMetrics() const
{
    const auto data = m_Data;
    data->Verify();
    auto metrics = data->m_Host.m_GetMetrics ? data->m_Host.m_GetMetrics() : WindowMetrics{};
    metrics.Validate();
    return metrics;
}

PlatformResult PlatformServices::SetCursor(CursorShape shape)
{
    const auto data = m_Data;
    data->Verify(true);
    if (shape < CursorShape::Arrow || shape > CursorShape::NotAllowed)
        throw std::invalid_argument("Invalid native cursor shape.");
    const auto ownerLease = data->m_Host.m_OwnerLifetime.lock();
    if (data->m_Closed || data->m_Host.m_Dispatcher.IsClosed())
        return data->m_LastResult = Closed();
    return data->InvokeHost([&] { return data->m_Host.m_SetCursor ? data->m_Host.m_SetCursor(shape) : Unsupported(); });
}

PlatformResult PlatformServices::SetWindowIcon(const WindowIcon& icon)
{
    const auto data = m_Data;
    data->Verify(true);
    icon.Validate();
    const auto ownerLease = data->m_Host.m_OwnerLifetime.lock();
    if (data->m_Closed || data->m_Host.m_Dispatcher.IsClosed())
        return data->m_LastResult = Closed();
    return data->InvokeHost([&] { return data->m_Host.m_SetIcon ? data->m_Host.m_SetIcon(icon) : Unsupported(); });
}

PlatformResult PlatformServices::FocusWindow()
{
    const auto data = m_Data;
    data->Verify(true);
    const auto ownerLease = data->m_Host.m_OwnerLifetime.lock();
    if (data->m_Closed || data->m_Host.m_Dispatcher.IsClosed())
        return data->m_LastResult = Closed();
    return data->InvokeHost([&] { return data->m_Host.m_FocusWindow ? data->m_Host.m_FocusWindow() : Unsupported(); });
}

PlatformResult PlatformServices::GetLastResult() const
{
    m_Data->Verify();
    return m_Data->m_LastResult;
}

UiDispatcher PlatformServices::GetDispatcher() const
{
    m_Data->Verify();
    return m_Data->m_Host.m_Dispatcher;
}

const PlatformHost& PlatformServices::GetHost() const
{
    m_Data->Verify();
    return m_Data->m_Host;
}

void PlatformServices::Pump()
{
    const auto data = m_Data;
    data->Verify(true);
    if (data->m_ServicePumping)
        throw std::logic_error("Platform services cannot be pumped recursively.");
    struct Scope
    {
        bool& m_Active;
        ~Scope() { m_Active = false; }
    } scope{data->m_ServicePumping};
    data->m_ServicePumping = true;
    const auto ownerLease = data->m_Host.m_OwnerLifetime.lock();
    data->Pump();
    if (!data->m_Closed)
        if (auto appearance = PollSystemAppearanceCore())
        {
            appearance->Validate();
            data->m_AppearanceChanged.Emit(*appearance);
        }
}

SystemAppearance PlatformServices::GetSystemAppearance() const
{
    const auto data = m_Data;
    data->Verify(true);
    if (data->m_Closed)
        throw std::logic_error("The appearance service has closed.");
    auto result = ReadSystemAppearanceCore();
    result.Validate();
    return result;
}

Signal<const SystemAppearance&>& PlatformServices::AppearanceChanged()
{
    m_Data->Verify();
    return m_Data->m_AppearanceChanged;
}

SystemAppearance PlatformServices::ReadSystemAppearanceCore() const
{
    return {};
}

std::optional<SystemAppearance> PlatformServices::PollSystemAppearanceCore()
{
    return {};
}

void PlatformServices::Close()
{
    const auto data = m_Data;
    data->Verify();
    const auto ownerLease = data->m_Host.m_OwnerLifetime.lock();
    if (data->m_Closed.exchange(true))
        return;
    Closing();
    const auto pending = std::move(data->m_Pending);
    for (const auto& entry : pending)
        entry.m_Session.Complete({FileDialogStatus::OwnerClosed});
}

bool PlatformServices::IsClosed() const
{
    m_Data->Verify();
    return m_Data->m_Closed.load() || m_Data->m_Host.m_Dispatcher.IsClosed();
}

void PlatformServices::Closing()
{
}

bool PlatformServices::HasPendingNativeWork() const
{
    m_Data->Verify();
    return NativeWorkPending();
}

bool PlatformServices::NativeWorkPending() const
{
    return false;
}
} // namespace EStudioUI
