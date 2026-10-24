#include <EStudioUI/SearchBox.h>
#include <EStudioUI/UiContext.h>
#include <utf8proc.h>
#include <cmath>
#include <cstdlib>
#include <stdexcept>
#include <unordered_set>

namespace EStudioUI
{
namespace
{
std::string Fold(std::string_view text)
{
    utf8proc_uint8_t* output = nullptr;
    const auto size =
        utf8proc_map(reinterpret_cast<const utf8proc_uint8_t*>(text.data()), static_cast<utf8proc_ssize_t>(text.size()),
                     &output, static_cast<utf8proc_option_t>(UTF8PROC_STABLE | UTF8PROC_COMPOSE | UTF8PROC_CASEFOLD));
    const std::unique_ptr<utf8proc_uint8_t, decltype(&std::free)> owner(output, std::free);
    if (size < 0)
    {
        throw std::invalid_argument("Suggestions must contain valid UTF-8.");
    }
    return size ? std::string(reinterpret_cast<const char*>(output), static_cast<std::size_t>(size)) : std::string{};
}
} // namespace

SearchBox::SearchBox() : Widget("searchbox")
{
    SetLayoutDirection(Axis::Horizontal);
    SetGap(0);
    m_Editor = &Add<TextBox>();
    m_Editor->SetPlaceholder("Search");
    m_Editor->SetMaxBytes(4096);
    m_Editor->SetWidth(SizeRule::Fill());
    m_Editor->SetHeight(SizeRule::Fill());
    auto& clear = Add<ToolButton>("×");
    clear.SetAccessibleName("Clear search");
    clear.SetWidth(SizeRule::Fixed(28));
    clear.SetHeight(SizeRule::Fill());
    const WidgetRef self(this);
    clear.Clicked().Connect(
        [self]
        {
            if (auto* live = dynamic_cast<SearchBox*>(self.Get()))
            {
                live->SetText("");
            }
        });
    m_Editor->TextChanged().Connect(
        [self](const std::string&)
        {
            if (auto* live = dynamic_cast<SearchBox*>(self.Get()))
            {
                live->Edited();
            }
        });
    m_Editor->FocusLost().Connect(
        [self]
        {
            if (auto* live = dynamic_cast<SearchBox*>(self.Get()))
            {
                if (auto* popup = live->m_Popup.Get())
                {
                    for (auto* focus = live->GetContext() ? live->GetContext()->GetFocus() : nullptr; focus;
                         focus = focus->GetParent())
                    {
                        if (focus == popup)
                        {
                            live->m_Timer.Cancel();
                            return;
                        }
                    }
                }
                ++live->m_Request;
                live->m_Timer.Cancel();
                if (auto* popup = dynamic_cast<CompletionPopup*>(live->m_Popup.Get()); popup && popup->IsOpen())
                {
                    popup->Close();
                }
            }
        });
}

TextBox& SearchBox::GetEditor() const
{
    VerifyAccess();
    return *m_Editor;
}

void SearchBox::SetText(std::string_view text)
{
    VerifyStructuralMutation();
    m_Editor->SetText(text);
}

const std::string& SearchBox::GetText() const
{
    VerifyAccess();
    return m_Editor->GetText();
}

void SearchBox::SetDebounce(double seconds)
{
    VerifyStructuralMutation();
    if (!std::isfinite(seconds) || seconds < 0 || seconds > 10)
    {
        throw std::invalid_argument("Search debounce must be between zero and ten seconds.");
    }
    m_Debounce = seconds;
}

void SearchBox::SetSuggestions(std::vector<CompletionItem> items)
{
    VerifyStructuralMutation();
    if (items.size() > 4096)
    {
        throw std::length_error("A search catalog accepts at most 4096 suggestions.");
    }
    std::vector<std::pair<std::string, CompletionItem>> catalog;
    std::unordered_set<std::string> ids;
    std::size_t bytes = 0;
    for (auto& item : items)
    {
        bytes += item.m_Id.size() + item.m_Text.size() + item.m_Detail.size();
        if (bytes > 1048576)
        {
            throw std::length_error("A search catalog accepts at most 1 MiB of text.");
        }
        if (item.m_Id.empty() || !ids.insert(item.m_Id).second)
        {
            throw std::invalid_argument("Suggestion IDs must be unique and nonempty.");
        }
        catalog.emplace_back(Fold(item.m_Text), std::move(item));
    }
    m_Catalog = std::move(catalog);
}

void SearchBox::Edited()
{
    const auto lease = RetainOwner();
    const WidgetRef self(this);
    const auto request = ++m_Request;
    m_Timer.Cancel();
    if (auto* popup = dynamic_cast<CompletionPopup*>(m_Popup.Get()); popup && popup->IsOpen())
    {
        popup->Close();
    }
    if (auto* live = dynamic_cast<SearchBox*>(self.Get()); live && live->m_Request == request)
    {
        if (auto* ui = live->GetContext(); ui && !live->m_Accepting)
        {
            live->m_Timer = ui->After(*live, live->m_Debounce, [](SearchBox& receiver) { receiver.Query(); });
        }
    }
}

void SearchBox::Query()
{
    const auto lease = RetainOwner();
    const WidgetRef self(this);
    const auto text = GetText();
    const auto request = m_Request;
    if (!m_Catalog.empty())
    {
        const auto key = Fold(text);
        std::vector<CompletionItem> results;
        std::size_t bytes = 0;
        for (const auto& [folded, item] : m_Catalog)
        {
            const auto size = item.m_Id.size() + item.m_Text.size() + item.m_Detail.size();
            if (folded.find(key) != std::string::npos && results.size() < 256 && bytes + size <= 262144 &&
                m_Editor->CanSetText(item.m_Text))
            {
                bytes += size;
                results.push_back(item);
            }
        }
        SetCompletions(request, std::move(results));
    }
    if (auto* live = dynamic_cast<SearchBox*>(self.Get()); live && live->m_Request == request)
    {
        live->m_QueryRequested.Emit(text, request);
    }
}

CompletionPopup& SearchBox::EnsurePopup()
{
    if (auto* popup = dynamic_cast<CompletionPopup*>(m_Popup.Get()); popup && popup->GetContext() == GetContext())
    {
        return *popup;
    }
    auto& popup = CompletionPopup::Create(*this);
    m_Popup = WidgetRef(&popup);
    const WidgetRef self(this);
    popup.Accepted().Connect(
        [self](CompletionItem item)
        {
            if (auto* live = dynamic_cast<SearchBox*>(self.Get()))
            {
                const auto lease = live->RetainOwner();
                live->m_Accepting = true;
                live->SetText(item.m_Text);
                if ((live = dynamic_cast<SearchBox*>(self.Get())))
                {
                    live->m_Accepting = false;
                    if (live->GetText() == item.m_Text)
                    {
                        live->m_CompletionAccepted.Emit(std::move(item));
                    }
                }
            }
        });
    return popup;
}

bool SearchBox::SetCompletions(std::uint64_t request, std::vector<CompletionItem> items)
{
    VerifyStructuralMutation();
    if (request != m_Request)
    {
        return false;
    }
    auto* ui = GetContext();
    if (!ui || ui->GetFocus() != m_Editor || !IsVisible() || !IsEnabled())
    {
        return false;
    }
    for (const auto& item : items)
    {
        if (!m_Editor->CanSetText(item.m_Text))
        {
            throw std::invalid_argument("Completion text must satisfy the search editor validator and size limit.");
        }
    }
    const WidgetRef self(this);
    auto& popup = EnsurePopup();
    const WidgetRef surface(&popup);
    popup.SetItems(std::move(items));
    if (!self.Get() || !surface.Get() || m_Request != request)
    {
        return false;
    }
    if (popup.GetItems().empty())
    {
        popup.Close();
    }
    else
    {
        const bool wasOpen = popup.IsOpen();
        const auto session = popup.Show(*this);
        if (!wasOpen)
        {
            session.OnFinished(
                [self, surface, request](PopupResult result)
                {
                    if (result.m_Reason == PopupCloseReason::Accepted || result.m_Reason == PopupCloseReason::Closed)
                    {
                        return;
                    }
                    if (auto* live = dynamic_cast<SearchBox*>(self.Get());
                        live && live->m_Request == request && live->m_Popup.Get() == surface.Get())
                    {
                        ++live->m_Request;
                        live->m_Timer.Cancel();
                    }
                });
        }
    }
    return true;
}

std::exception_ptr SearchBox::GetQueryError() const
{
    VerifyAccess();
    return m_Timer.GetError();
}

std::uint64_t SearchBox::GetRequestId() const
{
    VerifyAccess();
    return m_Request;
}

Signal<std::string, std::uint64_t>& SearchBox::QueryRequested()
{
    VerifyAccess();
    return m_QueryRequested;
}

Signal<const std::string&>& SearchBox::TextChanged()
{
    VerifyAccess();
    return m_Editor->TextChanged();
}

Signal<const std::string&>& SearchBox::Submitted()
{
    VerifyAccess();
    return m_Editor->Submitted();
}

Signal<CompletionItem>& SearchBox::CompletionAccepted()
{
    VerifyAccess();
    return m_CompletionAccepted;
}

bool SearchBox::OnKeyPreview(Key key, bool down, bool, Modifiers)
{
    if (!down)
    {
        return false;
    }
    auto* popup = dynamic_cast<CompletionPopup*>(m_Popup.Get());
    if (popup && popup->IsOpen())
    {
        if (key == Key::Down || key == Key::Up)
        {
            popup->Move(key == Key::Down ? 1 : -1);
            return true;
        }
        if (key == Key::Enter)
        {
            return popup->Accept();
        }
        if (key == Key::Escape)
        {
            ++m_Request;
            m_Timer.Cancel();
            popup->Close();
            return true;
        }
    }
    if (key == Key::Down && GetContext())
    {
        Query();
        return true;
    }
    return false;
}
} // namespace EStudioUI
