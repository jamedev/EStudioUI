#include <EStudioUI/CommandPalette.h>
#include <EStudioUI/UiContext.h>
#include <EStudioUI/Models.h>
#include <utf8proc.h>
#include <algorithm>
#include <cstdlib>
#include <stdexcept>

namespace EStudioUI
{
namespace
{
std::string FoldCommand(std::string_view text)
{
    utf8proc_uint8_t* output = nullptr;
    const auto length =
        utf8proc_map(reinterpret_cast<const utf8proc_uint8_t*>(text.data()), static_cast<utf8proc_ssize_t>(text.size()),
                     &output, static_cast<utf8proc_option_t>(UTF8PROC_STABLE | UTF8PROC_COMPOSE | UTF8PROC_CASEFOLD));
    const std::unique_ptr<utf8proc_uint8_t, decltype(&std::free)> owned(output, std::free);
    if (length < 0)
    {
        throw std::invalid_argument("Command captions must contain valid UTF-8.");
    }
    return length ? std::string(reinterpret_cast<char*>(output), static_cast<std::size_t>(length)) : std::string{};
}
} // namespace

CommandPalette::CommandPalette() : Popup("commandpalette")
{
    SetWidth(SizeRule::Fixed(540));
    SetHeight(SizeRule::Fixed(360));
    m_Search = &Add<SearchBox>();
    m_Search->SetWidth(SizeRule::Fill());
    m_Search->SetHeight(SizeRule::Fixed(36));
    m_Search->GetEditor().SetPlaceholder("Type a command…");
    m_List = &Add<ListWidget>();
    m_List->SetWidth(SizeRule::Fill());
    m_List->SetHeight(SizeRule::Fill());
    const WidgetRef self(this);
    m_Search->TextChanged().Connect(
        [self](const std::string&)
        {
            if (auto* live = dynamic_cast<CommandPalette*>(self.Get()))
            {
                live->Refresh();
            }
        });
    auto execute = [self](int)
    {
        if (auto* live = dynamic_cast<CommandPalette*>(self.Get()))
        {
            live->ExecuteCurrent();
        }
    };
    m_List->Activated().Connect(execute);
    m_List->ItemClicked().Connect(execute);
}

CommandPalette& CommandPalette::Create(Widget& owner)
{
    if (!owner.GetContext())
    {
        throw std::logic_error("Attach the palette owner first.");
    }
    return static_cast<CommandPalette&>(owner.GetContext()->AddOwnedPopup(std::make_unique<CommandPalette>(), owner));
}

void CommandPalette::SetActions(std::vector<std::shared_ptr<Action>> actions)
{
    VerifyStructuralMutation();
    if (actions.size() > 4096 ||
        std::any_of(actions.begin(), actions.end(), [](const auto& action) { return !action; }))
    {
        throw std::invalid_argument("A palette accepts at most 4096 nonnull actions.");
    }
    std::size_t bytes = 0;
    for (const auto& action : actions)
    {
        bytes += action->GetSnapshot()->m_Text.size();
        if (bytes > 1048576)
        {
            throw std::length_error("Palette captions exceed 1 MiB.");
        }
    }
    m_Actions = std::move(actions);
    if (GetContext())
    {
        Refresh();
    }
}

PopupSession CommandPalette::Show()
{
    VerifyStructuralMutation();
    Refresh();
    auto options = PopupOptions::Modal();
    options.m_Placement = PopupPlacement::Center;
    const WidgetRef self(this);
    auto session = Open(Point{}, options);
    if (auto* live = dynamic_cast<CommandPalette*>(self.Get()))
    {
        live->GetContext()->SetFocus(&live->m_Search->GetEditor());
    }
    return session;
}

SearchBox& CommandPalette::GetSearchBox() const
{
    VerifyAccess();
    return *m_Search;
}

void CommandPalette::Refresh()
{
    VerifyStructuralMutation();
    if (!GetContext())
    {
        return;
    }
    const auto key = FoldCommand(m_Search->GetText());
    std::vector<std::shared_ptr<Action>> visible;
    std::vector<std::string> captions;
    for (const auto& action : m_Actions)
    {
        if (action->GetStopToken().stop_requested())
        {
            continue;
        }
        const auto state = action->GetSnapshot();
        if (state->m_Visible && state->m_Enabled && FoldCommand(state->m_Text).find(key) != std::string::npos)
        {
            visible.push_back(action);
            captions.push_back(state->m_Text +
                               (state->m_Shortcuts.empty() ? "" : "    " + state->m_Shortcuts.front().ToString()));
        }
        if (visible.size() == 256)
        {
            break;
        }
    }

    struct Caption
    {
        std::string m_Text;
    };

    std::vector<Caption> rows;
    for (auto& caption : captions)
    {
        rows.push_back({std::move(caption)});
    }
    const WidgetRef self(this);
    m_Visible = std::move(visible);
    m_List->SetModel(MakeTableModel(GetContext()->States(), std::move(rows), Column("Command", &Caption::m_Text)));
    if (auto* live = dynamic_cast<CommandPalette*>(self.Get()))
    {
        live->m_List->SetCurrentIndex(live->m_Visible.empty() ? -1 : 0);
    }
}

bool CommandPalette::ExecuteCurrent()
{
    VerifyStructuralMutation();
    const auto lease = RetainOwner();
    const auto index = m_List->GetCurrentIndex();
    if (index < 0 || static_cast<std::size_t>(index) >= m_Visible.size())
    {
        return false;
    }
    const auto action = m_Visible[static_cast<std::size_t>(index)];
    if (action->GetStopToken().stop_requested())
    {
        return false;
    }
    const auto state = action->GetSnapshot();
    if (!state->m_Visible || !state->m_Enabled)
    {
        Refresh();
        return false;
    }
    Close({PopupCloseReason::Accepted, index});
    return !action->GetStopToken().stop_requested() && action->Trigger();
}

bool CommandPalette::OnKeyPreview(Key key, bool down, bool, Modifiers)
{
    if (!down)
    {
        return false;
    }
    if (key == Key::Enter)
    {
        ExecuteCurrent();
        return true;
    }
    if ((key == Key::Down || key == Key::Up) && !m_Visible.empty())
    {
        m_List->SetCurrentIndex(std::clamp(m_List->GetCurrentIndex() + (key == Key::Down ? 1 : -1), 0,
                                           static_cast<int>(m_Visible.size()) - 1));
        return true;
    }
    return false;
}
} // namespace EStudioUI
