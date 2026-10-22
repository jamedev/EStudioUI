#include <EStudioUI/ScrollWidget.h>
#include <EStudioUI/UiContext.h>

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace
{
using namespace EStudioUI;
int s_Checks = 0;

void Check(bool condition, const std::string& message)
{
    ++s_Checks;
    if (!condition)
        throw std::runtime_error(message);
}

bool Near(float a, float b)
{
    return std::abs(a - b) < 0.02f;
}

Point Center(Rect rectangle)
{
    return {rectangle.m_X + rectangle.m_Width * 0.5f, rectangle.m_Y + rectangle.m_Height * 0.5f};
}

class ScrollProbe : public ScrollWidget
{
  public:
    ScrollProbe() : ScrollWidget("scroll-probe")
    {
        SetWidth(SizeRule::Fixed(300));
        SetHeight(SizeRule::Fixed(200));
    }

    void SetVirtualSize(Point size)
    {
        m_VirtualSize = size;
        Invalidate();
    }

    unsigned State(bool horizontal, bool thumb = true) const
    {
        return ScrollPartState(horizontal, thumb);
    }

    const TextLayout& Shape(FontFace& font, std::string_view text, float size) const
    {
        return TextFor(font, text, size);
    }

    bool m_LastPressConsumed = false;

  protected:
    Point GetVirtualSize() const override
    {
        return m_VirtualSize;
    }

    void OnPointerDown(const PointerEvent& event) override
    {
        m_LastPressConsumed = HandleScrollPointerDown(event);
    }

  private:
    Point m_VirtualSize{1000, 800};
};

void Defaults(FontFace& font)
{
    ScrollProbe detached;
    Check(Near(detached.GetPartStyle("scroll-track", Vertical).m_Width.m_Value, 12) &&
              Near(detached.GetPartStyle("scroll-thumb", Vertical).m_Width.m_Value, 6),
          "detached controls already have scrollbar defaults");
    UiContext ui(font);
    auto& scroll = ui.Root().Add<ScrollProbe>();
    ui.Prepare(500, 300);
    Check(Near(scroll.GetViewport().m_Width, 288) && Near(scroll.GetViewport().m_Height, 188),
          "default viewport retains two twelve-pixel gutters");
    auto vertical = scroll.GetScrollThumbBounds(false);
    auto horizontal = scroll.GetScrollThumbBounds(true);
    Check(Near(vertical.m_X, 291) && Near(vertical.m_Width, 6) && Near(horizontal.m_Y, 191) &&
              Near(horizontal.m_Height, 6),
          "default six-pixel thumbs retain their original centered positions");
    scroll.SetVirtualSize({1000000, 1000000});
    Check(Near(scroll.GetScrollThumbBounds(false).m_Height, 20) && Near(scroll.GetScrollThumbBounds(true).m_Width, 20),
          "default thumb minimum remains twenty pixels for large collections");
    scroll.SetVirtualSize({100, 100});
    scroll.SetScrollOffset({500, 500});
    Check(scroll.GetScrollOffset() == Point{} && scroll.GetScrollTrackBounds(false).m_Width == 0 &&
              scroll.GetScrollThumbBounds(true).m_Height == 0,
          "fitting content has no bars and clamps both offsets");
    ui.PointerDown({294, 100});
    Check(!scroll.m_LastPressConsumed, "an invisible fitting scrollbar cannot start a drag");
    ui.PointerUp({294, 100});
}

void MetricsAndDragging(FontFace& font)
{
    UiContext ui(font);
    auto& scroll = ui.Root().Add<ScrollProbe>();
    std::string error;
    Check(ui.SetStyleSheet(R"CSS(
        scroll-probe::scroll-track { padding: 2px; border: 1px solid #123456; background: linear-gradient(to bottom, #123456, #456789); }
        scroll-probe::scroll-track:vertical { width: 24px; }
        scroll-probe::scroll-track:horizontal { height: 18px; }
        scroll-probe::scroll-thumb { background: #cc3333; border-radius: 4px; box-shadow: 0 1px 2px #0008; }
        scroll-probe::scroll-thumb:vertical { width: 10px; min-height: 60px; }
        scroll-probe::scroll-thumb:horizontal { height: 8px; min-width: 80px; }
        scroll-probe::scroll-thumb:hover { background: #33cc33; }
        scroll-probe::scroll-thumb:active { background: #3333cc; }
    )CSS",
                           error),
          error);
    ui.Prepare(500, 300);
    Check(Near(scroll.GetViewport().m_Width, 276) && Near(scroll.GetViewport().m_Height, 182),
          "orientation-specific track metrics reserve exactly the painted gutters");
    const auto verticalTrack = scroll.GetScrollTrackBounds(false);
    const auto verticalThumb = scroll.GetScrollThumbBounds(false);
    const auto horizontalThumb = scroll.GetScrollThumbBounds(true);
    Check(Near(verticalTrack.m_X, 276) && Near(verticalTrack.m_Width, 24) && Near(verticalTrack.m_Height, 182),
          "public track bounds expose the allocated geometry");
    Check(Near(verticalThumb.m_X, 283) && Near(verticalThumb.m_Y, 3) && Near(verticalThumb.m_Width, 10) &&
              Near(verticalThumb.m_Height, 60),
          "thumb dimensions and placement account for CSS border, padding, and minimum length");
    Check(Near(horizontalThumb.m_X, 3) && Near(horizontalThumb.m_Y, 187) && Near(horizontalThumb.m_Width, 80) &&
              Near(horizontalThumb.m_Height, 8),
          "horizontal thumb metrics use the matching orientation palette");
    const auto& trackStyle = scroll.GetPartStyle("scroll-track", Vertical);
    const auto& thumbStyle = scroll.GetPartStyle("scroll-thumb", Vertical);
    Check(trackStyle.m_BackgroundImage.m_Stops.size() == 2 && thumbStyle.m_Shadow.m_Color.m_A > 0,
          "scroll parts retain compiled gradients and shadows for rich box rendering");
    const auto start = Center(verticalThumb);
    ui.PointerMove(start);
    Check((scroll.State(false) & Hover) && !(scroll.State(true) & Hover), "only the pointed thumb enters hover");
    Check(scroll.GetPartStyle("scroll-thumb", scroll.State(false)).m_Background.m_G > 0.7f,
          "thumb hover selects the precompiled state style");
    ui.PointerDown(start);
    Check(scroll.m_LastPressConsumed && (scroll.State(false) & Pressed), "painted thumb begins vertical drag");
    Check(scroll.GetPartStyle("scroll-thumb", scroll.State(false)).m_Background.m_B > 0.7f,
          "captured thumb selects its active style");
    const Point end{start.m_X, start.m_Y + 176 - 60};
    ui.PointerMove(end);
    Check(Near(scroll.GetScrollOffset().m_Y, 618), "vertical drag uses the padded track travel and reaches max");
    Check(Near(scroll.GetScrollThumbBounds(false).m_Y + scroll.GetScrollThumbBounds(false).m_Height, 179),
          "thumb end aligns exactly with the track interior end");
    ui.PointerUp(end);
    Check(!(scroll.State(false) & Pressed), "release clears only scrollbar drag state");
    const auto horizontalStart = Center(scroll.GetScrollThumbBounds(true));
    ui.PointerDown(horizontalStart);
    ui.PointerMove({horizontalStart.m_X + 270 - 80, horizontalStart.m_Y});
    Check(Near(scroll.GetScrollOffset().m_X, 724), "horizontal drag reaches its independently clamped end");
    ui.PointerMove({-1000, horizontalStart.m_Y});
    Check(Near(scroll.GetScrollOffset().m_X, 0), "captured dragging outside the control clamps at zero");
    ui.PointerUp({-1000, horizontalStart.m_Y});
    Check(!(scroll.State(true) & (Hover | Pressed)), "pointer release outside leaves no stale scrollbar state");
    scroll.SetScrollOffset({0, 0});
    ui.PointerDown({verticalTrack.m_X + 1, verticalTrack.m_Y + verticalTrack.m_Height - 1});
    Check(scroll.m_LastPressConsumed && Near(scroll.GetScrollOffset().m_Y, 618),
          "the styled gutter participates in track clicks and jumps to the clamped end");
    ui.Deactivate();
    Check(!(scroll.State(false) & Pressed), "capture cancellation clears the scrollbar active state");
}

void HiddenAndIdle(FontFace& font)
{
    UiContext ui(font);
    auto& scroll = ui.Root().Add<ScrollProbe>();
    auto& child = scroll.Add<Widget>();
    child.SetFloatingBounds({20, 20, 60, 30});
    std::string error;
    Check(ui.SetStyleSheet(R"CSS(
        @keyframes pulse { from { opacity: 0.2; } to { opacity: 1; } }
        scroll-probe::scroll-thumb { animation: pulse 1s infinite; transition: background 1s; }
        scroll-probe::scroll-thumb:hover { background: red; }
    )CSS",
                           error),
          error);
    ui.Prepare(500, 300);
    ui.PointerMove(Center(scroll.GetScrollThumbBounds(false)));
    Check(scroll.State(false) & Hover, "visible thumb is hovered before entering an embedded editor");
    ui.PointerMove({30, 30});
    Check(scroll.IsHovered() && !(scroll.State(false) & Hover),
          "moving into an embedded child clears scrollbar hover while the scroll owner remains hovered");
    ui.Prepare(500, 300);
    const auto stats = ui.GetStats();
    for (int i = 0; i < 60; ++i)
    {
        ui.SetTime(i / 60.0);
        ui.Prepare(500, 300);
    }
    Check(!ui.HasAnimations(), "shared scrollbar state palettes never allocate looping per-item timelines");
    Check(ui.GetStats().m_StyleResolutions == stats.m_StyleResolutions &&
              ui.GetStats().m_LayoutPasses == stats.m_LayoutPasses,
          "idle scrollbar palettes cause no recurring selector or layout work");
    scroll.SetScrollOffset({10000, 10000});
    Check(ui.SetStyleSheet("scroll-probe::scroll-track { display: none; }", error), error);
    ui.Prepare(500, 300);
    Check(Near(scroll.GetViewport().m_Width, 300) && Near(scroll.GetViewport().m_Height, 200),
          "display none tracks remove both reserved gutters");
    Check(Near(scroll.GetScrollOffset().m_X, 700) && Near(scroll.GetScrollOffset().m_Y, 600),
          "theme metric changes immediately reclamp the public scroll position");
    Check(scroll.GetScrollTrackBounds(false).m_Width == 0 && scroll.GetScrollThumbBounds(true).m_Height == 0,
          "hidden tracks suppress their thumbs too");
    ui.PointerDown({294, 100});
    Check(!scroll.m_LastPressConsumed, "hidden tracks cannot consume pointer presses");
    ui.PointerUp({294, 100});
    Check(!ui.HasAnimations(), "hidden scrollbars cannot request animation frames");
    Check(ui.SetStyleSheet("scroll-probe::scroll-thumb { display: none; }", error), error);
    ui.Prepare(500, 300);
    Check(Near(scroll.GetViewport().m_Width, 288) && scroll.GetScrollThumbBounds(false).m_Width == 0,
          "hiding a thumb retains the explicitly styled track gutter");
    ui.PointerDown({294, 100});
    Check(!scroll.m_LastPressConsumed, "a hidden thumb cannot initiate dragging");
    ui.PointerUp({294, 100});
}

void TransformedStationaryHover(FontFace& font)
{
    UiContext ui(font);
    auto& scroll = ui.Root().Add<ScrollProbe>();
    std::string error;
    Check(ui.SetStyleSheet(R"CSS(
        scroll-probe { transform: translate(70px, 40px) scale(1.2); transform-origin: 0% 0%; }
        scroll-probe::scroll-thumb:hover { background: #33cc33; }
    )CSS",
                           error),
          error);
    ui.Prepare(500, 300);
    const auto matrix = scroll.GetStyle().m_Transform.Resolve(scroll.GetBounds());
    const auto pointer = matrix.Map(Center(scroll.GetScrollThumbBounds(false)));
    ui.PointerMove(pointer);
    Check((scroll.State(false) & Hover) &&
              scroll.GetPartStyle("scroll-thumb", scroll.State(false)).m_Background.m_G > 0.7f,
          "a transformed scrollbar compares window pointer snapshots and hits inverse-mapped layout geometry");
    scroll.SetScrollOffset({0, 10000});
    Check(
        !(scroll.State(false) & Hover) && (scroll.State(false, false) & Hover),
        "programmatic scroll immediately clears a stationary pointer's stale thumb hover while preserving track hover");
    scroll.SetScrollOffset({0, 0});
    Check(scroll.State(false) & Hover,
          "programmatic scroll back under a stationary transformed pointer restores thumb hover");
    const auto start = Center(scroll.GetScrollThumbBounds(false));
    const auto end = matrix.Map({start.m_X, start.m_Y + 10000});
    ui.PointerDown(pointer);
    ui.PointerMove(end);
    Check(Near(scroll.GetScrollOffset().m_Y, 612),
          "transformed scrollbar dragging still clamps using layout-space travel");
    ui.PointerUp(end);
}

void BoundedTextCache(FontFace& font)
{
    ScrollProbe scroll;
    const auto& small = scroll.Shape(font, "same caption", 12);
    const auto* smallAddress = &small;
    const float smallWidth = small.m_Width;
    const auto& large = scroll.Shape(font, "same caption", 24);
    Check(Near(large.m_Width, smallWidth * 2) && scroll.GetTextCacheEntryCount() == 2,
          "part font sizes have independent cached shaped captions");
    Check(&scroll.Shape(font, "same caption", 12) == smallAddress,
          "repainting an existing font-size variant reuses its retained shaping run");
    FontFace otherFont(ESTUDIOUI_FONT_PATH);
    scroll.Shape(otherFont, "same caption", 12);
    Check(scroll.GetTextCacheEntryCount() == 3, "font service identity is part of the shaped-caption key");
    for (int i = 0; i < 700; ++i)
        scroll.Shape(font, "caption " + std::to_string(i), 16);
    Check(scroll.GetTextCacheEntryCount() == 512, "text cache remains bounded across large virtual collections");
    const auto* recentlyUsed = &scroll.Shape(font, "caption 699", 16);
    scroll.Shape(font, "one more caption", 16);
    Check(&scroll.Shape(font, "caption 699", 16) == recentlyUsed,
          "eviction retains recent visible captions rather than clearing the whole cache");
    const std::string prefix(1600, 'a');
    for (int i = 0; i < 350; ++i)
        scroll.Shape(font, prefix + std::to_string(i), 16);
    Check(scroll.GetTextCacheBytes() <= 8 * 1024 * 1024 && scroll.GetTextCacheEntryCount() < 350,
          "a byte budget also bounds caches of unusually long captions");
    const auto count = scroll.GetTextCacheEntryCount();
    const auto bytes = scroll.GetTextCacheBytes();
    scroll.Shape(font, std::string(400000, 'a'), 16);
    Check(scroll.GetTextCacheEntryCount() == count && scroll.GetTextCacheBytes() == bytes,
          "a single oversized caption uses replaceable scratch storage without evicting the working cache");
    bool rejected = false;
    try
    {
        scroll.Shape(font, "invalid size", std::numeric_limits<float>::quiet_NaN());
    }
    catch (const std::invalid_argument&)
    {
        rejected = true;
    }
    Check(rejected, "nonfinite caption sizes cannot poison cache lookup identities");
}
} // namespace

int main()
{
    try
    {
        FontFace font(ESTUDIOUI_FONT_PATH);
        Defaults(font);
        MetricsAndDragging(font);
        HiddenAndIdle(font);
        TransformedStationaryHover(font);
        BoundedTextCache(font);
        std::cout << "Styled scrollbars PASS / " << s_Checks << " checks\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Styled scrollbars FAIL / " << error.what() << '\n';
        return 1;
    }
}
