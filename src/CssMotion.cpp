#include <EStudioUI/CssMotion.h>
#include <EStudioUI/UiContext.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <optional>

namespace EStudioUI
{
struct CssMotion::Impl
{
    struct Knot
    {
        double m_Offset;
        StyleValue m_Value;
        std::optional<EasingCurve> m_Easing;
    };

    struct Track
    {
        StyleProperty m_Property;
        std::vector<Knot> m_Knots;
    };

    struct Timeline
    {
        CssAnimation m_Spec;
        std::vector<Track> m_Tracks;
        double m_Elapsed = 0;
        double m_LastTime = 0;
    };

    struct Transition
    {
        StyleProperty m_Property;
        StyleValue m_From, m_To;
        CssTransition m_Spec;
        double m_Start = 0;
    };

    struct Layer
    {
        bool m_Initialized = false;
        Style m_Base, m_Presentation;
        std::vector<Transition> m_Transitions;
        std::vector<Timeline> m_Animations;
        std::vector<StyleProperty> m_Properties;
    };

    struct Entry
    {
        WidgetRef m_Widget;
        std::array<Layer, 3> m_Layers;
    };

    UiContext& m_Context;
    std::map<const Widget*, Entry> m_Entries;

    explicit Impl(UiContext& context) : m_Context(context)
    {
    }

    static bool Running(const Timeline& timeline)
    {
        const auto& spec = timeline.m_Spec;
        return !spec.m_Paused && spec.m_Duration > 0 &&
               (!spec.m_Iterations || timeline.m_Elapsed < spec.m_Delay + spec.m_Duration * spec.m_Iterations);
    }

    static std::vector<Track> Compile(const CssAnimation& spec, const Style& base, const StyleSheet& sheet)
    {
        std::map<StyleProperty, std::vector<Knot>> values;
        const auto* blocks = sheet.FindKeyframes(spec.m_Name);
        if (!blocks)
            return {};
        Style sample = base;
        double previousOffset = -1;
        for (const auto& block : *blocks)
        {
            if (block.m_Offset != previousOffset)
                sample = base;
            block.m_Apply(sample);
            previousOffset = block.m_Offset;
            for (const auto& name : block.m_Properties)
            {
                for (auto property : FindStyleProperties(name))
                {
                    auto& knots = values[property];
                    Knot knot{block.m_Offset, GetStyleValue(sample, property), block.m_Easing};
                    if (!knots.empty() && knots.back().m_Offset == block.m_Offset)
                        knots.back() = std::move(knot);
                    else
                        knots.push_back(std::move(knot));
                }
            }
        }
        std::vector<Track> tracks;
        for (auto& [property, knots] : values)
        {
            const auto underlying = GetStyleValue(base, property);
            if (knots.front().m_Offset > 0)
                knots.insert(knots.begin(), {0, underlying, {}});
            if (knots.back().m_Offset < 1)
                knots.push_back({1, underlying, {}});
            tracks.push_back({property, std::move(knots)});
        }
        return tracks;
    }

    static StyleValue SampleValue(const StyleValue& a, const StyleValue& b, double progress)
    {
        if (const auto* length = std::get_if<Length>(&a); length && length->m_Unit != std::get<Length>(b).m_Unit)
            return progress >= 1 ? b : a;
        return InterpolateStyleValue(a, b, progress);
    }

    static void Sample(Layer& layer, double time)
    {
        for (auto property : layer.m_Properties)
            SetStyleValue(layer.m_Presentation, property, GetStyleValue(layer.m_Base, property));
        for (const auto& timeline : layer.m_Animations)
        {
            const auto& spec = timeline.m_Spec;
            const double elapsed = timeline.m_Elapsed - spec.m_Delay;
            const bool before = elapsed < 0;
            const bool ended =
                spec.m_Iterations && (spec.m_Duration == 0 || elapsed >= spec.m_Duration * spec.m_Iterations);
            if ((before && !spec.m_FillBefore) || (ended && !spec.m_FillAfter))
                continue;
            double iteration = 0, fraction = 0;
            if (ended)
            {
                iteration = spec.m_Iterations - 1;
                fraction = 1;
            }
            else if (!before && spec.m_Duration > 0)
            {
                iteration = std::floor(elapsed / spec.m_Duration);
                fraction = (elapsed - iteration * spec.m_Duration) / spec.m_Duration;
            }
            if (spec.m_Reverse != (spec.m_Alternate && std::fmod(iteration, 2) >= 1))
                fraction = 1 - fraction;
            for (const auto& track : timeline.m_Tracks)
            {
                const auto& knots = track.m_Knots;
                const auto upper =
                    std::upper_bound(knots.begin(), knots.end(), fraction,
                                     [](double value, const Knot& knot) { return value < knot.m_Offset; });
                if (upper == knots.begin())
                    SetStyleValue(layer.m_Presentation, track.m_Property, knots.front().m_Value);
                else if (upper == knots.end())
                    SetStyleValue(layer.m_Presentation, track.m_Property, knots.back().m_Value);
                else
                {
                    const auto& left = *(upper - 1);
                    const auto& right = *upper;
                    const double progress = (fraction - left.m_Offset) / (right.m_Offset - left.m_Offset);
                    const auto& easing = left.m_Easing ? *left.m_Easing : spec.m_Easing;
                    SetStyleValue(layer.m_Presentation, track.m_Property,
                                  SampleValue(left.m_Value, right.m_Value, easing.Evaluate(progress)));
                }
            }
        }
        // Transitions override named animation samples, matching CSS animation precedence.
        for (const auto& transition : layer.m_Transitions)
        {
            const double elapsed = time - transition.m_Start - transition.m_Spec.m_Delay;
            const double progress = std::clamp(elapsed / transition.m_Spec.m_Duration, 0.0, 1.0);
            SetStyleValue(
                layer.m_Presentation, transition.m_Property,
                SampleValue(transition.m_From, transition.m_To, transition.m_Spec.m_Easing.Evaluate(progress)));
        }
        std::erase_if(
            layer.m_Transitions, [time](const auto& transition)
            { return time >= transition.m_Start + transition.m_Spec.m_Delay + transition.m_Spec.m_Duration; });
    }

    static void Advance(Layer& layer, double time)
    {
        for (auto& timeline : layer.m_Animations)
        {
            if (!timeline.m_Spec.m_Paused)
                timeline.m_Elapsed += std::max(0.0, time - timeline.m_LastTime);
            timeline.m_LastTime = time;
        }
        Sample(layer, time);
    }
};

CssMotion::CssMotion(UiContext& context) : m_Impl(std::make_unique<Impl>(context))
{
}

CssMotion::~CssMotion() = default;

void CssMotion::Reset()
{
    m_Impl->m_Entries.clear();
}

const Style& CssMotion::Resolve(Widget& widget, int layerIndex, Style base, const StyleSheet& sheet)
{
    auto& self = *m_Impl;
    auto& entry = self.m_Entries[&widget];
    if (entry.m_Widget.Get() != &widget)
        entry = {WidgetRef(&widget), {}};
    auto& layer = entry.m_Layers.at(static_cast<std::size_t>(layerIndex));
    const double now = self.m_Context.GetTime();
    if (layer.m_Initialized)
        Impl::Advance(layer, now);
    if (layer.m_Initialized)
    {
        for (int i = 0; i < static_cast<int>(StyleProperty::Count); ++i)
        {
            const auto property = static_cast<StyleProperty>(i);
            const auto to = GetStyleValue(base, property);
            if (GetStyleValue(layer.m_Base, property) == to)
                continue;
            const CssTransition* selected = nullptr;
            for (const auto& definition : base.m_Transitions)
            {
                const auto properties = FindStyleProperties(definition.m_Property);
                if (std::find(properties.begin(), properties.end(), property) != properties.end())
                    selected = &definition;
            }
            const auto from = GetStyleValue(layer.m_Presentation, property);
            std::erase_if(layer.m_Transitions,
                          [property](const auto& transition) { return transition.m_Property == property; });
            if (selected && selected->m_Duration > 0 && from != to)
                layer.m_Transitions.push_back({property, from, to, *selected, now});
        }
    }
    else
        layer.m_Initialized = true;
    std::erase_if(layer.m_Transitions,
                  [&](const auto& transition)
                  {
                      const CssTransition* selected = nullptr;
                      for (const auto& definition : base.m_Transitions)
                      {
                          const auto properties = FindStyleProperties(definition.m_Property);
                          if (std::find(properties.begin(), properties.end(), transition.m_Property) !=
                              properties.end())
                              selected = &definition;
                      }
                      return !selected || selected->m_Duration <= 0;
                  });

    std::vector<Impl::Timeline> animations;
    for (std::size_t i = 0; i < base.m_Animations.size(); ++i)
    {
        const auto& spec = base.m_Animations[i];
        Impl::Timeline timeline{spec, {}, 0, now};
        if (i < layer.m_Animations.size())
        {
            auto old = layer.m_Animations[i].m_Spec;
            old.m_Paused = spec.m_Paused;
            if (old == spec)
                timeline.m_Elapsed = layer.m_Animations[i].m_Elapsed;
        }
        if (!spec.m_Name.empty() && (spec.m_Iterations || spec.m_Duration > 0))
            timeline.m_Tracks = Impl::Compile(spec, base, sheet);
        animations.push_back(std::move(timeline));
    }
    layer.m_Base = std::move(base);
    layer.m_Presentation = layer.m_Base;
    layer.m_Animations = std::move(animations);
    layer.m_Properties.clear();
    const auto addProperty = [&](StyleProperty property)
    {
        if (std::find(layer.m_Properties.begin(), layer.m_Properties.end(), property) == layer.m_Properties.end())
            layer.m_Properties.push_back(property);
    };
    for (const auto& transition : layer.m_Transitions)
        addProperty(transition.m_Property);
    for (const auto& timeline : layer.m_Animations)
        for (const auto& track : timeline.m_Tracks)
            addProperty(track.m_Property);
    Impl::Sample(layer, now);
    return layer.m_Presentation;
}

void CssMotion::Update(double time)
{
    auto& self = *m_Impl;
    std::erase_if(self.m_Entries,
                  [&](const auto& item)
                  {
                      const auto* widget = item.second.m_Widget.Get();
                      return !widget || widget->GetContext() != &self.m_Context;
                  });
    for (auto& [address, entry] : self.m_Entries)
    {
        auto& widget = *entry.m_Widget.Get();
        for (std::size_t index = 0; index < entry.m_Layers.size(); ++index)
        {
            auto& layer = entry.m_Layers[index];
            const bool active =
                !layer.m_Transitions.empty() ||
                std::any_of(layer.m_Animations.begin(), layer.m_Animations.end(),
                            [](const auto& timeline) { return !timeline.m_Tracks.empty() && Impl::Running(timeline); });
            if (!active)
                continue;
            Impl::Advance(layer, time);
            self.m_Context.RequestRepaint();
            if (index != 0)
                continue;
            for (auto property : layer.m_Properties)
            {
                if (widget.m_StyleOverrides.contains(property))
                    continue;
                if (widget.m_WidthRule && (property == StyleProperty::Width || property == StyleProperty::MinWidth ||
                                           property == StyleProperty::MaxWidth))
                    continue;
                if (widget.m_HeightRule && (property == StyleProperty::Height || property == StyleProperty::MinHeight ||
                                            property == StyleProperty::MaxHeight))
                    continue;
                if ((widget.m_Direction && property == StyleProperty::Row) ||
                    (widget.m_Gap && property == StyleProperty::Gap))
                    continue;
                auto value = GetStyleValue(layer.m_Presentation, property);
                if (property == StyleProperty::Displayed && !widget.m_Visible)
                    value = false;
                if (GetStyleValue(widget.m_Style, property) == value)
                    continue;
                SetStyleValue(widget.m_Style, property, value);
                if (!IsPaintOnlyProperty(property))
                    self.m_Context.Invalidate();
            }
        }
    }
}

const Style* CssMotion::Decoration(const Widget& widget, int layerIndex) const
{
    const auto found = m_Impl->m_Entries.find(&widget);
    if (found == m_Impl->m_Entries.end() || found->second.m_Widget.Get() != &widget)
        return nullptr;
    const auto& layer = found->second.m_Layers.at(static_cast<std::size_t>(layerIndex));
    return layer.m_Initialized && layer.m_Presentation.m_Decoration && layer.m_Presentation.m_Displayed
               ? &layer.m_Presentation
               : nullptr;
}

bool CssMotion::HasRunning() const
{
    for (const auto& [address, entry] : m_Impl->m_Entries)
    {
        const auto* widget = entry.m_Widget.Get();
        if (!widget || !widget->IsVisible())
            continue;
        for (std::size_t i = 0; i < entry.m_Layers.size(); ++i)
        {
            const auto& layer = entry.m_Layers[i];
            if (i != 0 && !layer.m_Presentation.m_Decoration)
                continue;
            if (!layer.m_Transitions.empty())
                return true;
            for (const auto& timeline : layer.m_Animations)
                if (!timeline.m_Tracks.empty() && Impl::Running(timeline))
                    return true;
        }
    }
    return false;
}
} // namespace EStudioUI
