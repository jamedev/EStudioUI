#include <EStudioUI/Animation.h>

#include <cmath>
#include <limits>
#include <stdexcept>
#include <type_traits>

namespace EStudioUI
{
namespace
{
StyleValue Interpolate(const StyleValue& from, const StyleValue& to, double progress)
{
    return std::visit(
        [&](const auto& first) -> StyleValue
        {
            using Value = std::decay_t<decltype(first)>;
            const auto& last = std::get<Value>(to);
            if constexpr (std::is_same_v<Value, bool> || std::is_same_v<Value, std::string>)
                return progress >= 1 ? last : first;
            else if constexpr (std::is_same_v<Value, Length>)
                return Length{first.m_Unit, std::max(first.m_Unit == Length::Unit::Fraction ? 0.0001f : 0.0f,
                                                     Tween(first.m_Value, last.m_Value, progress))};
            else
                return Tween(first, last, progress);
        },
        from);
}
} // namespace

float Tween(float from, float to, double progress)
{
    const double value = Tween(static_cast<double>(from), static_cast<double>(to), progress);
    if (std::abs(value) > std::numeric_limits<float>::max())
        throw std::invalid_argument("Animation sample exceeds the float range.");
    return static_cast<float>(value);
}

double Tween(double from, double to, double progress)
{
    if (!std::isfinite(from) || !std::isfinite(to) || !std::isfinite(progress))
        throw std::invalid_argument("Animation endpoints and progress must be finite.");
    const double value = std::lerp(from, to, progress);
    if (!std::isfinite(value))
        throw std::invalid_argument("Animation sample exceeds the double range.");
    return value;
}

Point Tween(Point from, Point to, double progress)
{
    return {Tween(from.m_X, to.m_X, progress), Tween(from.m_Y, to.m_Y, progress)};
}

Rect Tween(Rect from, Rect to, double progress)
{
    if (from.m_Width < 0 || from.m_Height < 0 || to.m_Width < 0 || to.m_Height < 0)
        throw std::invalid_argument("Animated rectangles require nonnegative dimensions.");
    return {Tween(from.m_X, to.m_X, progress), Tween(from.m_Y, to.m_Y, progress),
            std::max(0.0f, Tween(from.m_Width, to.m_Width, progress)),
            std::max(0.0f, Tween(from.m_Height, to.m_Height, progress))};
}

Color Tween(Color from, Color to, double progress)
{
    const auto validate = [](Color color)
    {
        for (float component : {color.m_R, color.m_G, color.m_B, color.m_A})
            if (!std::isfinite(component) || component < 0 || component > 1)
                throw std::invalid_argument("Animated colors require finite RGBA in [0,1].");
    };
    validate(from);
    validate(to);
    progress = std::clamp(progress, 0.0, 1.0);
    if (progress == 0)
        return from;
    if (progress == 1)
        return to;
    const float alpha = Tween(from.m_A, to.m_A, progress);
    if (alpha <= 0)
        return {0, 0, 0, 0};
    return {Tween(from.m_R * from.m_A, to.m_R * to.m_A, progress) / alpha,
            Tween(from.m_G * from.m_A, to.m_G * to.m_A, progress) / alpha,
            Tween(from.m_B * from.m_A, to.m_B * to.m_A, progress) / alpha, alpha};
}

Insets Tween(Insets from, Insets to, double progress)
{
    return {std::max(0.0f, Tween(from.m_Top, to.m_Top, progress)),
            std::max(0.0f, Tween(from.m_Right, to.m_Right, progress)),
            std::max(0.0f, Tween(from.m_Bottom, to.m_Bottom, progress)),
            std::max(0.0f, Tween(from.m_Left, to.m_Left, progress))};
}

Shadow Tween(Shadow from, Shadow to, double progress)
{
    return {Tween(from.m_X, to.m_X, progress), Tween(from.m_Y, to.m_Y, progress),
            std::max(0.0f, Tween(from.m_Blur, to.m_Blur, progress)), Tween(from.m_Color, to.m_Color, progress)};
}

void AnimationOptions::Validate() const
{
    if (!std::isfinite(m_Duration) || !std::isfinite(m_Delay) || m_Duration < 0 || m_Delay < 0 ||
        (m_Duration == 0 && m_Iterations == 0) ||
        (m_Iterations != 0 && !std::isfinite(m_Delay + m_Duration * m_Iterations)))
        throw std::invalid_argument(
            "Animation timing must be finite and nonnegative; infinite repeats need a duration.");
}

Animation::Animation(AnimationOptions options) : m_Options(std::move(options))
{
    m_Options.Validate();
}

Animation& Animation::AddCustom(Widget& widget, std::string channel, std::function<void(Widget&, double)> update)
{
    if (channel.empty() || !update)
        throw std::invalid_argument("Animation tracks require a property channel and callback.");
    if (std::any_of(m_Tracks.begin(), m_Tracks.end(),
                    [&](const auto& track) { return track.m_Target.Get() == &widget && track.m_Channel == channel; }))
        throw std::invalid_argument("An animation cannot write the same widget property twice.");
    m_Tracks.push_back({WidgetRef(&widget), std::move(channel), std::move(update), {}, true});
    return *this;
}

Animation& Animation::AddStyleProperty(Widget& widget, StyleProperty property, StyleValue from, StyleValue to,
                                       bool releaseOnFinish)
{
    Style validation;
    SetStyleValue(validation, property, from);
    from = GetStyleValue(validation, property);
    SetStyleValue(validation, property, to);
    to = GetStyleValue(validation, property);
    if (auto* first = std::get_if<Length>(&from))
    {
        auto& last = std::get<Length>(to);
        if (first->m_Unit != last.m_Unit)
        {
            if (first->m_Unit == Length::Unit::Auto || first->m_Unit == Length::Unit::Fraction ||
                last.m_Unit == Length::Unit::Auto || last.m_Unit == Length::Unit::Fraction)
                throw std::invalid_argument(
                    "Mixed content/Fill lengths need resolved pixel endpoints; use AnimateStyleSheet.");
            const auto parent = widget.GetParent() ? widget.GetParent()->GetContentBounds() : Rect{};
            const float size = property == StyleProperty::Width ? parent.m_Width : parent.m_Height;
            *first = {Length::Unit::Pixels, first->Resolve(size, 0)};
            last = {Length::Unit::Pixels, last.Resolve(size, 0)};
        }
    }
    AddCustom(widget, "style:" + std::string(GetStylePropertyName(property)),
              [property, from = std::move(from), to = std::move(to)](Widget& target, double progress)
              { target.SetStyleProperty(property, Interpolate(from, to, progress)); });
    m_Tracks.back().m_Release = [property](Widget& target) { target.ClearStyleProperty(property); };
    m_Tracks.back().m_Discrete = std::holds_alternative<bool>(GetStyleValue(validation, property)) ||
                                 std::holds_alternative<std::string>(GetStyleValue(validation, property));
    m_Tracks.back().m_ReleaseOnFinish = releaseOnFinish;
    return *this;
}

Animation& Animation::AddStyle(Widget& widget, const Style& from, const Style& to,
                               std::initializer_list<StyleProperty> properties)
{
    if (properties.size())
    {
        for (auto property : properties)
            AddStyleProperty(widget, property, GetStyleValue(from, property), GetStyleValue(to, property));
    }
    else
    {
        for (int i = 0; i < static_cast<int>(StyleProperty::Count); ++i)
        {
            const auto property = static_cast<StyleProperty>(i);
            auto first = GetStyleValue(from, property);
            auto last = GetStyleValue(to, property);
            if (first != last)
                AddStyleProperty(widget, property, std::move(first), std::move(last));
        }
    }
    return *this;
}

Animation& Animation::AddBounds(Widget& widget, Rect from, Rect to)
{
    static_cast<void>(Tween(from, to, 0));
    return AddCustom(widget, "bounds", [from, to](Widget& target, double progress)
                     { target.SetFloatingBounds(Tween(from, to, progress)); });
}

Animation& Animation::AddTranslation(Widget& widget, Point from, Point to)
{
    AddStyleProperty(widget, StyleProperty::TranslateX, from.m_X, to.m_X);
    AddStyleProperty(widget, StyleProperty::TranslateY, from.m_Y, to.m_Y);
    return *this;
}
} // namespace EStudioUI
