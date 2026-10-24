#include <EStudioUI/TemporalValue.h>
#include <chrono>
#include <cstdio>
#include <stdexcept>

namespace EStudioUI
{
namespace
{
int Digits(std::string_view text)
{
    int value = 0;
    for (const auto c : text)
    {
        if (c < '0' || c > '9')
        {
            return -1;
        }
        value = value * 10 + c - '0';
    }
    return value;
}
} // namespace

bool CalendarDate::IsValid() const
{
    return m_Year >= 1 && m_Year <= 9999 &&
           std::chrono::year_month_day{std::chrono::year{m_Year}, std::chrono::month{m_Month}, std::chrono::day{m_Day}}
               .ok();
}

std::string CalendarDate::ToString() const
{
    if (!IsValid())
    {
        throw std::invalid_argument("Invalid Gregorian date.");
    }
    char text[64];
    std::snprintf(text, sizeof(text), "%04d-%02u-%02u", m_Year, m_Month, m_Day);
    return text;
}

std::optional<CalendarDate> CalendarDate::Parse(std::string_view text)
{
    if (text.size() != 10 || text[4] != '-' || text[7] != '-')
    {
        return {};
    }
    const auto year = Digits(text.substr(0, 4)), month = Digits(text.substr(5, 2)), day = Digits(text.substr(8, 2));
    if (year < 0 || month < 0 || day < 0)
    {
        return {};
    }
    CalendarDate value{year, static_cast<unsigned>(month), static_cast<unsigned>(day)};
    return value.IsValid() ? std::optional{value} : std::nullopt;
}

bool ClockTime::IsValid() const
{
    return m_Hour < 24 && m_Minute < 60 && m_Second < 60 && m_Millisecond < 1000;
}

std::string ClockTime::ToString() const
{
    if (!IsValid())
    {
        throw std::invalid_argument("Invalid local clock time.");
    }
    char text[64];
    if (m_Millisecond)
    {
        std::snprintf(text, sizeof(text), "%02u:%02u:%02u.%03u", m_Hour, m_Minute, m_Second, m_Millisecond);
    }
    else
    {
        std::snprintf(text, sizeof(text), "%02u:%02u:%02u", m_Hour, m_Minute, m_Second);
    }
    return text;
}

std::optional<ClockTime> ClockTime::Parse(std::string_view text)
{
    if ((text.size() != 5 && text.size() != 8 && text.size() != 12) || text[2] != ':' ||
        (text.size() > 5 && text[5] != ':') || (text.size() > 8 && text[8] != '.'))
    {
        return {};
    }
    const auto hour = Digits(text.substr(0, 2)), minute = Digits(text.substr(3, 2)),
               second = text.size() > 5 ? Digits(text.substr(6, 2)) : 0,
               ms = text.size() > 8 ? Digits(text.substr(9, 3)) : 0;
    if (hour < 0 || minute < 0 || second < 0 || ms < 0)
    {
        return {};
    }
    ClockTime value{static_cast<unsigned>(hour), static_cast<unsigned>(minute), static_cast<unsigned>(second),
                    static_cast<unsigned>(ms)};
    return value.IsValid() ? std::optional{value} : std::nullopt;
}

bool DateTimeValue::IsValid() const
{
    return m_Date.IsValid() && m_Time.IsValid();
}

std::string DateTimeValue::ToString() const
{
    return m_Date.ToString() + "T" + m_Time.ToString();
}

std::optional<DateTimeValue> DateTimeValue::Parse(std::string_view text)
{
    if (text.size() < 16 || text[10] != 'T')
    {
        return {};
    }
    const auto date = CalendarDate::Parse(text.substr(0, 10));
    const auto time = ClockTime::Parse(text.substr(11));
    return date && time ? std::optional<DateTimeValue>{{*date, *time}} : std::nullopt;
}
} // namespace EStudioUI
