#include <EStudioUI/Bindings.h>
#include <EStudioUI/SearchBox.h>
#include <EStudioUI/RangeSlider.h>
#include <EStudioUI/TagEditor.h>
#include <EStudioUI/FontPicker.h>
#include <EStudioUI/DateTimeEdit.h>
#include <cmath>
#include <stdexcept>
#include <unordered_set>

namespace EStudioUI
{
Binding BindText(SearchBox& target, State<std::string>& state, BindingMode mode)
{
    return BindText(target.GetEditor(), state, mode);
}

Binding BindValue(RangeSlider& target, State<RangeValues>& state, BindingMode mode)
{
    const auto setter = [](RangeSlider& live, RangeValues value) { live.SetValues(value); };
    auto [binding, data] = Detail::MakeBinding(
        target, state, setter,
        [](RangeSlider& live, RangeValues value)
        {
            if (!std::isfinite(value.m_Lower) || !std::isfinite(value.m_Upper) || value.m_Lower > value.m_Upper ||
                value.m_Lower < live.GetMinimum() || value.m_Upper > live.GetMaximum())
            {
                throw std::invalid_argument("Bound interval must be finite, ordered and inside the slider bounds.");
            }
        });
    if (mode == BindingMode::TwoWay && data->IsConnected())
    {
        Detail::AddBindingWriter(data, target, state, target.ValuesChanged(), setter,
                                 [](const RangeSlider& live) { return live.GetValues(); });
    }
    return binding;
}

Binding BindTags(TagEditor& target, State<std::vector<std::string>>& state, BindingMode mode)
{
    const auto setter = [](TagEditor& live, const std::vector<std::string>& value) { live.SetTags(value); };
    auto [binding, data] = Detail::MakeBinding(
        target, state, setter,
        [](TagEditor&, const std::vector<std::string>& value)
        {
            if (value.size() > 128)
            {
                throw std::length_error("Bound tag set exceeds 128 entries.");
            }
            std::unordered_set<std::string> unique;
            for (const auto& tag : value)
            {
                if (tag.empty() || tag.size() > 256 || !unique.insert(tag).second)
                {
                    throw std::invalid_argument("Bound tags must be unique, nonempty and at most 256 bytes.");
                }
            }
        });
    if (mode == BindingMode::TwoWay && data->IsConnected())
    {
        Detail::AddBindingWriter(data, target, state, target.TagsChanged(), setter,
                                 [](const TagEditor& live) { return live.GetTags(); });
    }
    return binding;
}

namespace
{
template <class Target> Binding FontBinding(Target& target, State<FontSelection>& state, BindingMode mode)
{
    const auto setter = [](Target& live, const FontSelection& value) { live.SetFont(value); };
    auto [binding, data] = Detail::MakeBinding(target, state, setter, [](Target&, const FontSelection& value)
                                               { FontEditor::Validate(value); });
    if (mode == BindingMode::TwoWay && data->IsConnected())
    {
        Detail::AddBindingWriter(data, target, state, target.FontChanged(), setter,
                                 [](const Target& live) { return live.GetFont(); });
    }
    return binding;
}
} // namespace

Binding BindFont(FontEditor& target, State<FontSelection>& state, BindingMode mode)
{
    return FontBinding(target, state, mode);
}

Binding BindFont(FontPicker& target, State<FontSelection>& state, BindingMode mode)
{
    return FontBinding(target, state, mode);
}

Binding BindDate(DateEdit& target, State<std::optional<CalendarDate>>& state, BindingMode mode)
{
    const auto setter = [](DateEdit& live, const std::optional<CalendarDate>& value) { live.SetDate(value); };
    auto [binding, data] = Detail::MakeBinding(target, state, setter,
                                               [](DateEdit&, const std::optional<CalendarDate>& value)
                                               {
                                                   if (value && !value->IsValid())
                                                   {
                                                       throw std::invalid_argument("Bound date must be valid.");
                                                   }
                                               });
    if (mode == BindingMode::TwoWay && data->IsConnected())
    {
        Detail::AddBindingWriter(data, target, state, target.DateChanged(), setter,
                                 [](const DateEdit& live) { return live.GetDate(); });
    }
    return binding;
}

Binding BindTime(TimeEdit& target, State<std::optional<ClockTime>>& state, BindingMode mode)
{
    const auto setter = [](TimeEdit& live, const std::optional<ClockTime>& value) { live.SetTime(value); };
    auto [binding, data] = Detail::MakeBinding(target, state, setter,
                                               [](TimeEdit&, const std::optional<ClockTime>& value)
                                               {
                                                   if (value && !value->IsValid())
                                                   {
                                                       throw std::invalid_argument("Bound local time must be valid.");
                                                   }
                                               });
    if (mode == BindingMode::TwoWay && data->IsConnected())
    {
        Detail::AddBindingWriter(data, target, state, target.TimeChanged(), setter,
                                 [](const TimeEdit& live) { return live.GetTime(); });
    }
    return binding;
}

Binding BindValue(DateTimeEdit& target, State<std::optional<DateTimeValue>>& state, BindingMode mode)
{
    const auto setter = [](DateTimeEdit& live, const std::optional<DateTimeValue>& value) { live.SetValue(value); };
    auto [binding, data] =
        Detail::MakeBinding(target, state, setter,
                            [](DateTimeEdit&, const std::optional<DateTimeValue>& value)
                            {
                                if (value && !value->IsValid())
                                {
                                    throw std::invalid_argument("Bound local datetime must be valid.");
                                }
                            });
    if (mode == BindingMode::TwoWay && data->IsConnected())
    {
        Detail::AddBindingWriter(data, target, state, target.ValueChanged(), setter,
                                 [](const DateTimeEdit& live) { return live.GetValue(); });
    }
    return binding;
}
} // namespace EStudioUI
