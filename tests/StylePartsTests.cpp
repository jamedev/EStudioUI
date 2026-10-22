#include <EStudioUI/Button.h>
#include <EStudioUI/UiContext.h>

#include <cmath>
#include <iostream>
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

bool Near(double first, double last)
{
    return std::abs(first - last) < 0.0002;
}

class PartsWidget : public Widget
{
  public:
    explicit PartsWidget(bool animated = true) : Widget("parts")
    {
        RegisterStylePart(
            "face", Hover | Pressed,
            [](const Style& owner, unsigned)
            {
                Style style;
                style.m_Background = owner.m_SelectionBackground;
                style.m_Color = owner.m_Color;
                style.m_FontSize = owner.m_FontSize;
                style.m_Width = {Length::Unit::Pixels, 12};
                return style;
            },
            animated);
        RegisterStylePart("item", Hover | Selected | Alternate,
                          [](const Style& owner, unsigned state)
                          {
                              Style style;
                              style.m_Background = state & Selected ? owner.m_SelectionBackground : owner.m_Background;
                              style.m_Color = owner.m_Color;
                              return style;
                          });
    }

    using Widget::RegisterStylePart;
    using Widget::SetPartState;
    using Widget::SetState;
};

void Parser()
{
    StyleSheet sheet;
    std::string error;
    Check(sheet.Parse(R"CSS(
        .scope { color: #123456; }
        parts { background: black; }
        parts::face { background: red; border-width: 2px; }
        parts:hover::face { color: blue; }
        parts::face:hover { background: blue; }
        parts::face:disabled { opacity: .25; }
        .scope > parts#target::face:selected { border-radius: 15px; }
        parts::face:selected { border-radius: 3px !important; }
        parts:focus-within::item:alternate:expanded:sorted { background: #00ff00; }
        parts:valid:placeholder-shown:horizontal::face { height: 9px; }
        parts:hover::before { content: ''; opacity: .3; }
    )CSS",
                      error),
          error);
    StyleNode parent{"panel", "", {"scope"}};
    StyleNode node{
        "parts", "target", {}, Hover | Disabled | FocusWithin | Valid | PlaceholderShown | Horizontal, &parent};
    Style fallback;
    fallback.m_Padding = {1, 2, 3, 4};
    fallback.m_FontSize = 22;
    const auto plain = sheet.ComputePart(node, "face", 0, fallback);
    Check(plain.m_Background == Color{1, 0, 0, 1} && plain.m_Color == Color{0, 0, 1, 1},
          "owner hover and part hover remain independent");
    Check(plain.m_Padding == fallback.m_Padding && plain.m_FontSize == 22 && Near(plain.m_Opacity, .25),
          "named part cascade retains supplied defaults and inherits disabled state");
    const auto hovered = sheet.ComputePart(node, "face", Hover | Selected, fallback);
    Check(hovered.m_Background == Color{0, 0, 1, 1} && hovered.m_Radius == 3,
          "part state specificity and important declarations");
    Check(sheet.ComputePart(node, "item", Alternate | Expanded | Sorted, fallback).m_Background == Color{0, 1, 0, 1},
          "semantic owner and collection part states are parsed");
    Check(plain.m_Height == Length{Length::Unit::Pixels, 9}, "input semantic states match owner selectors");
    Check(sheet.Compute(node).m_Background == Color{0, 0, 0, 1}, "named part rules never leak into owner rules");
    Check(sheet.ComputePart(node, "", 0, fallback).m_Background == fallback.m_Background,
          "an empty part name cannot import ordinary owner declarations");
    Check(sheet.Compute(node, nullptr, 1).m_Decoration && Near(sheet.Compute(node, nullptr, 1).m_Opacity, .3),
          "before and after decorations preserve their existing grammar");
    auto compiled = sheet.CompilePart(node, "face", Hover);
    const auto revision = sheet.GetRevision();
    for (const auto* invalid : {"parts::face:unknown { color: red; }", "parts::face::after { color: red; }",
                                "parts::before:hover { content: ''; }", "parts::face child { color: red; }",
                                "parts::face { content: ''; }", "parts:: { color: red; }"})
        Check(!sheet.Parse(invalid, error) && sheet.GetRevision() == revision,
              "invalid named part reload is transactional");
    Check(sheet.Parse("parts::face { background: green; }", error), error);
    Style oldAssignments;
    compiled(oldAssignments);
    Check(oldAssignments.m_Background == Color{0, 0, 1, 1},
          "compiled assignments own their declaration values after reload");
    Check(sheet.Parse("numericinput { gap: 9px; } .numeric-increment { width: 41px; }", error), error);
    Check(sheet.Compute(StyleNode{"toolbar", "", {}}).m_Gap == 6 &&
              sheet.Compute(StyleNode{"contextmenu", "", {}}).m_Gap == 3 &&
              sheet.Compute(StyleNode{"tabbar", "", {}}).m_Gap == 4 &&
              sheet.Compute(StyleNode{"numericinput", "", {}}).m_Gap == 9,
          "composite default gaps remain CSS overridable");
    Check(sheet.Compute(StyleNode{"toolbutton", "", {"numeric-increment"}}).m_Width.m_Value == 41 &&
              sheet.Compute(StyleNode{"panel", "", {"toolbar-separator"}}).m_Width.m_Value == 1,
          "semantic composite metrics participate below the cascade");
}

void PalettesAndFocus(FontFace& font)
{
    PartsWidget detached;
    Check(detached.GetPartStyle("face").m_Width.m_Value == 12, "detached parts expose constructor defaults");
    detached.SetStyleProperty(StyleProperty::SelectionBackground, Color{1, 0, 0, 1});
    Check(detached.GetPartStyle("face").m_Background == Color{1, 0, 0, 1}, "detached defaults follow paint overrides");
    bool rejected = false;
    try
    {
        detached.RegisterStylePart("too-many-states", 127, [](const Style& owner, unsigned) { return owner; });
    }
    catch (const std::invalid_argument&)
    {
        rejected = true;
    }
    Check(rejected, "shared palette size is bounded before allocation");
    UiContext ui(font);
    auto& composite = ui.Root().Add<PartsWidget>();
    composite.SetFloatingBounds({20, 20, 180, 80});
    auto& editor = composite.Add<Button>("Focus");
    std::string error;
    Check(ui.SetStyleSheet(R"CSS(
        parts { color: red; }
        parts:focus-within { border-width: 3px; }
        parts::face:focus-within { border-radius: 8px; }
        parts::item:selected { background: blue; animation: flash 1s infinite; transition: opacity 1s; }
        @keyframes flash { from { opacity: .2; } to { opacity: .8; } }
    )CSS",
                           error),
          error);
    ui.Prepare(400, 240);
    Check(!ui.HasAnimations(), "virtual collection palettes do not start per-item motion");
    Check(composite.GetPartStyle("item", Selected).m_Animations.empty() &&
              composite.GetPartStyle("item", Selected).m_Transitions.empty(),
          "shared palettes discard motion specifications");
    ui.SetFocus(&editor);
    ui.Prepare(400, 240);
    Check(composite.GetStyle().m_BorderWidth == 3 && composite.GetPartStyle("face").m_Radius == 8,
          "focus within reaches composite owners and named parts");
    ui.SetFocus(nullptr);
    ui.Prepare(400, 240);
    Check(composite.GetStyle().m_BorderWidth == 0 && composite.GetPartStyle("face").m_Radius == 0,
          "focus within clears when focus leaves the subtree");
    const auto stats = ui.GetStats();
    composite.SetStyleProperty(StyleProperty::SelectionBackground, Color{0, 1, 0, 1});
    ui.Prepare(400, 240);
    Check(composite.GetPartStyle("face").m_Background == Color{0, 1, 0, 1},
          "paint-only owner overrides refresh compiled part defaults immediately");
    Check(ui.GetStats().m_LayoutPasses == stats.m_LayoutPasses &&
              ui.GetStats().m_StyleResolutions == stats.m_StyleResolutions,
          "refreshing part defaults does not match selectors or perform layout");
    composite.SetEnabled(false);
    ui.Prepare(400, 240);
    Check(!editor.IsEnabled(), "composite disable propagation remains intact");
}

void Motion(FontFace& font)
{
    UiContext ui(font);
    auto& widget = ui.Root().Add<PartsWidget>();
    widget.SetFloatingBounds({20, 20, 180, 80});
    std::string error;
    Check(ui.SetStyleSheet(R"CSS(
        parts::face { background: red; transition: background 1s linear; }
        parts::face:hover { background: blue; }
    )CSS",
                           error),
          error);
    ui.Prepare(400, 240);
    ui.PointerMove({50, 50});
    ui.Prepare(400, 240);
    Check(ui.HasAnimations(), "fixed-part hover starts one compiled timeline");
    const auto stats = ui.GetStats();
    ui.SetTime(.5);
    ui.Prepare(400, 240);
    Check(Near(widget.GetPartStyle("face").m_Background.m_R, .5) &&
              Near(widget.GetPartStyle("face").m_Background.m_B, .5),
          "part transitions interpolate paint values");
    Check(ui.GetStats().m_LayoutPasses == stats.m_LayoutPasses &&
              ui.GetStats().m_StyleResolutions == stats.m_StyleResolutions,
          "part color motion retains caches");
    ui.PointerMove({300, 200});
    ui.Prepare(400, 240);
    ui.SetTime(1);
    Check(Near(widget.GetPartStyle("face").m_Background.m_R, .75),
          "owner pointer leave reverses fixed-part hover without stale state");
    ui.SetTime(1.5);
    Check(!ui.HasAnimations() && widget.GetPartStyle("face").m_Background == Color{1, 0, 0, 1},
          "fixed-part transitions stop at their exact endpoint");
    Check(ui.SetStyleSheet(R"CSS(
        parts { selection-background-color: red; animation: tint 1s linear forwards; }
        @keyframes tint { from { selection-background-color: red; } to { selection-background-color: blue; } }
    )CSS",
                           error),
          error);
    ui.Prepare(400, 240);
    const auto inheritedStats = ui.GetStats();
    ui.SetTime(2);
    Check(Near(widget.GetPartStyle("face").m_Background.m_R, .5), "owner CSS motion refreshes part defaults");
    Check(ui.GetStats().m_StyleResolutions == inheritedStats.m_StyleResolutions,
          "owner motion refresh uses compiled assignments");
}

void SheetAnimation(FontFace& font)
{
    UiContext ui(font);
    auto& widget = ui.Root().Add<PartsWidget>();
    std::string error;
    const std::string initial =
        "parts::face { background: red; } parts::item { background: red; } parts::item:selected { background: blue; }";
    const std::string final =
        "parts::face { background: blue; } parts::item { background: blue; } parts::item:selected { background: red; }";
    AnimationOptions options;
    options.m_Duration = 1;
    options.m_Easing = EasingCurve(EasingCurve::Kind::Linear);
    Check(ui.SetStyleSheet(initial, error), error);
    ui.Prepare(400, 240);
    auto id = ui.AnimateStyleSheet(final, options, error);
    Check(id != 0, error);
    const auto stats = ui.GetStats();
    ui.SetTime(.5);
    ui.Prepare(400, 240);
    Check(Near(widget.GetPartStyle("face").m_Background.m_R, .5) &&
              Near(widget.GetPartStyle("item", 0).m_Background.m_R, .5) &&
              Near(widget.GetPartStyle("item", Selected).m_Background.m_R, .5),
          "whole stylesheet animation interpolates fixed and shared palette variants");
    Check(ui.GetStats().m_LayoutPasses == stats.m_LayoutPasses &&
              ui.GetStats().m_StyleResolutions == stats.m_StyleResolutions,
          "palette sheet animation is paint-only between endpoints");
    ui.Animations().Pause(id);
    ui.SetTime(.7);
    Check(Near(widget.GetPartStyle("face").m_Background.m_R, .5), "whole stylesheet part animation supports pause");
    ui.Animations().Resume(id);
    ui.SetTime(1.2);
    ui.Prepare(400, 240);
    Check(widget.GetPartStyle("face").m_Background == Color{0, 0, 1, 1} &&
              widget.GetPartStyle("item", Selected).m_Background == Color{1, 0, 0, 1},
          "whole stylesheet parts release overrides at their exact endpoint");
    id = ui.AnimateStyleSheet(initial, options, error);
    ui.SetTime(1.5);
    ui.Animations().Stop(id, AnimationStopMode::ReleaseStyle);
    ui.Prepare(400, 240);
    Check(widget.GetPartStyle("face").m_Background == Color{1, 0, 0, 1},
          "cancelling a sheet transition releases part overrides");
    id = ui.AnimateStyleSheet(final, options, error);
    ui.SetTime(1.8);
    const auto retained = widget.GetPartStyle("face").m_Background;
    ui.Animations().Stop(id, AnimationStopMode::KeepCurrent);
    ui.Prepare(400, 240);
    Check(widget.GetPartStyle("face").m_Background == retained, "KeepCurrent retains named-part presentation");
    widget.ClearStyleProperties();
    ui.Prepare(400, 240);
    Check(widget.GetPartStyle("face").m_Background == Color{0, 0, 1, 1},
          "clearing presentation overrides also releases retained named-part palettes");
    id = ui.AnimateStyleSheet(initial, options, error);
    ui.SetTime(2.1);
    const auto interrupted = widget.GetPartStyle("face").m_Background;
    const auto replacement = ui.AnimateStyleSheet(final, options, error);
    Check(replacement && widget.GetPartStyle("face").m_Background == interrupted,
          "replacing a sheet animation begins at the current part presentation");
    ui.Animations().Seek(replacement, 1);
    ui.Prepare(400, 240);
    Check(widget.GetPartStyle("face").m_Background == Color{0, 0, 1, 1},
          "replacement part animation releases the superseded tracks");
    options.m_Alternate = true;
    options.m_Iterations = 2;
    id = ui.AnimateStyleSheet(initial, final, options, error);
    ui.Animations().Seek(id, 2);
    ui.Prepare(400, 240);
    Check(widget.GetPartStyle("face").m_Background == Color{1, 0, 0, 1} &&
              widget.GetPartStyle("item", Selected).m_Background == Color{0, 0, 1, 1},
          "even alternating sheet animation restores initial fixed and shared parts");
}

void PopupScope(FontFace& font)
{
    UiContext ui(font);
    auto& owner = ui.Root().Add<Widget>("splittoolbutton");
    owner.AddClass("accent");
    auto& popup = ui.AddOwnedPopup(std::make_unique<Widget>("contextmenu"), owner);
    auto& item = popup.Add<Button>("Action");
    popup.SetVisible(true);
    std::string error;
    Check(ui.SetStyleSheet(".accent { color: blue; } .accent > contextmenu { background: red; } .accent contextmenu "
                           "button { border-width: 7px; }",
                           error),
          error);
    ui.Prepare(400, 240);
    Check(popup.GetParent() == &ui.Root() && popup.GetStyle().m_Background == Color{1, 0, 0, 1} &&
              popup.GetStyle().m_Color == Color{0, 0, 1, 1} && item.GetStyle().m_BorderWidth == 7,
          "owned overlays keep root geometry and inherit logical owner selector scope");
    WidgetRef observed(&popup);
    ui.Root().RemoveChild(owner);
    ui.Prepare(400, 240);
    Check(!observed.Get(), "destroying a logical owner removes the overlay without dangling selector pointers");
}

void CacheAndLifetime(FontFace& font)
{
    UiContext ui(font);
    auto& group = ui.Root().Add<Widget>();
    for (int i = 0; i < 500; ++i)
        group.Add<PartsWidget>().SetFloatingBounds({float(i % 25 * 20), float(i / 25 * 20), 18, 18});
    std::string error;
    Check(ui.SetStyleSheet(R"CSS(
        parts::face { animation: wave 1s linear infinite alternate; }
        @keyframes wave { from { background: red; opacity: .4; } to { background: blue; opacity: .8; } }
    )CSS",
                           error),
          error);
    ui.Prepare(600, 500);
    const auto stats = ui.GetStats();
    for (int frame = 1; frame <= 240; ++frame)
    {
        ui.SetTime(frame / 120.0);
        ui.Prepare(600, 500);
    }
    Check(ui.GetStats().m_LayoutPasses == stats.m_LayoutPasses &&
              ui.GetStats().m_StyleResolutions == stats.m_StyleResolutions,
          "500 fixed parts retain style and layout caches over 240 animation ticks");
    group.SetVisible(false);
    ui.Prepare(600, 500);
    Check(!ui.HasAnimations(), "hidden owner subtrees do not request frames for part timelines");
    group.SetVisible(true);
    ui.Prepare(600, 500);
    Check(ui.HasAnimations(), "visible part timelines resume normally");
    group.ClearChildren();
    ui.SetTime(3);
    ui.Prepare(600, 500);
    Check(!ui.HasAnimations(), "deleted part owners leave no active timeline references");
    auto& replacement = group.Add<PartsWidget>();
    replacement.SetId("replacement");
    Check(ui.SetStyleSheet("#replacement::face { background: green; }", error), error);
    ui.Prepare(600, 500);
    Check(!ui.HasAnimations() && Near(replacement.GetPartStyle("face").m_Background.m_G, .5),
          "reused widget allocations do not retain a previous owner's part animations");
}
} // namespace

int main()
{
    try
    {
        Parser();
        FontFace font(ESTUDIOUI_FONT_PATH);
        PalettesAndFocus(font);
        Motion(font);
        SheetAnimation(font);
        PopupScope(font);
        CacheAndLifetime(font);
        std::cout << s_Checks << " style part checks passed.\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Style parts: " << error.what() << '\n';
        return 1;
    }
}
