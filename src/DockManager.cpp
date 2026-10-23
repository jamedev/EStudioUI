#include <EStudioUI/DockManager.h>
#include <EStudioUI/Splitter.h>
#include <EStudioUI/TabWidget.h>
#include <EStudioUI/TextBuffer.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

namespace EStudioUI
{
namespace
{
std::size_t AreaIndex(DockArea area)
{
    const auto index = static_cast<std::size_t>(area);
    if (index >= 4)
        throw std::invalid_argument("A dock area must be Left, Right, Top or Bottom.");
    return index;
}

bool ValidId(std::string_view id)
{
    return !id.empty() && id.size() <= 128 &&
           std::all_of(id.begin(), id.end(),
                       [](unsigned char c)
                       {
                           return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                                  c == '_' || c == '-' || c == '.';
                       });
}

bool ValidBounds(Rect bounds)
{
    return std::isfinite(bounds.m_X) && std::isfinite(bounds.m_Y) && std::isfinite(bounds.m_Width) &&
           std::isfinite(bounds.m_Height) && std::abs(bounds.m_X) <= 1000000 && std::abs(bounds.m_Y) <= 1000000 &&
           bounds.m_Width >= 160 && bounds.m_Height >= 120 && bounds.m_Width <= 16384 && bounds.m_Height <= 16384;
}
} // namespace

struct DockManager::Impl
{
    struct Entry
    {
        WidgetRef m_Panel;
        WidgetRef m_Page;
        DockArea m_Area = DockArea::Left;
        bool m_Visible = true;
        std::unique_ptr<Window> m_Window;
        Rect m_Bounds{160, 160, 420, 460};
    };

    struct Mutation
    {
        Impl& m_Data;

        explicit Mutation(Impl& data) : m_Data(data)
        {
            if (data.m_Operating || data.m_Closing || data.m_Owner->IsClosed())
                throw std::logic_error("A workspace operation cannot reenter an unfinished operation or closed owner.");
            data.m_Operating = true;
        }

        ~Mutation()
        {
            m_Data.m_Operating = false;
        }
    };

    WidgetRef m_Self;
    Window* m_Owner = nullptr;
    Widget* m_Central = nullptr;
    Widget* m_Storage = nullptr;
    Widget* m_Preview = nullptr;
    Splitter* m_Vertical = nullptr;
    Splitter* m_Horizontal = nullptr;
    std::array<TabWidget*, 4> m_Groups{};
    std::vector<std::unique_ptr<Entry>> m_Entries;
    std::vector<std::unique_ptr<Window>> m_Retired;
    std::vector<Connection> m_Connections;
    std::uint64_t m_StyleRevision = 0;
    std::uint64_t m_Revision = 0;
    std::uint64_t m_Notified = 0;
    bool m_Operating = false;
    bool m_Closing = false;
    bool m_Notifying = false;
    WidgetRef m_Drag;
    Point m_DragStart;
    bool m_DragMoved = false;
    std::optional<DockArea> m_Target;
    Signal<> m_Changed;

    Entry& Get(const DockWidget& panel) const
    {
        for (const auto& entry : m_Entries)
            if (entry->m_Panel.Get() == &panel)
                return *entry;
        throw std::invalid_argument("The dock panel does not belong to this manager.");
    }

    void CheckAlive() const
    {
        if (m_Closing || !m_Self.Get() || m_Owner->IsClosed())
            throw std::logic_error("A workspace callback removed the manager or closed its owner.");
    }

    int PageIndex(const Entry& entry) const
    {
        auto& group = *m_Groups[AreaIndex(entry.m_Area)];
        for (int index = 0; index < group.GetTabCount(); ++index)
            if (&group.GetPage(index) == entry.m_Page.Get())
                return index;
        return -1;
    }

    void RefreshAreas()
    {
        CheckAlive();
        for (auto* group : m_Groups)
        {
            group->SetTabBarVisible(group->GetTabCount() > 1);
            group->SetVisible(group->GetTabCount() != 0);
        }
    }

    void RetireWindow(Entry& entry)
    {
        if (!entry.m_Window)
            return;
        entry.m_Bounds = entry.m_Window->GetWindowBounds();
        auto window = std::move(entry.m_Window);
        m_Retired.push_back(std::move(window));
        m_Retired.back()->Close();
    }

    void RemovePage(Entry& entry)
    {
        const int index = PageIndex(entry);
        if (index >= 0)
        {
            entry.m_Page = {};
            m_Groups[AreaIndex(entry.m_Area)]->RemoveTab(index);
            CheckAlive();
        }
    }

    void Move(Entry& entry, DockArea area, int index, bool visible)
    {
        auto* panel = dynamic_cast<DockWidget*>(entry.m_Panel.Get());
        if (!panel)
            throw std::logic_error("A dock panel was removed outside its manager.");
        AreaIndex(area);
        // Park before removing a page: the panel and every application child keep their identities.
        panel->ReparentTo(*m_Storage);
        CheckAlive();
        RemovePage(entry);
        RetireWindow(entry);
        CheckAlive();
        entry.m_Area = area;
        entry.m_Visible = visible;
        panel = dynamic_cast<DockWidget*>(entry.m_Panel.Get());
        if (!panel)
            throw std::logic_error("An input retirement callback removed a dock panel.");
        panel->SetVisible(true);
        if (visible)
        {
            auto& group = *m_Groups[AreaIndex(area)];
            auto& page = group.AddTab(panel->GetTitle());
            entry.m_Page = WidgetRef(&page);
            panel->ReparentTo(page);
            CheckAlive();
            const int last = group.GetTabCount() - 1;
            if (index >= 0 && index < last)
                group.MoveTab(last, index);
            const int selected = PageIndex(entry);
            group.SetCurrentIndex(selected);
        }
        RefreshAreas();
        ++m_Revision;
    }

    void Float(Entry& entry, Rect bounds)
    {
        if (!ValidBounds(bounds))
            throw std::invalid_argument(
                "Floating bounds must be finite desktop coordinates with size 160..16384 by 120..16384.");
        if (entry.m_Window && !entry.m_Window->IsClosed())
        {
            entry.m_Window->SetWindowBounds(bounds);
            entry.m_Bounds = bounds;
            ++m_Revision;
            return;
        }
        auto* panel = dynamic_cast<DockWidget*>(entry.m_Panel.Get());
        if (!panel)
            throw std::logic_error("The floating panel no longer exists.");
        WindowOptions options;
        options.m_Title = panel->GetTitle();
        options.m_Width = static_cast<int>(bounds.m_Width);
        options.m_Height = static_cast<int>(bounds.m_Height);
        auto host = m_Owner->CreateOwnedWindow(std::move(options));
        CheckAlive();
        host->SetWindowBounds(bounds);
        CheckAlive();
        panel = dynamic_cast<DockWidget*>(entry.m_Panel.Get());
        if (!panel)
            throw std::logic_error("A native focus callback removed the floating panel.");
        host->Ui().Root().SetStyleProperty(StyleProperty::Padding, Insets{});
        host->Ui().Root().SetGap(0);
        const auto manager = m_Self;
        const auto identity = panel->GetDockId();
        host->Closed().Connect(
            [manager, identity]
            {
                auto* live = dynamic_cast<DockManager*>(manager.Get());
                if (!live || live->m_Impl->m_Closing || !live->GetContext())
                    return;
                // A close may arrive from a native callback; transfer at the next safe owner turn.
                live->GetContext()->Defer(
                    [manager, identity](UiContext&)
                    {
                        auto* target = dynamic_cast<DockManager*>(manager.Get());
                        auto* dock = target ? target->FindDock(identity) : nullptr;
                        if (!dock || target->m_Impl->m_Closing)
                            return;
                        auto* floating = target->GetFloatingWindow(*dock);
                        if (floating && floating->IsClosed())
                            target->SetDockVisible(*dock, false);
                    });
            });
        panel->ReparentTo(host->Ui().Root());
        CheckAlive();
        RemovePage(entry);
        RetireWindow(entry);
        CheckAlive();
        entry.m_Window = std::move(host);
        entry.m_Bounds = bounds;
        entry.m_Visible = true;
        panel->SetVisible(true);
        RefreshAreas();
        ++m_Revision;
    }

    void Changed()
    {
        if (m_Notifying || m_Closing)
            return;
        m_Notifying = true;
        try
        {
            unsigned turns = 0;
            while (m_Notified != m_Revision && m_Self.Get() && !m_Closing)
            {
                if (++turns > 64)
                    throw std::logic_error("Workspace observers exceeded 64 reentrant changes.");
                m_Notified = m_Revision;
                m_Changed.Emit();
            }
            m_Notifying = false;
        }
        catch (...)
        {
            m_Notifying = false;
            throw;
        }
    }
};

DockManager::DockManager(Window& window) : Widget("dockmanager"), m_Impl(std::make_shared<Impl>())
{
    window.VerifyAccess();
    auto& data = *m_Impl;
    data.m_Self = WidgetRef(this);
    data.m_Owner = &window;
    data.m_StyleRevision = window.Ui().GetStyleSheetRevision();
    SetHeight(SizeRule::Fill());
    SetGap(0);
    data.m_Vertical = &Add<Splitter>(Axis::Vertical);
    data.m_Vertical->AddClass("workspace-splitter");
    data.m_Vertical->SetHeight(SizeRule::Fill());
    data.m_Groups[2] = &data.m_Vertical->Add<TabWidget>();
    data.m_Horizontal = &data.m_Vertical->Add<Splitter>(Axis::Horizontal);
    data.m_Horizontal->AddClass("workspace-splitter");
    data.m_Groups[0] = &data.m_Horizontal->Add<TabWidget>();
    data.m_Central = &data.m_Horizontal->Add<Widget>("workspace-central");
    data.m_Central->SetHeight(SizeRule::Fill());
    data.m_Groups[1] = &data.m_Horizontal->Add<TabWidget>();
    data.m_Groups[3] = &data.m_Vertical->Add<TabWidget>();
    data.m_Vertical->SetSizes({180, 600, 200});
    data.m_Horizontal->SetSizes({240, 680, 280});
    for (std::size_t index = 0; index < data.m_Groups.size(); ++index)
    {
        auto* group = data.m_Groups[index];
        group->AddClass("dock-tabs");
        group->SetHeight(SizeRule::Fill());
        group->SetWidth(SizeRule::Fill());
        group->SetVisible(false);
        data.m_Connections.push_back(group->CurrentChanged().Connect(
            [self = data.m_Self](int)
            {
                if (auto* live = dynamic_cast<DockManager*>(self.Get());
                    live && !live->m_Impl->m_Operating && !live->m_Impl->m_Closing)
                {
                    const auto keep = live->m_Impl;
                    ++keep->m_Revision;
                    keep->Changed();
                }
            }));
    }
    for (auto* splitter : {data.m_Vertical, data.m_Horizontal})
        data.m_Connections.push_back(splitter->SizesChanged().Connect(
            [self = data.m_Self](const std::vector<float>&)
            {
                if (auto* live = dynamic_cast<DockManager*>(self.Get());
                    live && !live->m_Impl->m_Operating && !live->m_Impl->m_Closing)
                {
                    const auto keep = live->m_Impl;
                    ++keep->m_Revision;
                    keep->Changed();
                }
            }));
    data.m_Storage = &Add<Widget>("dock-storage");
    data.m_Storage->SetVisible(false);
    data.m_Preview = &Add<Widget>("dock-preview");
    data.m_Preview->SetFloatingBounds({0, 0, 0, 0});
    data.m_Preview->SetVisible(false);
    data.m_Connections.push_back(window.Closed().Connect(
        [weak = std::weak_ptr<Impl>(m_Impl)]
        {
            if (const auto live = weak.lock())
            {
                live->m_Closing = true;
            }
        }));
    data.m_Connections.push_back(window.FramePreparing().Connect(
        [weak = std::weak_ptr<Impl>(m_Impl)]
        {
            if (const auto live = weak.lock(); live && !live->m_Closing)
            {
                auto retired = std::move(live->m_Retired);
                retired.clear();
                if (live->m_Closing || !live->m_Self.Get())
                    return;
                const auto revision = live->m_Owner->Ui().GetStyleSheetRevision();
                if (revision != live->m_StyleRevision)
                {
                    for (const auto& entry : live->m_Entries)
                        if (entry->m_Window && !entry->m_Window->IsClosed())
                        {
                            entry->m_Window->Ui().CopyStyleSheetFrom(live->m_Owner->Ui());
                            if (live->m_Closing || !live->m_Self.Get())
                                return;
                        }
                    live->m_StyleRevision = revision;
                }
            }
        }));
}

DockManager::~DockManager()
{
    m_Impl->m_Closing = true;
    for (const auto& connection : m_Impl->m_Connections)
        connection.Disconnect();
    // Retire native surfaces before the manager's ordinary child ownership is torn down.
    m_Impl->m_Entries.clear();
    m_Impl->m_Retired.clear();
}

Widget& DockManager::Central() const
{
    VerifyAccess();
    return *m_Impl->m_Central;
}

DockWidget& DockManager::AddDock(std::string id, std::string title, DockArea area)
{
    VerifyStructuralMutation();
    const auto ownerLease = RetainOwner();
    const auto data = m_Impl;
    AreaIndex(area);
    if (!GetContext() || GetContext() != &data->m_Owner->Ui())
        throw std::logic_error("Attach the DockManager to its owner window before adding panels.");
    if (!ValidId(id) || FindDock(id) || data->m_Entries.size() >= 128)
        throw std::invalid_argument(
            "Dock IDs must be unique ASCII letters/digits/_.-, 1..128 bytes; at most 128 panels are admitted.");
    if (title.size() > 4096 || TextBuffer::Sanitize(title) != title)
        throw std::invalid_argument("Dock captions must be single-line UTF-8, at most 4096 bytes.");
    DockWidget* result = nullptr;
    {
        Impl::Mutation mutation(*data);
        auto entry = std::make_unique<Impl::Entry>();
        data->m_Entries.reserve(data->m_Entries.size() + 1);
        result = &data->m_Storage->Add<DockWidget>(std::move(id), std::move(title));
        result->m_Manager = WidgetRef(this);
        entry->m_Panel = WidgetRef(result);
        entry->m_Area = area;
        data->m_Entries.push_back(std::move(entry));
        data->Move(*data->m_Entries.back(), area, -1, true);
    }
    const WidgetRef created(result);
    data->Changed();
    if (!created.Get())
        throw std::logic_error("A workspace observer removed the newly created panel.");
    return *result;
}

DockWidget* DockManager::FindDock(std::string_view id) const
{
    VerifyAccess();
    for (const auto& entry : m_Impl->m_Entries)
        if (auto* panel = dynamic_cast<DockWidget*>(entry->m_Panel.Get()); panel && panel->GetDockId() == id)
            return panel;
    return nullptr;
}

void DockManager::Dock(DockWidget& panel, DockArea area, int index)
{
    VerifyStructuralMutation();
    const auto lease = RetainOwner();
    const auto data = m_Impl;
    AreaIndex(area);
    if (index < -1 || index > 128)
        throw std::invalid_argument("Dock tab index must be -1 or 0..128.");
    {
        Impl::Mutation mutation(*data);
        auto& entry = data->Get(panel);
        if (entry.m_Visible && !entry.m_Window && entry.m_Area == area && data->PageIndex(entry) >= 0)
        {
            auto& group = *data->m_Groups[AreaIndex(area)];
            const int previous = data->PageIndex(entry);
            if (index >= 0)
                group.MoveTab(previous, std::min(index, group.GetTabCount() - 1));
            group.SetCurrentIndex(data->PageIndex(entry));
            ++data->m_Revision;
        }
        else
            data->Move(entry, area, index, true);
    }
    data->Changed();
}

void DockManager::Tabify(DockWidget& panel, DockWidget& beside)
{
    VerifyStructuralMutation();
    const auto data = m_Impl;
    auto& other = data->Get(beside);
    data->Get(panel);
    if (&panel == &beside)
        return;
    if (!other.m_Visible || other.m_Window)
        throw std::invalid_argument("Tabify requires a visible, docked destination panel.");
    Dock(panel, other.m_Area, data->PageIndex(other) + 1);
}

void DockManager::Float(DockWidget& panel, std::optional<Rect> desktopBounds)
{
    VerifyStructuralMutation();
    const auto lease = RetainOwner();
    const auto data = m_Impl;
    {
        Impl::Mutation mutation(*data);
        auto& entry = data->Get(panel);
        if (entry.m_Window && !entry.m_Window->IsClosed() && !desktopBounds)
        {
            entry.m_Window->Platform().FocusWindow();
            return;
        }
        auto bounds = desktopBounds.value_or(entry.m_Bounds);
        if (!desktopBounds && !entry.m_Window)
        {
            const auto owner = data->m_Owner->GetWindowBounds();
            bounds.m_X = owner.m_X + 70;
            bounds.m_Y = owner.m_Y + 80;
        }
        data->Float(entry, bounds);
    }
    data->Changed();
}

void DockManager::SetDockVisible(DockWidget& panel, bool visible)
{
    VerifyStructuralMutation();
    const auto lease = RetainOwner();
    const auto data = m_Impl;
    data->CheckAlive();
    auto& entry = data->Get(panel);
    if (visible && entry.m_Window && !entry.m_Window->IsClosed())
    {
        entry.m_Window->Platform().FocusWindow();
        return;
    }
    if (!visible && !entry.m_Visible)
        return;
    if (visible && entry.m_Visible && !entry.m_Window && data->PageIndex(entry) >= 0)
    {
        data->m_Groups[AreaIndex(entry.m_Area)]->SetCurrentIndex(data->PageIndex(entry));
        return;
    }
    {
        Impl::Mutation mutation(*data);
        data->Move(entry, entry.m_Area, -1, visible);
    }
    data->Changed();
}

bool DockManager::IsDockVisible(const DockWidget& panel) const
{
    VerifyAccess();
    return m_Impl->Get(panel).m_Visible;
}

bool DockManager::IsFloating(const DockWidget& panel) const
{
    VerifyAccess();
    return m_Impl->Get(panel).m_Window != nullptr;
}

DockArea DockManager::GetDockArea(const DockWidget& panel) const
{
    VerifyAccess();
    return m_Impl->Get(panel).m_Area;
}

Window* DockManager::GetFloatingWindow(const DockWidget& panel) const
{
    VerifyAccess();
    return m_Impl->Get(panel).m_Window.get();
}

Rect DockManager::GetAreaBounds(DockArea area) const
{
    VerifyAccess();
    return m_Impl->m_Groups[AreaIndex(area)]->GetBounds();
}

Signal<>& DockManager::WorkspaceChanged()
{
    VerifyAccess();
    return m_Impl->m_Changed;
}

void DockManager::UpdateTitle(DockWidget& panel)
{
    VerifyStructuralMutation();
    const auto& entry = m_Impl->Get(panel);
    const int index = m_Impl->PageIndex(entry);
    if (index >= 0)
        m_Impl->m_Groups[AreaIndex(entry.m_Area)]->SetTabText(index, panel.GetTitle());
}

void DockManager::BeginDrag(DockWidget& panel, Point screenPoint)
{
    VerifyStructuralMutation();
    m_Impl->Get(panel);
    m_Impl->m_Drag = WidgetRef(&panel);
    m_Impl->m_DragStart = screenPoint;
    m_Impl->m_DragMoved = false;
    m_Impl->m_Target.reset();
}

void DockManager::UpdateDrag(Point screenPoint)
{
    VerifyStructuralMutation();
    auto& data = *m_Impl;
    if (!data.m_Drag.Get())
        return;
    if (std::hypot(screenPoint.m_X - data.m_DragStart.m_X, screenPoint.m_Y - data.m_DragStart.m_Y) >= 5)
        data.m_DragMoved = true;
    if (!data.m_DragMoved)
        return;
    const auto metrics = data.m_Owner->Platform().GetWindowMetrics();
    const auto point = metrics.FromScreen(screenPoint);
    const auto bounds = GetBounds();
    data.m_Target.reset();
    if (bounds.Contains(point))
    {
        const float left = point.m_X - bounds.m_X, right = bounds.m_X + bounds.m_Width - point.m_X;
        const float top = point.m_Y - bounds.m_Y, bottom = bounds.m_Y + bounds.m_Height - point.m_Y;
        const float horizontal = std::min(120.0f, bounds.m_Width * .25f);
        const float vertical = std::min(100.0f, bounds.m_Height * .25f);
        if (left < horizontal)
            data.m_Target = DockArea::Left;
        else if (right < horizontal)
            data.m_Target = DockArea::Right;
        else if (top < vertical)
            data.m_Target = DockArea::Top;
        else if (bottom < vertical)
            data.m_Target = DockArea::Bottom;
    }
    if (!data.m_Target)
    {
        data.m_Preview->SetVisible(false);
        return;
    }
    auto preview = bounds;
    if (*data.m_Target == DockArea::Left || *data.m_Target == DockArea::Right)
    {
        preview.m_Width *= .25f;
        if (*data.m_Target == DockArea::Right)
            preview.m_X += bounds.m_Width - preview.m_Width;
    }
    else
    {
        preview.m_Height *= .25f;
        if (*data.m_Target == DockArea::Bottom)
            preview.m_Y += bounds.m_Height - preview.m_Height;
    }
    data.m_Preview->SetFloatingBounds(preview);
    data.m_Preview->SetVisible(true);
}

void DockManager::EndDrag(Point screenPoint)
{
    VerifyStructuralMutation();
    const auto data = m_Impl;
    UpdateDrag(screenPoint);
    const auto panelRef = data->m_Drag;
    const auto target = data->m_Target;
    const bool moved = data->m_DragMoved;
    CancelDrag();
    auto* panel = dynamic_cast<DockWidget*>(panelRef.Get());
    if (!panel || !moved)
        return;
    if (target)
        Dock(*panel, *target);
    else
        Float(*panel, Rect{screenPoint.m_X - 80, screenPoint.m_Y - 20, 420, 460});
}

void DockManager::CancelDrag()
{
    VerifyStructuralMutation();
    m_Impl->m_Drag = {};
    m_Impl->m_Target.reset();
    m_Impl->m_DragMoved = false;
    m_Impl->m_Preview->SetVisible(false);
}

std::string DockManager::SaveState() const
{
    VerifyAccess();
    const auto& data = *m_Impl;
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::setprecision(9) << "EStudioUI.Workspace 1\n";
    for (auto* splitter : {data.m_Vertical, data.m_Horizontal})
    {
        const auto sizes = splitter->GetSizes();
        out << "sizes";
        for (float size : sizes)
            out << ' ' << std::max(1.0f, size);
        out << '\n';
    }
    out << "panels " << data.m_Entries.size() << '\n';
    for (const auto& entry : data.m_Entries)
    {
        const auto* panel = dynamic_cast<DockWidget*>(entry->m_Panel.Get());
        if (!panel)
            throw std::logic_error("A dock panel was removed outside its manager.");
        auto bounds = entry->m_Window ? entry->m_Window->GetWindowBounds() : entry->m_Bounds;
        bounds.m_Width = std::clamp(bounds.m_Width, 160.0f, 16384.0f);
        bounds.m_Height = std::clamp(bounds.m_Height, 120.0f, 16384.0f);
        out << std::quoted(panel->GetDockId()) << ' ' << AreaIndex(entry->m_Area) << ' ' << data.PageIndex(*entry)
            << ' ' << entry->m_Visible << ' ' << (entry->m_Window != nullptr) << ' ' << bounds.m_X << ' ' << bounds.m_Y
            << ' ' << bounds.m_Width << ' ' << bounds.m_Height << '\n';
    }
    out << "selected";
    for (std::size_t area = 0; area < 4; ++area)
    {
        std::string id;
        const auto selected = data.m_Groups[area]->GetCurrentIndex();
        for (const auto& entry : data.m_Entries)
            if (AreaIndex(entry->m_Area) == area && entry->m_Page.Get() && data.PageIndex(*entry) == selected)
                if (const auto* panel = dynamic_cast<DockWidget*>(entry->m_Panel.Get()))
                    id = panel->GetDockId();
        out << ' ' << std::quoted(id);
    }
    out << "\nend\n";
    return out.str();
}

bool DockManager::RestoreState(std::string_view state, std::string& error)
{
    VerifyStructuralMutation();
    const auto lease = RetainOwner();
    const auto data = m_Impl;

    struct Saved
    {
        std::string m_Id;
        DockArea m_Area;
        int m_Order = -1;
        bool m_Visible = false;
        bool m_Floating = false;
        Rect m_Bounds;
    };

    std::array<std::array<float, 3>, 2> sizes{};
    std::array<std::string, 4> selected;
    std::vector<Saved> panels;
    try
    {
        if (state.size() > 65536)
            throw std::invalid_argument("Workspace state exceeds 64 KiB.");
        std::istringstream in{std::string(state)};
        in.imbue(std::locale::classic());
        std::string token;
        int version = 0;
        if (!(in >> token >> version) || token != "EStudioUI.Workspace" || version != 1)
            throw std::invalid_argument("Unsupported workspace signature or version.");
        for (auto& axis : sizes)
        {
            if (!(in >> token) || token != "sizes")
                throw std::invalid_argument("Workspace splitter sizes are missing.");
            for (auto& size : axis)
                if (!(in >> size) || !std::isfinite(size) || size <= 0 || size > 1000000)
                    throw std::invalid_argument("Workspace sizes must be finite positive weights, at most 1000000.");
        }
        std::size_t count = 0;
        if (!(in >> token >> count) || token != "panels" || count > 128)
            throw std::invalid_argument("Workspace panel count must be 0..128.");
        std::unordered_set<std::string> ids;
        std::array<std::unordered_set<int>, 4> orders;
        for (std::size_t index = 0; index < count; ++index)
        {
            Saved panel;
            int area = 0, visible = 0, floating = 0;
            if (!(in >> std::quoted(panel.m_Id) >> area >> panel.m_Order >> visible >> floating >> panel.m_Bounds.m_X >>
                  panel.m_Bounds.m_Y >> panel.m_Bounds.m_Width >> panel.m_Bounds.m_Height) ||
                !ValidId(panel.m_Id) || !ids.insert(panel.m_Id).second || area < 0 || area > 3 || panel.m_Order < -1 ||
                panel.m_Order >= 128 || (visible != 0 && visible != 1) || (floating != 0 && floating != 1) ||
                !ValidBounds(panel.m_Bounds) || (floating && !visible) || (visible && !floating && panel.m_Order < 0) ||
                ((!visible || floating) && panel.m_Order != -1))
                throw std::invalid_argument("Invalid or duplicate workspace panel record.");
            panel.m_Area = static_cast<DockArea>(area);
            panel.m_Visible = visible != 0;
            panel.m_Floating = floating != 0;
            if (visible && !floating && !orders[static_cast<std::size_t>(area)].insert(panel.m_Order).second)
                throw std::invalid_argument("Workspace tab orders must be unique within each group.");
            panels.push_back(std::move(panel));
        }
        if (!(in >> token) || token != "selected")
            throw std::invalid_argument("Workspace selection record is missing.");
        for (std::size_t area = 0; area < 4; ++area)
        {
            auto& id = selected[area];
            if (!(in >> std::quoted(id)) || (!id.empty() && !ValidId(id)))
                throw std::invalid_argument("Invalid workspace selection identity.");
            if (!id.empty() && std::none_of(panels.begin(), panels.end(),
                                            [&](const Saved& panel)
                                            {
                                                return panel.m_Id == id && panel.m_Visible && !panel.m_Floating &&
                                                       AreaIndex(panel.m_Area) == area;
                                            }))
                throw std::invalid_argument("Workspace selection must name a visible panel in the same group.");
        }
        if (!(in >> token) || token != "end" || (in >> token))
            throw std::invalid_argument("Workspace has a missing end marker or trailing data.");
    }
    catch (const std::exception& exception)
    {
        error = exception.what();
        return false;
    }
    // All bytes have been checked before any live layout, visibility or native surface is changed.
    {
        Impl::Mutation mutation(*data);
        for (const auto& saved : panels)
            if (auto* panel = FindDock(saved.m_Id))
                data->Move(data->Get(*panel), saved.m_Area, -1, false);
        std::stable_sort(panels.begin(), panels.end(),
                         [](const Saved& left, const Saved& right) { return left.m_Order < right.m_Order; });
        for (const auto& saved : panels)
        {
            auto* panel = FindDock(saved.m_Id);
            if (!panel)
                continue;
            auto& entry = data->Get(*panel);
            entry.m_Bounds = saved.m_Bounds;
            if (saved.m_Floating)
                data->Float(entry, saved.m_Bounds);
            else if (saved.m_Visible)
                data->Move(entry, saved.m_Area, saved.m_Order, true);
        }
        data->m_Vertical->SetSizes(std::vector<float>(sizes[0].begin(), sizes[0].end()));
        data->m_Horizontal->SetSizes(std::vector<float>(sizes[1].begin(), sizes[1].end()));
        for (std::size_t area = 0; area < 4; ++area)
            if (auto* panel = FindDock(selected[area]))
                data->m_Groups[area]->SetCurrentIndex(data->PageIndex(data->Get(*panel)));
        ++data->m_Revision;
    }
    error.clear();
    data->Changed();
    return true;
}
} // namespace EStudioUI
