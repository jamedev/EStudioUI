#pragma once
#include <compare>
#include <optional>
#include <string>
#include <string_view>

namespace EStudioUI
{
//! A Gregorian civil date without timezone conversion.
struct CalendarDate
{
    //! Civil year from one through 9999.
    int m_Year = 2000;
    //! Month from one through twelve.
    unsigned m_Month = 1;
    //! Day in the configured month.
    unsigned m_Day = 1;
    //! Tests Gregorian validity and supported bounds.
    bool IsValid() const;
    //! Formats YYYY-MM-DD, rejecting invalid values.
    std::string ToString() const;
    //! Parses exact ISO civil dates without locale dependence.
    static std::optional<CalendarDate> Parse(std::string_view text);
    //! Compares civil date coordinates.
    auto operator<=>(const CalendarDate&) const = default;
};

//! A local clock value, explicitly independent of timezone or daylight-saving rules.
struct ClockTime
{
    //! Hour from zero through 23.
    unsigned m_Hour = 0;
    //! Minute from zero through 59.
    unsigned m_Minute = 0;
    //! Second from zero through 59; leap seconds are outside this value type.
    unsigned m_Second = 0;
    //! Millisecond from zero through 999.
    unsigned m_Millisecond = 0;
    //! Tests all field limits.
    bool IsValid() const;
    //! Formats HH:MM:SS, adding .fff when milliseconds are nonzero.
    std::string ToString() const;
    //! Parses HH:MM, HH:MM:SS or HH:MM:SS.fff.
    static std::optional<ClockTime> Parse(std::string_view text);
    //! Compares clock coordinates.
    auto operator<=>(const ClockTime&) const = default;
};

//! An explicit civil date and local clock pair.
struct DateTimeValue
{
    //! Gregorian date component.
    CalendarDate m_Date;
    //! Local clock component.
    ClockTime m_Time;
    //! Tests both component values.
    bool IsValid() const;
    //! Formats the components separated by T.
    std::string ToString() const;
    //! Parses one ISO local datetime without an offset suffix.
    static std::optional<DateTimeValue> Parse(std::string_view text);
    //! Compares both components.
    auto operator<=>(const DateTimeValue&) const = default;
};
} // namespace EStudioUI
