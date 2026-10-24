#include <EStudioUI/Bindings.h>
#include <EStudioUI/SegmentedControl.h>
#include <EStudioUI/TagEditor.h>
#include <EStudioUI/RangeSlider.h>
#include <EStudioUI/LinkLabel.h>
#include <EStudioUI/DateTimeEdit.h>
#include <EStudioUI/FontPicker.h>
#include <EStudioUI/Popup.h>
#include <EStudioUI/SemanticTree.h>
#include <EStudioUI/ThemeController.h>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace EStudioUI;

namespace
{
int s_Checks = 0;

void Check(bool value, const char* message)
{
    ++s_Checks;
    if (!value)
    {
        throw std::runtime_error(message);
    }
}

template <class F> void Throws(F action, const char* message)
{
    bool threw = false;
    try
    {
        action();
    }
    catch (const std::exception&)
    {
        threw = true;
    }
    Check(threw, message);
}

Point Center(const Widget& widget)
{
    const auto b = widget.GetBounds();
    return {b.m_X + b.m_Width / 2, b.m_Y + b.m_Height / 2};
}

void Temporal()
{
    Check(CalendarDate::Parse("2000-02-29").has_value() && !CalendarDate::Parse("1900-02-29"),
          "Gregorian century and 400-year leap rules are enforced");
    Check(!CalendarDate::Parse("0000-01-01") && !CalendarDate::Parse("2026-04-31") && !CalendarDate::Parse("2026-1-01"),
          "Civil dates reject unsupported years, impossible days and non-ISO widths");
    Check(CalendarDate{9999, 12, 31}.ToString() == "9999-12-31", "Civil date upper boundary round-trips");
    Check(ClockTime::Parse("23:59:59.999")->ToString() == "23:59:59.999" &&
              ClockTime::Parse("12:30")->ToString() == "12:30:00",
          "Clock formats round-trip seconds and milliseconds");
    Check(!ClockTime::Parse("24:00") && !ClockTime::Parse("00:60") && !ClockTime::Parse("00:00:60"),
          "Clock bounds reject overflow and leap-second ambiguity");
    Check(DateTimeValue::Parse("2024-02-29T12:30")->ToString() == "2024-02-29T12:30:00" &&
              !DateTimeValue::Parse("2024-02-29T12:30Z"),
          "Local datetime format never silently infers a timezone");
}

void Choices(FontFace& font)
{
    UiContext ui(font);
    ThemeController theme(ui);
    auto& segments = ui.Root().Add<SegmentedControl>();
    segments.SetItems({"Design", "Inspect", "Results"});
    segments.SetWidth(SizeRule::Fixed(400));
    ui.Prepare(800, 600);
    segments.SetCurrentIndex(1);
    auto* radio = dynamic_cast<RadioButton*>(segments.GetChildren()[1].get());
    ui.SetFocus(radio);
    ui.KeyEvent(Key::Right, true);
    Check(segments.GetCurrentIndex() == 2, "Segmented control reuses exclusive radio keyboard behavior");
    auto& multi = ui.Root().Add<MultiSelectComboBox>();
    multi.SetItems({"Steel", "Aluminum", "Titanium"});
    const auto snapshot = multi.GetModel()->GetSnapshot();
    multi.SetSelectedItems({snapshot.GetItem(0), snapshot.GetItem(2)});
    Check(multi.GetSelectionModel()->GetSnapshot()->m_Items.size() == 2,
          "Multi-select owns canonical selected identities");
    multi.ShowPopup();
    ui.Prepare(800, 600);
    Check(ui.GetPopup() && multi.GetSelectionModel()->GetMode() == SelectionMode::Multiple,
          "Multiple selection presents a virtualized ordinary popup");
    multi.HidePopup();
    auto& tags = ui.Root().Add<TagEditor>();
    State<std::vector<std::string>> value(ui.States(), {"load", "steel"});
    auto binding = BindTags(tags, value);
    tags.AddTag("mesh");
    Check(value.Get().size() == 3, "Tag edits publish through typed state bindings");
    Throws([&] { value.Set({"duplicate", "duplicate"}); },
           "Tag binding rejects invalid final candidates before commit");
    Check(tags.GetTags().size() == 3 && value.Get().size() == 3,
          "Rejected tag candidates retain canonical values and presentation");
    tags.SetSuggestions({"steel", "mesh", "concrete"});
    tags.RemoveTag("steel");
    Check(value.Get() == std::vector<std::string>({"load", "mesh"}), "Chip removal preserves remaining tag order");
}

void Range(FontFace& font)
{
    UiContext ui(font);
    auto& slider = ui.Root().Add<RangeSlider>();
    slider.SetWidth(SizeRule::Fixed(400));
    slider.SetHeight(SizeRule::Fixed(40));
    State<RangeValues> interval(ui.States(), {20, 80});
    auto binding = BindValue(slider, interval);
    ui.Prepare(800, 600);
    ui.SetFocus(&slider);
    ui.KeyEvent(Key::End, true);
    Check(interval.Get() == RangeValues{80, 80}, "Lower endpoint clamps at upper without crossing");
    ui.KeyEvent(Key::Tab, true);
    ui.KeyEvent(Key::Home, true);
    Check(interval.Get() == RangeValues{80, 80}, "Upper endpoint clamps at lower without crossing");
    slider.SetValues({25, 75});
    const auto b = slider.GetContentBounds();
    ui.PointerDown({b.m_X + b.m_Width * .25f, b.m_Y + b.m_Height / 2});
    ui.PointerMove({b.m_X + b.m_Width, b.m_Y + b.m_Height / 2});
    ui.PointerUp({b.m_X + b.m_Width, b.m_Y + b.m_Height / 2});
    Check(slider.GetValues().m_Lower == slider.GetValues().m_Upper,
          "Captured drag remains ordered outside the active thumb");
    slider.SetValues({50, 50});
    ui.Prepare(800, 600);
    const auto mid = Center(slider);
    ui.PointerDown(mid);
    ui.PointerMove({mid.m_X + 50, mid.m_Y});
    ui.PointerUp({mid.m_X + 50, mid.m_Y});
    Check(slider.GetValues().m_Lower == 50 && slider.GetValues().m_Upper > 50,
          "A fresh gesture separates coincident handles in its movement direction");
    const auto root = ui.Semantics().GetId(slider);
    const auto child = ui.Semantics().GetChildAt(root, 1);
    Check(child && ui.Semantics().Perform(child, {SemanticAction::SetNumber, {}, 95}) == SemanticStatus::Succeeded &&
              slider.GetValues().m_Upper == 95,
          "Accessible virtual endpoints use the canonical interval setter");
    Throws([&] { interval.Set({90, 20}); }, "Inverted intervals reject before state publication");
    slider.SetRange(-std::numeric_limits<double>::max(), std::numeric_limits<double>::max());
    slider.SetValues({-1e300, 1e300});
    Check(std::isfinite(slider.GetValues().m_Lower) && std::isfinite(slider.GetValues().m_Upper),
          "Opposite finite numeric extremes remain representable");
}

void Editors(FontFace& font)
{
    UiContext ui(font);
    ThemeController theme(ui);
    auto& date = ui.Root().Add<DateEdit>();
    date.SetDate(CalendarDate{2024, 2, 29});
    date.GetEditor().SetText("2023-02-29");
    Check(!date.Commit() && date.GetDate() == CalendarDate{2024, 2, 29},
          "Invalid complete date drafts retain the last valid date");
    date.GetEditor().SetText("2028-02-29");
    Check(date.Commit() && date.GetDate() == CalendarDate{2028, 2, 29}, "A later valid leap date commits normally");
    date.ShowCalendar();
    ui.Prepare(800, 600);
    auto* popup = ui.GetPopup();
    Check(popup != nullptr, "Date calendar uses ordinary popup ownership");
    static_cast<Popup*>(popup)->Close();
    auto& time = ui.Root().Add<TimeEdit>();
    time.SetTime(ClockTime{23, 59, 59, 999});
    ui.SetFocus(&time.GetEditor());
    ui.KeyEvent(Key::Up, true);
    Check(time.GetTime() == ClockTime{23, 59, 59, 999}, "Time keyboard stepping clamps at the day boundary");
    auto& pair = ui.Root().Add<DateTimeEdit>();
    pair.SetValue(DateTimeValue{{2024, 1, 2}, {3, 4, 5, 0}});
    pair.GetDateEditor().GetEditor().SetText("2025-02-30");
    pair.GetTimeEditor().GetEditor().SetText("12:00");
    Check(!pair.Commit() && pair.GetValue()->m_Date == CalendarDate{2024, 1, 2} &&
              pair.GetTimeEditor().GetTime() == ClockTime{3, 4, 5, 0},
          "Datetime commit validates both drafts before changing either component");
    auto& calendar = ui.Root().Add<Calendar>();
    calendar.SetDate({2024, 2, 28});
    ui.SetFocus(&calendar);
    ui.KeyEvent(Key::Right, true);
    Check(calendar.GetDate() == CalendarDate{2024, 2, 29}, "Calendar arrow navigation includes leap days");
    ui.KeyEvent(Key::Right, true);
    Check(calendar.GetDate() == CalendarDate{2024, 3, 1}, "Calendar keyboard navigation crosses month boundaries");
    calendar.SetDate({1, 1, 1});
    ui.KeyEvent(Key::Left, true);
    Check(calendar.GetDate() == CalendarDate{1, 1, 1}, "Calendar cannot navigate outside civil year bounds");
    auto& editor = ui.Root().Add<FontEditor>();
    editor.RefreshFamilies();
    auto requested = editor.GetFont();
    requested.m_Size = 24;
    requested.m_Weight = 600;
    requested.m_Style = "italic";
    editor.SetFont(requested);
    Check(editor.GetFont() == requested, "Font editor retains complete requested typography");
    auto invalid = requested;
    invalid.m_Size = std::numeric_limits<float>::infinity();
    Throws([&] { editor.SetFont(invalid); }, "Font values reject nonfinite sizes before updating preview");
    auto& picker = ui.Root().Add<FontPicker>();
    picker.SetFont(requested);
    picker.ShowPicker();
    static_cast<Popup*>(ui.GetPopup())->Close({PopupCloseReason::Rejected, 0});
    Check(picker.GetFont() == requested, "Font popup cancellation preserves the applied selection");
    auto& label = ui.Root().Add<SelectableLabel>("Engineering Δ displacement");
    label.SelectAll();
    Check(label.IsReadOnly() && label.GetSelectedText() == label.GetText(),
          "Selectable label reuses grapheme-aware read-only selection");
    auto& link = ui.Root().Add<LinkLabel>("Documentation", "https://example.invalid/docs");
    std::string activated;
    link.Activated().Connect([&](std::string target) { activated = std::move(target); });
    ui.SetFocus(&link);
    ui.KeyEvent(Key::Enter, true);
    Check(activated == link.GetTarget(), "Link activation routes an owned target to application policy");
}
} // namespace

int main()
{
    try
    {
        Temporal();
        FontFace font(ESTUDIOUI_FONT_PATH);
        Choices(font);
        Range(font);
        Editors(font);
        std::cout << "Common controls: " << s_Checks << " checks passed\n";
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
