#include <EStudioUI/Bindings.h>
#include <EStudioUI/DateTimeEdit.h>
#include <EStudioUI/FontPicker.h>
#include <EStudioUI/RangeSlider.h>
#include <EStudioUI/TagEditor.h>
using namespace EStudioUI;

//! Keeps application values outside widgets and uses ordinary receiver-bound bindings.
void BuildPickers(Widget& parent, State<RangeValues>& interval, State<FontSelection>& font,
                  State<std::vector<std::string>>& tags, std::vector<ScopedBinding>& bindings)
{
    auto& slider = parent.Add<RangeSlider>();
    bindings.emplace_back(BindValue(slider, interval));
    auto& tagEditor = parent.Add<TagEditor>();
    bindings.emplace_back(BindTags(tagEditor, tags));
    auto& picker = parent.Add<FontPicker>();
    bindings.emplace_back(BindFont(picker, font));
    auto& date = parent.Add<DateEdit>();
    date.SetDate(CalendarDate{2028, 2, 29});
    auto& datetime = parent.Add<DateTimeEdit>();
    datetime.SetValue(DateTimeValue{{2028, 2, 29}, {9, 30, 0, 0}});
}

int main()
{
    return 0;
}
