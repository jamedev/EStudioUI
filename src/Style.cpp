#include <EStudioUI/Style.h>
#include <EStudioUI/StyleProperty.h>

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cmath>
#include <fstream>
#include <functional>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace EStudioUI
{
namespace
{
std::string Trim(std::string_view value)
{
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos)
        return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    return std::string(value.substr(first, last - first + 1));
}

std::string Lower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

std::vector<std::string> Words(std::string_view text)
{
    std::vector<std::string> result;
    std::string current;
    int parentheses = 0;
    for (char c : text)
    {
        if (c == '(')
            ++parentheses;
        if (c == ')')
            --parentheses;
        if (std::isspace(static_cast<unsigned char>(c)) && parentheses == 0)
        {
            if (!current.empty())
            {
                result.push_back(std::move(current));
                current.clear();
            }
        }
        else
            current += c;
    }
    if (!current.empty())
        result.push_back(std::move(current));
    return result;
}

float Number(std::string value, bool allowNegative = false)
{
    value = Trim(value);
    if (value.ends_with("px"))
        value.resize(value.size() - 2);
    float result = 0;
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() || !std::isfinite(result) ||
        (!allowNegative && result < 0) || std::abs(result) > 1000000)
        throw std::runtime_error("Invalid numeric value: " + value);
    return result;
}

Length Dimension(const std::string& value)
{
    if (value == "auto" || value == "content")
        return {};
    if (value.ends_with("fr"))
    {
        const auto weight = Number(value.substr(0, value.size() - 2));
        if (weight <= 0)
            throw std::runtime_error("Fractional sizes require a positive weight.");
        return {Length::Unit::Fraction, weight};
    }
    if (value.ends_with('%'))
        return {Length::Unit::Percent, Number(value.substr(0, value.size() - 1))};
    return {Length::Unit::Pixels, Number(value)};
}

Color ParseColor(std::string value)
{
    value = Lower(Trim(value));
    static const std::unordered_map<std::string, Color> names{
        {"transparent", {0, 0, 0, 0}},   {"black", {0, 0, 0, 1}},        {"white", {1, 1, 1, 1}},
        {"red", {1, 0, 0, 1}},           {"green", {0, 0.5f, 0, 1}},     {"blue", {0, 0, 1, 1}},
        {"gray", {0.5f, 0.5f, 0.5f, 1}}, {"grey", {0.5f, 0.5f, 0.5f, 1}}};
    if (const auto found = names.find(value); found != names.end())
        return found->second;

    if (value.starts_with('#'))
    {
        auto hex = value.substr(1);
        if (hex.size() == 3 || hex.size() == 4)
        {
            std::string expanded;
            for (char c : hex)
            {
                expanded += c;
                expanded += c;
            }
            hex = std::move(expanded);
        }
        if (hex.size() != 6 && hex.size() != 8)
            throw std::runtime_error("Invalid hexadecimal color: " + value);
        unsigned components[4]{0, 0, 0, 255};
        for (std::size_t i = 0; i < hex.size() / 2; ++i)
        {
            const auto parsed = std::from_chars(hex.data() + i * 2, hex.data() + i * 2 + 2, components[i], 16);
            if (parsed.ec != std::errc{} || parsed.ptr != hex.data() + i * 2 + 2)
                throw std::runtime_error("Invalid hexadecimal color: " + value);
        }
        return {static_cast<float>(components[0]) / 255.0f, static_cast<float>(components[1]) / 255.0f,
                static_cast<float>(components[2]) / 255.0f, static_cast<float>(components[3]) / 255.0f};
    }
    if (value.starts_with("rgb(") || value.starts_with("rgba("))
    {
        if (!value.ends_with(')'))
            throw std::runtime_error("Unterminated color: " + value);
        const bool alpha = value.starts_with("rgba(");
        const auto first = value.find('(');
        auto inner = value.substr(first + 1, value.size() - first - 2);
        std::replace(inner.begin(), inner.end(), ',', ' ');
        const auto parts = Words(inner);
        if (parts.size() != (alpha ? 4u : 3u))
            throw std::runtime_error("Invalid rgb/rgba color: " + value);
        float channels[4]{0, 0, 0, 1};
        for (std::size_t i = 0; i < parts.size(); ++i)
        {
            const bool percent = parts[i].ends_with('%');
            const auto number = percent ? parts[i].substr(0, parts[i].size() - 1) : parts[i];
            const float divisor = percent ? 100.0f : (i < 3 ? 255.0f : 1.0f);
            channels[i] = std::clamp(Number(number) / divisor, 0.0f, 1.0f);
        }
        return {channels[0], channels[1], channels[2], channels[3]};
    }
    throw std::runtime_error("Unsupported color: " + value);
}

Insets ParseInsets(const std::string& value)
{
    const auto words = Words(value);
    if (words.empty() || words.size() > 4)
        throw std::runtime_error("Expected one to four edge distances.");
    float values[4]{};
    for (std::size_t i = 0; i < words.size(); ++i)
        values[i] = Number(words[i]);
    if (words.size() == 1)
        return {values[0], values[0], values[0], values[0]};
    if (words.size() == 2)
        return {values[0], values[1], values[0], values[1]};
    if (words.size() == 3)
        return {values[0], values[1], values[2], values[1]};
    return {values[0], values[1], values[2], values[3]};
}

bool IdentifierCharacter(char c)
{
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-';
}

std::vector<std::string> Split(std::string_view text, char delimiter)
{
    std::vector<std::string> result;
    int depth = 0;
    char quote = 0;
    std::size_t start = 0;
    for (std::size_t i = 0; i < text.size(); ++i)
    {
        const char c = text[i];
        if (quote)
        {
            if (c == quote && (i == 0 || text[i - 1] != '\\'))
                quote = 0;
            continue;
        }
        if (c == '\'' || c == '"')
            quote = c;
        else if (c == '(')
            ++depth;
        else if (c == ')')
            --depth;
        else if (c == delimiter && depth == 0)
        {
            result.push_back(Trim(text.substr(start, i - start)));
            start = i + 1;
        }
        if (depth < 0)
            throw std::runtime_error("Unbalanced CSS function.");
    }
    if (quote || depth)
        throw std::runtime_error("Unterminated CSS function or string.");
    result.push_back(Trim(text.substr(start)));
    return result;
}

float Angle(std::string value)
{
    if (value.ends_with("deg"))
        return Number(value.substr(0, value.size() - 3), true);
    if (value.ends_with("turn"))
        return Number(value.substr(0, value.size() - 4), true) * 360;
    if (value.ends_with("rad"))
        return Number(value.substr(0, value.size() - 3), true) * 180 / 3.14159265358979323846f;
    if (value == "0")
        return 0;
    throw std::runtime_error("Angles require deg, rad, or turn.");
}

float Percent(const std::string& text)
{
    if (!text.ends_with('%'))
        throw std::runtime_error("Expected a percentage.");
    return Number(text.substr(0, text.size() - 1), true) / 100;
}

Gradient ParseGradient(const std::string& value)
{
    if (value == "none")
        return {};
    Gradient result;
    const bool linear = value.starts_with("linear-gradient(");
    if (!linear && !value.starts_with("radial-gradient("))
        throw std::runtime_error("Supported background images: linear-gradient and radial-gradient.");
    if (!value.ends_with(')'))
        throw std::runtime_error("Unterminated gradient.");
    const auto open = value.find('(');
    const auto parts = Split(std::string_view(value).substr(open + 1, value.size() - open - 2), ',');
    result.m_Kind = linear ? Gradient::Kind::Linear : Gradient::Kind::Radial;
    std::size_t first = 0;
    if (!parts.empty() && linear &&
        (parts[0].starts_with("to ") || parts[0].ends_with("deg") || parts[0].ends_with("turn") ||
         parts[0].ends_with("rad")))
    {
        const auto& direction = parts[0];
        if (direction == "to right")
            result.m_Angle = 90;
        else if (direction == "to left")
            result.m_Angle = 270;
        else if (direction == "to top")
            result.m_Angle = 0;
        else if (direction == "to bottom")
            result.m_Angle = 180;
        else if (direction == "to top right" || direction == "to right top")
            result.m_Angle = 45;
        else if (direction == "to bottom right" || direction == "to right bottom")
            result.m_Angle = 135;
        else if (direction == "to bottom left" || direction == "to left bottom")
            result.m_Angle = 225;
        else if (direction == "to top left" || direction == "to left top")
            result.m_Angle = 315;
        else
            result.m_Angle = Angle(direction);
        first = 1;
    }
    if (!parts.empty() && !linear &&
        (parts[0].starts_with("ellipse") || parts[0].starts_with("circle") || parts[0].starts_with("at ")))
    {
        auto words = Words(parts[0]);
        std::size_t index = 0;
        if (words[0] == "ellipse" || words[0] == "circle")
            ++index;
        if (index < words.size() && words[index] != "at")
        {
            result.m_Radius.m_X = Percent(words[index++]);
            result.m_Radius.m_Y =
                index < words.size() && words[index] != "at" ? Percent(words[index++]) : result.m_Radius.m_X;
        }
        if (index < words.size())
        {
            if (words[index++] != "at" || words.size() - index != 2)
                throw std::runtime_error("Radial gradients use: ellipse [rx% ry%] at x% y%.");
            result.m_Center = {Percent(words[index]), Percent(words[index + 1])};
        }
        first = 1;
    }
    if (parts.size() - first < 2 || parts.size() - first > 8)
        throw std::runtime_error("Gradients require two to eight stops.");
    std::vector<bool> specified;
    for (std::size_t i = first; i < parts.size(); ++i)
    {
        const auto words = Words(parts[i]);
        if (words.empty() || words.size() > 2)
            throw std::runtime_error("Gradient stops use color [position%].");
        result.m_Stops.push_back({words.size() == 2 ? Percent(words[1]) : 0, ParseColor(words[0])});
        specified.push_back(words.size() == 2);
    }
    if (!specified.front())
    {
        specified.front() = true;
        result.m_Stops.front().m_Position = 0;
    }
    if (!specified.back())
    {
        specified.back() = true;
        result.m_Stops.back().m_Position = 1;
    }
    std::size_t anchor = 0;
    for (std::size_t i = 1; i < result.m_Stops.size(); ++i)
    {
        if (!specified[i])
            continue;
        auto& stop = result.m_Stops[i];
        stop.m_Position = std::max(stop.m_Position, result.m_Stops[anchor].m_Position);
        for (std::size_t j = anchor + 1; j < i; ++j)
            result.m_Stops[j].m_Position = std::lerp(result.m_Stops[anchor].m_Position, stop.m_Position,
                                                     static_cast<float>(j - anchor) / static_cast<float>(i - anchor));
        anchor = i;
    }
    return result;
}

Transform ParseTransform(const std::string& value)
{
    Transform result;
    if (value == "none")
        return result;
    std::size_t start = 0;
    while (start < value.size())
    {
        start = value.find_first_not_of(" \t\r\n", start);
        if (start == std::string::npos)
            break;
        const auto open = value.find('(', start);
        const auto close = value.find(')', open);
        if (open == std::string::npos || close == std::string::npos)
            throw std::runtime_error("Unterminated transform.");
        const auto name = Lower(Trim(std::string_view(value).substr(start, open - start)));
        auto body = value.substr(open + 1, close - open - 1);
        std::replace(body.begin(), body.end(), ',', ' ');
        const auto parts = Words(body);
        TransformOperation operation;
        const auto count = parts.size();
        if (count == 0)
            throw std::runtime_error("Transform arguments are required.");
        if (name == "translate" || name == "translatex" || name == "translatey")
        {
            if (count > (name == "translate" ? 2u : 1u))
                throw std::runtime_error("Invalid translation arguments.");
            operation.m_Kind = TransformOperation::Kind::Translate;
            const auto length = [](const std::string& word)
            { return word.ends_with('%') ? Percent(word) * 100 : Number(word, true); };
            if (name == "translatey")
            {
                operation.m_Values[1] = length(parts[0]);
                operation.m_PercentY = parts[0].ends_with('%');
            }
            else
            {
                operation.m_Values[0] = length(parts[0]);
                operation.m_PercentX = parts[0].ends_with('%');
                if (count == 2)
                {
                    operation.m_Values[1] = length(parts[1]);
                    operation.m_PercentY = parts[1].ends_with('%');
                }
            }
        }
        else if (name == "scale" || name == "scalex" || name == "scaley")
        {
            if (count > (name == "scale" ? 2u : 1u))
                throw std::runtime_error("Invalid scale arguments.");
            operation.m_Kind = TransformOperation::Kind::Scale;
            operation.m_Values[0] = name == "scaley" ? 1 : Number(parts[0], true);
            operation.m_Values[1] = name == "scalex" ? 1 : Number(parts[count == 2 ? 1 : 0], true);
        }
        else if (name == "rotate" || name == "skewx" || name == "skewy")
        {
            if (count != 1)
                throw std::runtime_error("Angles require one argument.");
            operation.m_Kind = name == "rotate"  ? TransformOperation::Kind::Rotate
                               : name == "skewx" ? TransformOperation::Kind::SkewX
                                                 : TransformOperation::Kind::SkewY;
            operation.m_Values[0] = Angle(parts[0]);
        }
        else if (name == "matrix")
        {
            if (count != 6)
                throw std::runtime_error("matrix requires six numbers.");
            operation.m_Kind = TransformOperation::Kind::Matrix;
            for (std::size_t i = 0; i < 6; ++i)
                operation.m_Values[i] = Number(parts[i], true);
        }
        else
            throw std::runtime_error("Unsupported 2D transform: " + name);
        result.m_Operations.push_back(operation);
        if (result.m_Operations.size() > 8)
            throw std::runtime_error("Transforms support at most eight operations.");
        start = close + 1;
    }
    if (result.m_Operations.empty())
        throw std::runtime_error("Expected a transform list.");
    return result;
}

std::vector<Shadow> ParseShadows(const std::string& value)
{
    if (value == "none")
        return {};
    std::vector<Shadow> result;
    for (const auto& part : Split(value, ','))
    {
        auto words = Words(part);
        Shadow shadow;
        if (!words.empty() && words.front() == "inset")
        {
            shadow.m_Inset = true;
            words.erase(words.begin());
        }
        if (!words.empty() && words.back() == "inset")
        {
            shadow.m_Inset = true;
            words.pop_back();
        }
        if (words.size() < 3 || words.size() > 5)
            throw std::runtime_error("Shadows use [inset] x y [blur [spread]] color.");
        shadow.m_Color = ParseColor(words.back());
        words.pop_back();
        shadow.m_X = Number(words[0], true);
        shadow.m_Y = Number(words[1], true);
        if (words.size() > 2)
            shadow.m_Blur = Number(words[2]);
        if (words.size() > 3)
            shadow.m_Spread = Number(words[3], true);
        result.push_back(shadow);
        if (result.size() > 8)
            throw std::runtime_error("Box shadows support at most eight layers.");
    }
    return result;
}

EasingCurve ParseEasing(const std::string& value)
{
    using Kind = EasingCurve::Kind;
    if (value == "step-start")
        return EasingCurve::Steps(1, true);
    if (value == "step-end")
        return EasingCurve::Steps(1);
    if (value.starts_with("steps(") && value.ends_with(')'))
    {
        const auto args = Split(std::string_view(value).substr(6, value.size() - 7), ',');
        if (args.empty() || args.size() > 2)
            throw std::runtime_error("steps requires a count and optional start/end.");
        const float count = Number(args[0]);
        if (count < 1 || count > 1000000 || std::floor(count) != count)
            throw std::runtime_error("Step counts require a positive integer.");
        if (args.size() == 2 && args[1] != "start" && args[1] != "end" && args[1] != "jump-start" &&
            args[1] != "jump-end")
            throw std::runtime_error("steps supports start and end jumps.");
        return EasingCurve::Steps(static_cast<unsigned>(count),
                                  args.size() == 2 && (args[1] == "start" || args[1] == "jump-start"));
    }
    if (value == "linear")
        return EasingCurve(Kind::Linear);
    if (value == "ease")
        return EasingCurve(Kind::Ease);
    if (value == "ease-in")
        return EasingCurve(Kind::EaseIn);
    if (value == "ease-out")
        return EasingCurve(Kind::EaseOut);
    if (value == "ease-in-out")
        return EasingCurve(Kind::EaseInOut);
    if (value.starts_with("cubic-bezier(") && value.ends_with(')'))
    {
        const auto args = Split(std::string_view(value).substr(13, value.size() - 14), ',');
        if (args.size() == 4)
            return EasingCurve::CubicBezier(Number(args[0], true), Number(args[1], true), Number(args[2], true),
                                            Number(args[3], true));
    }
    throw std::runtime_error("Unsupported easing curve: " + value);
}

bool IsEasing(const std::string& value)
{
    return value == "linear" || value == "ease" || value == "ease-in" || value == "ease-out" ||
           value == "ease-in-out" || value.starts_with("cubic-bezier(") || value.starts_with("steps(") ||
           value == "step-start" || value == "step-end";
}

bool IsTime(const std::string& value)
{
    return !value.empty() &&
           (std::isdigit(static_cast<unsigned char>(value[0])) || value[0] == '.' || value[0] == '-' ||
            value[0] == '+') &&
           value.ends_with('s');
}

double Time(const std::string& value)
{
    if (!IsTime(value))
        throw std::runtime_error("Durations require s or ms.");
    return value.ends_with("ms") ? Number(value.substr(0, value.size() - 2)) / 1000.0
                                 : Number(value.substr(0, value.size() - 1));
}

std::vector<CssTransition> ParseTransitions(const std::string& value)
{
    std::vector<CssTransition> result;
    if (value == "none")
        return result;
    for (const auto& part : Split(value, ','))
    {
        CssTransition transition;
        int times = 0;
        bool property = false, easing = false;
        for (const auto& word : Words(part))
        {
            if (IsTime(word))
            {
                if (times == 0)
                    transition.m_Duration = Time(word);
                else if (times == 1)
                    transition.m_Delay = Time(word);
                else
                    throw std::runtime_error("Too many transition times.");
                ++times;
            }
            else if (IsEasing(word))
            {
                if (easing)
                    throw std::runtime_error("Duplicate transition easing.");
                transition.m_Easing = ParseEasing(word);
                easing = true;
            }
            else
            {
                if (property || FindStyleProperties(word).empty())
                    throw std::runtime_error("Unknown transition property: " + word);
                transition.m_Property = word;
                property = true;
            }
        }
        if (Words(part).empty())
            throw std::runtime_error("Empty transition.");
        result.push_back(transition);
        if (result.size() > 32)
            throw std::runtime_error("At most 32 transition definitions are supported.");
    }
    return result;
}

std::vector<CssAnimation> ParseAnimations(const std::string& value)
{
    std::vector<CssAnimation> result;
    if (value == "none")
        return result;
    for (const auto& part : Split(value, ','))
    {
        CssAnimation animation;
        int times = 0;
        for (const auto& word : Words(part))
        {
            if (IsTime(word))
            {
                if (times == 0)
                    animation.m_Duration = Time(word);
                else if (times == 1)
                    animation.m_Delay = Time(word);
                else
                    throw std::runtime_error("Too many animation times.");
                ++times;
            }
            else if (IsEasing(word))
                animation.m_Easing = ParseEasing(word);
            else if (word == "infinite")
                animation.m_Iterations = 0;
            else if (word == "normal")
                animation.m_Reverse = animation.m_Alternate = false;
            else if (word == "reverse")
                animation.m_Reverse = true;
            else if (word == "alternate")
                animation.m_Alternate = true;
            else if (word == "alternate-reverse")
                animation.m_Alternate = animation.m_Reverse = true;
            else if (word == "none")
                animation.m_FillBefore = animation.m_FillAfter = false;
            else if (word == "forwards")
                animation.m_FillAfter = true;
            else if (word == "backwards")
                animation.m_FillBefore = true;
            else if (word == "both")
                animation.m_FillBefore = animation.m_FillAfter = true;
            else if (word == "paused" || word == "running")
                animation.m_Paused = word == "paused";
            else if (!word.empty() && std::isdigit(static_cast<unsigned char>(word[0])))
            {
                const float count = Number(word);
                if (count < 1 || std::floor(count) != count)
                    throw std::runtime_error("Iteration counts require positive integers or infinite.");
                animation.m_Iterations = static_cast<unsigned>(count);
            }
            else
            {
                if (!animation.m_Name.empty() || !std::all_of(word.begin(), word.end(), IdentifierCharacter))
                    throw std::runtime_error("Invalid animation name: " + word);
                animation.m_Name = word;
            }
        }
        if (animation.m_Name.empty() || (!animation.m_Iterations && animation.m_Duration == 0))
            throw std::runtime_error("Named animations require a name and infinite loops require a positive duration.");
        result.push_back(animation);
        if (result.size() > 8)
            throw std::runtime_error("At most eight named animations are supported.");
    }
    return result;
}

std::function<void(Style&)> CompileMotionLonghand(const std::string& name, const std::string& value)
{
    const auto values = Split(value, ',');
    if (values.size() > 32)
        throw std::runtime_error("Too many timing definitions.");
    if (name.starts_with("transition-"))
    {
        if (name != "transition-property" && name != "transition-duration" && name != "transition-delay" &&
            name != "transition-timing-function")
            throw std::runtime_error("Unsupported transition longhand: " + name);
        std::vector<CssTransition> items;
        for (const auto& text : values)
        {
            CssTransition item;
            if (name == "transition-property")
            {
                if (text != "none" && FindStyleProperties(text).empty())
                    throw std::runtime_error("Unknown transition property: " + text);
                item.m_Property = text;
            }
            else if (name == "transition-duration")
                item.m_Duration = Time(text);
            else if (name == "transition-delay")
                item.m_Delay = Time(text);
            else
                item.m_Easing = ParseEasing(text);
            items.push_back(item);
        }
        return [name, items](Style& style)
        {
            auto& targets = style.m_Transitions;
            if (name == "transition-property")
            {
                if (items.size() == 1 && items[0].m_Property == "none")
                {
                    targets.clear();
                    return;
                }
                const auto seed = targets.empty() ? CssTransition{} : targets.front();
                targets.resize(items.size(), seed);
            }
            else if (targets.empty())
                targets.resize(items.size());
            for (std::size_t i = 0; i < targets.size(); ++i)
            {
                const auto& item = items[i % items.size()];
                if (name == "transition-property")
                    targets[i].m_Property = item.m_Property;
                else if (name == "transition-duration")
                    targets[i].m_Duration = item.m_Duration;
                else if (name == "transition-delay")
                    targets[i].m_Delay = item.m_Delay;
                else
                    targets[i].m_Easing = item.m_Easing;
            }
        };
    }
    if (values.size() > 8)
        throw std::runtime_error("At most eight named animations are supported.");
    std::vector<CssAnimation> items;
    for (const auto& text : values)
    {
        CssAnimation item;
        if (name == "animation-name")
        {
            if (text.empty() || !std::all_of(text.begin(), text.end(), IdentifierCharacter))
                throw std::runtime_error("Invalid animation name.");
            item.m_Name = text == "none" ? "" : text;
        }
        else if (name == "animation-duration")
            item.m_Duration = Time(text);
        else if (name == "animation-delay")
            item.m_Delay = Time(text);
        else if (name == "animation-timing-function")
            item.m_Easing = ParseEasing(text);
        else if (name == "animation-play-state")
        {
            if (text != "paused" && text != "running")
                throw std::runtime_error("Invalid animation-play-state.");
            item.m_Paused = text == "paused";
        }
        else if (name == "animation-iteration-count")
        {
            if (text == "infinite")
                item.m_Iterations = 0;
            else
            {
                const float count = Number(text);
                if (count < 1 || std::floor(count) != count)
                    throw std::runtime_error("Iteration counts require positive integers or infinite.");
                item.m_Iterations = static_cast<unsigned>(count);
            }
        }
        else if (name == "animation-direction")
        {
            if (text != "normal" && text != "reverse" && text != "alternate" && text != "alternate-reverse")
                throw std::runtime_error("Invalid animation-direction.");
            item.m_Reverse = text == "reverse" || text == "alternate-reverse";
            item.m_Alternate = text == "alternate" || text == "alternate-reverse";
        }
        else if (name == "animation-fill-mode")
        {
            if (text != "none" && text != "forwards" && text != "backwards" && text != "both")
                throw std::runtime_error("Invalid animation-fill-mode.");
            item.m_FillBefore = text == "backwards" || text == "both";
            item.m_FillAfter = text == "forwards" || text == "both";
        }
        else
            throw std::runtime_error("Unsupported animation longhand: " + name);
        items.push_back(item);
    }
    return [name, items](Style& style)
    {
        auto& targets = style.m_Animations;
        if (name == "animation-name")
        {
            const auto seed = targets.empty() ? CssAnimation{} : targets.front();
            targets.resize(items.size(), seed);
        }
        else if (targets.empty())
            targets.resize(items.size());
        for (std::size_t i = 0; i < targets.size(); ++i)
        {
            const auto& item = items[i % items.size()];
            if (name == "animation-name")
                targets[i].m_Name = item.m_Name;
            else if (name == "animation-duration")
                targets[i].m_Duration = item.m_Duration;
            else if (name == "animation-delay")
                targets[i].m_Delay = item.m_Delay;
            else if (name == "animation-timing-function")
                targets[i].m_Easing = item.m_Easing;
            else if (name == "animation-play-state")
                targets[i].m_Paused = item.m_Paused;
            else if (name == "animation-iteration-count")
                targets[i].m_Iterations = item.m_Iterations;
            else if (name == "animation-direction")
            {
                targets[i].m_Reverse = item.m_Reverse;
                targets[i].m_Alternate = item.m_Alternate;
            }
            else if (name == "animation-fill-mode")
            {
                targets[i].m_FillBefore = item.m_FillBefore;
                targets[i].m_FillAfter = item.m_FillAfter;
            }
        }
    };
}

std::size_t CloseBlock(const std::string& source, std::size_t open)
{
    int depth = 1;
    char quote = 0;
    for (std::size_t i = open + 1; i < source.size(); ++i)
    {
        const char c = source[i];
        if (quote)
        {
            if (c == quote && source[i - 1] != '\\')
                quote = 0;
        }
        else if (c == '"' || c == '\'')
            quote = c;
        else if (c == '{')
            ++depth;
        else if (c == '}' && --depth == 0)
            return i;
    }
    throw std::runtime_error("Unterminated CSS block.");
}
} // namespace

float Length::Resolve(float parent, float fallback) const
{
    if (m_Unit == Unit::Pixels)
        return m_Value;
    if (m_Unit == Unit::Percent)
        return parent * m_Value / 100.0f;
    return fallback;
}

void StyleSheet::Apply(Style& style, const Declaration& declaration)
{
    const auto& name = declaration.m_Name;
    const auto& value = declaration.m_Value;
    if (name == "background" || name == "background-color")
    {
        if (name == "background" && value.find("gradient(") != std::string::npos)
        {
            style.m_BackgroundImage = ParseGradient(value);
            style.m_Background = {0, 0, 0, 0};
        }
        else
        {
            style.m_Background = ParseColor(value);
            if (name == "background")
                style.m_BackgroundImage = {};
        }
    }
    else if (name == "background-image")
        style.m_BackgroundImage = ParseGradient(value);
    else if (name == "transform")
        style.m_Transform = ParseTransform(value);
    else if (name == "transform-origin")
    {
        auto words = Words(value);
        if (words.empty() || words.size() > 2)
            throw std::runtime_error("transform-origin requires one or two percentages or edge keywords.");
        const auto position = [](const std::string& word)
        {
            if (word == "left" || word == "top")
                return 0.0f;
            if (word == "center")
                return 0.5f;
            if (word == "right" || word == "bottom")
                return 1.0f;
            return Percent(word);
        };
        style.m_Transform.m_Origin = {position(words[0]), words.size() == 2 ? position(words[1]) : 0.5f};
    }
    else if (name == "overflow")
    {
        if (value != "hidden" && value != "visible")
            throw std::runtime_error("overflow supports hidden and visible.");
        style.m_ClipContent = value == "hidden";
    }
    else if (name == "content")
    {
        if (value != "\"\"" && value != "''" && value != "none")
            throw std::runtime_error("Decorative content supports an empty string or none.");
        style.m_Decoration = value != "none";
    }
    else if (name == "position")
    {
        if (value != "absolute")
            throw std::runtime_error("Decorative layers support position: absolute.");
    }
    else if (name == "inset" || name == "top" || name == "right" || name == "bottom" || name == "left")
    {
        const auto length = [](const std::string& word)
        {
            if (word == "auto")
                return Length{};
            return word.ends_with('%') ? Length{Length::Unit::Percent, Percent(word) * 100}
                                       : Length{Length::Unit::Pixels, Number(word, true)};
        };
        if (name == "inset")
        {
            const auto words = Words(value);
            if (words.empty() || words.size() > 4)
                throw std::runtime_error("inset requires one to four offsets.");
            const auto top = length(words[0]);
            const auto right = words.size() > 1 ? length(words[1]) : top;
            const auto bottom = words.size() > 2 ? length(words[2]) : top;
            style.m_Anchors = {top, right, bottom, words.size() > 3 ? length(words[3]) : right};
        }
        else
        {
            auto& edge = name == "top"      ? style.m_Anchors.m_Top
                         : name == "right"  ? style.m_Anchors.m_Right
                         : name == "bottom" ? style.m_Anchors.m_Bottom
                                            : style.m_Anchors.m_Left;
            edge = length(value);
        }
    }
    else if (name == "transition")
        style.m_Transitions = ParseTransitions(value);
    else if (name == "animation")
        style.m_Animations = ParseAnimations(value);
    else if (name == "animation-play-state")
    {
        const auto values = Split(value, ',');
        for (const auto& item : values)
            if (item != "paused" && item != "running")
                throw std::runtime_error("Invalid animation-play-state.");
        for (std::size_t i = 0; i < style.m_Animations.size(); ++i)
            style.m_Animations[i].m_Paused = values[i % values.size()] == "paused";
    }
    else if (name == "animation-timing-function")
    {
        const auto curve = ParseEasing(value);
        for (auto& animation : style.m_Animations)
            animation.m_Easing = curve;
    }
    else if (name == "color")
        style.m_Color = ParseColor(value);
    else if (name == "border-color")
        style.m_BorderColor = ParseColor(value);
    else if (name == "selection-background-color")
        style.m_SelectionBackground = ParseColor(value);
    else if (name == "selection-color")
        style.m_SelectionColor = ParseColor(value);
    else if (name == "placeholder-color")
        style.m_PlaceholderColor = ParseColor(value);
    else if (name == "caret-color")
        style.m_CaretColor = ParseColor(value);
    else if (name == "border-width")
        style.m_BorderWidth = Number(value);
    else if (name == "border-radius")
        style.m_Radius = Number(value);
    else if (name == "font-size")
        style.m_FontSize = std::clamp(Number(value), 0.0f, 512.0f);
    else if (name == "opacity")
        style.m_Opacity = std::clamp(Number(value), 0.0f, 1.0f);
    else if (name == "translate-x")
        style.m_TranslateX = Number(value, true);
    else if (name == "translate-y")
        style.m_TranslateY = Number(value, true);
    else if (name == "padding")
        style.m_Padding = ParseInsets(value);
    else if (name == "margin")
        style.m_Margin = ParseInsets(value);
    else if (name == "padding-top")
        style.m_Padding.m_Top = Number(value);
    else if (name == "padding-right")
        style.m_Padding.m_Right = Number(value);
    else if (name == "padding-bottom")
        style.m_Padding.m_Bottom = Number(value);
    else if (name == "padding-left")
        style.m_Padding.m_Left = Number(value);
    else if (name == "width")
        style.m_Width = Dimension(value);
    else if (name == "height")
        style.m_Height = Dimension(value);
    else if (name == "min-width")
        style.m_MinWidth = Number(value);
    else if (name == "min-height")
        style.m_MinHeight = Number(value);
    else if (name == "max-width")
        style.m_MaxWidth = Number(value);
    else if (name == "max-height")
        style.m_MaxHeight = Number(value);
    else if (name == "gap")
        style.m_Gap = Number(value);
    else if (name == "row-height")
    {
        style.m_RowHeight = Number(value);
        if (style.m_RowHeight < 12)
            throw std::runtime_error("row-height must be at least 12px.");
    }
    else if (name == "indent-size")
        style.m_Indent = Number(value);
    else if (name == "header-background")
        style.m_HeaderBackground = ParseColor(value);
    else if (name == "alternate-background")
        style.m_AlternateBackground = ParseColor(value);
    else if (name == "hover-background")
        style.m_HoverBackground = ParseColor(value);
    else if (name == "grid-color")
        style.m_GridColor = ParseColor(value);
    else if (name == "flex-grow" || name == "flex")
        style.m_FlexGrow = Number(value);
    else if (name == "flex-direction")
    {
        if (value != "row" && value != "column")
            throw std::runtime_error("flex-direction supports row and column.");
        style.m_Row = value == "row";
    }
    else if (name == "align-items")
    {
        if (value != "stretch" && value != "start" && value != "center" && value != "end" && value != "flex-start" &&
            value != "flex-end")
            throw std::runtime_error("Unsupported align-items value.");
        style.m_Align = value.starts_with("flex-") ? value.substr(5) : value;
    }
    else if (name == "justify-content")
    {
        if (value != "start" && value != "center" && value != "end" && value != "space-between" &&
            value != "flex-start" && value != "flex-end")
            throw std::runtime_error("Unsupported justify-content value.");
        style.m_Justify = value.starts_with("flex-") ? value.substr(5) : value;
    }
    else if (name == "text-align")
    {
        if (value != "left" && value != "center" && value != "right")
            throw std::runtime_error("Unsupported text-align value.");
        style.m_TextAlign = value;
    }
    else if (name == "display")
    {
        if (value != "none" && value != "flex" && value != "block")
            throw std::runtime_error("display supports flex, block, and none.");
        style.m_Displayed = value != "none";
    }
    else if (name == "border")
    {
        if (value == "none" || value == "0")
            style.m_BorderWidth = 0;
        else
        {
            const auto parts = Words(value);
            if (parts.size() != 3 || parts[1] != "solid")
                throw std::runtime_error("border requires: width solid color.");
            style.m_BorderWidth = Number(parts[0]);
            style.m_BorderColor = ParseColor(parts[2]);
        }
    }
    else if (name == "box-shadow")
    {
        const auto shadows = ParseShadows(value);
        style.m_Shadow = shadows.empty() ? Shadow{} : shadows.front();
        style.m_ShadowLayers.m_Layers =
            shadows.size() > 1 ? std::vector<Shadow>(shadows.begin() + 1, shadows.end()) : std::vector<Shadow>{};
    }
    else
        throw std::runtime_error("Unsupported CSS property: " + name);
}

void StyleSheet::Compile(Declaration& declaration)
{
    const auto& name = declaration.m_Name;
    const auto& value = declaration.m_Value;
    if (name.starts_with("transition-") || name.starts_with("animation-"))
    {
        declaration.m_Apply = CompileMotionLonghand(name, value);
        return;
    }
    Style parsed;
    Apply(parsed, declaration);
    const auto properties = FindStyleProperties(name);
    for (auto property : properties)
        declaration.m_Properties.emplace_back(GetStylePropertyName(property));
    if (name == "position")
        declaration.m_Apply = [](Style&) {};
    else if (name == "transition")
        declaration.m_Apply = [items = parsed.m_Transitions](Style& style) { style.m_Transitions = items; };
    else if (name == "animation")
        declaration.m_Apply = [items = parsed.m_Animations](Style& style) { style.m_Animations = items; };
    else if (name == "animation-play-state")
    {
        const auto values = Split(value, ',');
        declaration.m_Apply = [values](Style& style)
        {
            for (std::size_t i = 0; i < style.m_Animations.size(); ++i)
                style.m_Animations[i].m_Paused = values[i % values.size()] == "paused";
        };
    }
    else if (name == "animation-timing-function")
    {
        const auto curve = ParseEasing(value);
        declaration.m_Apply = [curve](Style& style)
        {
            for (auto& animation : style.m_Animations)
                animation.m_Easing = curve;
        };
    }
    else if (name == "transform")
        declaration.m_Apply = [operations = parsed.m_Transform.m_Operations](Style& style)
        { style.m_Transform.m_Operations = operations; };
    else if (name == "transform-origin")
        declaration.m_Apply = [origin = parsed.m_Transform.m_Origin](Style& style)
        { style.m_Transform.m_Origin = origin; };
    else if (name.starts_with("padding-"))
    {
        float Insets::* member = name == "padding-top"      ? &Insets::m_Top
                                 : name == "padding-right"  ? &Insets::m_Right
                                 : name == "padding-bottom" ? &Insets::m_Bottom
                                                            : &Insets::m_Left;
        const float distance = parsed.m_Padding.*member;
        declaration.m_Properties = {"padding"};
        declaration.m_Apply = [member, distance](Style& style) { style.m_Padding.*member = distance; };
    }
    else if (name == "left" || name == "right" || name == "top" || name == "bottom")
    {
        Length Anchors::* member = name == "left"    ? &Anchors::m_Left
                                   : name == "right" ? &Anchors::m_Right
                                   : name == "top"   ? &Anchors::m_Top
                                                     : &Anchors::m_Bottom;
        const auto distance = parsed.m_Anchors.*member;
        declaration.m_Apply = [member, distance](Style& style) { style.m_Anchors.*member = distance; };
    }
    else
    {
        if (properties.empty())
            throw std::runtime_error("Property has no compiled representation: " + name);
        std::vector<std::pair<StyleProperty, StyleValue>> assignments;
        for (auto property : properties)
            assignments.emplace_back(property, GetStyleValue(parsed, property));
        declaration.m_Apply = [assignments = std::move(assignments)](Style& style)
        {
            for (const auto& [property, typed] : assignments)
                SetStyleValue(style, property, typed);
        };
    }
}

bool StyleSheet::Parse(std::string_view css, std::string& error)
{
    std::size_t offset = 0;
    try
    {
        std::string source(css);
        if (source.starts_with("\xEF\xBB\xBF"))
            source.replace(0, 3, "   ");
        for (std::size_t start = source.find("/*"); start != std::string::npos; start = source.find("/*", start + 2))
        {
            const auto end = source.find("*/", start + 2);
            if (end == std::string::npos)
            {
                offset = start;
                throw std::runtime_error("Unterminated CSS comment.");
            }
            for (std::size_t i = start; i < end + 2; ++i)
                if (source[i] != '\n')
                    source[i] = ' ';
        }

        std::vector<Rule> rules;
        std::map<std::string, std::vector<Keyframe>, std::less<>> keyframes;
        const auto parseDeclarations = [&](std::string_view body)
        {
            std::vector<Declaration> declarations;
            for (const auto& text : Split(body, ';'))
            {
                if (text.empty())
                    continue;
                const auto colon = text.find(':');
                if (colon == std::string::npos)
                    throw std::runtime_error("Expected property: value.");
                Declaration declaration;
                declaration.m_Name = Lower(Trim(text.substr(0, colon)));
                declaration.m_Value = Trim(text.substr(colon + 1));
                if (declaration.m_Value.ends_with("!important"))
                {
                    declaration.m_Important = true;
                    declaration.m_Value = Trim(declaration.m_Value.substr(0, declaration.m_Value.size() - 10));
                }
                Compile(declaration);
                declarations.push_back(std::move(declaration));
            }
            return declarations;
        };
        while (offset < source.size())
        {
            const auto begin = source.find_first_not_of(" \t\r\n", offset);
            if (begin == std::string::npos)
                break;
            offset = begin;
            const auto open = source.find('{', begin);
            const auto close = open == std::string::npos ? std::string::npos : CloseBlock(source, open);
            if (open == std::string::npos || close == std::string::npos)
                throw std::runtime_error("Expected a selector followed by { declarations }.");

            const auto heading = Trim(std::string_view(source).substr(begin, open - begin));
            if (heading.starts_with('@'))
            {
                if (!heading.starts_with("@keyframes "))
                    throw std::runtime_error("Only @keyframes at-rules are supported.");
                const auto name = Trim(std::string_view(heading).substr(11));
                if (name.empty() || !std::all_of(name.begin(), name.end(), IdentifierCharacter))
                    throw std::runtime_error("Invalid keyframe name.");
                std::vector<Keyframe> frames;
                std::size_t position = open + 1;
                while (position < close)
                {
                    const auto frameStart = source.find_first_not_of(" \t\r\n", position);
                    if (frameStart == std::string::npos || frameStart >= close)
                        break;
                    const auto frameOpen = source.find('{', frameStart);
                    if (frameOpen == std::string::npos || frameOpen >= close)
                        throw std::runtime_error("Expected a keyframe block.");
                    const auto frameClose = CloseBlock(source, frameOpen);
                    if (frameClose >= close)
                        throw std::runtime_error("Unterminated keyframe block.");
                    const auto declarations =
                        parseDeclarations(std::string_view(source).substr(frameOpen + 1, frameClose - frameOpen - 1));
                    Keyframe frame;
                    for (const auto& declaration : declarations)
                    {
                        if (declaration.m_Important)
                            throw std::runtime_error("!important is not supported in keyframes.");
                        if (declaration.m_Name == "animation-timing-function")
                            frame.m_Easing = ParseEasing(declaration.m_Value);
                        else if (declaration.m_Properties.empty())
                            throw std::runtime_error("Keyframes can only write style properties.");
                        frame.m_Properties.insert(frame.m_Properties.end(), declaration.m_Properties.begin(),
                                                  declaration.m_Properties.end());
                    }
                    frame.m_Apply = [declarations](Style& style)
                    {
                        for (const auto& declaration : declarations)
                            declaration.m_Apply(style);
                    };
                    for (const auto& selector :
                         Split(std::string_view(source).substr(frameStart, frameOpen - frameStart), ','))
                    {
                        frame.m_Offset = selector == "from" ? 0 : selector == "to" ? 1 : Percent(selector);
                        if (frame.m_Offset < 0 || frame.m_Offset > 1)
                            throw std::runtime_error("Keyframe offsets must be in [0%,100%].");
                        frames.push_back(frame);
                        if (frames.size() > 256)
                            throw std::runtime_error("At most 256 keyframe blocks are supported per animation.");
                    }
                    position = frameClose + 1;
                }
                std::stable_sort(frames.begin(), frames.end(),
                                 [](const auto& a, const auto& b) { return a.m_Offset < b.m_Offset; });
                keyframes[name] = std::move(frames);
                offset = close + 1;
                continue;
            }
            const auto declarations = parseDeclarations(std::string_view(source).substr(open + 1, close - open - 1));

            const auto selectors = source.substr(begin, open - begin);
            std::size_t selectorStart = 0;
            while (selectorStart <= selectors.size())
            {
                const auto comma = selectors.find(',', selectorStart);
                auto selector = Trim(std::string_view(selectors).substr(selectorStart, comma - selectorStart));
                if (selector.empty())
                    throw std::runtime_error("Empty CSS selector.");
                Rule rule;
                if (selector.ends_with("::before") || selector.ends_with("::after"))
                {
                    rule.m_PseudoElement = selector.ends_with("::before") ? 1 : 2;
                    selector.resize(selector.size() - (rule.m_PseudoElement == 1 ? 8 : 7));
                    ++rule.m_Specificity;
                }
                rule.m_Declarations = declarations;
                std::size_t i = 0;
                bool child = false;
                while (i < selector.size())
                {
                    while (i < selector.size() && std::isspace(static_cast<unsigned char>(selector[i])))
                        ++i;
                    if (i == selector.size())
                        break;
                    if (selector[i] == '>')
                    {
                        if (rule.m_Parts.empty() || child)
                            throw std::runtime_error("Invalid child selector.");
                        child = true;
                        ++i;
                        continue;
                    }
                    SelectorPart part;
                    part.m_Child = child;
                    child = false;
                    bool any = false;
                    while (i < selector.size() && !std::isspace(static_cast<unsigned char>(selector[i])) &&
                           selector[i] != '>')
                    {
                        const char prefix = selector[i];
                        if (prefix == '*')
                        {
                            ++i;
                            any = true;
                            continue;
                        }
                        if (prefix == '#' || prefix == '.' || prefix == ':')
                            ++i;
                        const auto wordStart = i;
                        while (i < selector.size() && IdentifierCharacter(selector[i]))
                            ++i;
                        if (wordStart == i)
                            throw std::runtime_error("Unsupported CSS selector: " + selector);
                        auto word = selector.substr(wordStart, i - wordStart);
                        any = true;
                        if (prefix == '#')
                        {
                            part.m_Id = word;
                            rule.m_Specificity += 10000;
                        }
                        else if (prefix == '.')
                        {
                            part.m_Classes.push_back(word);
                            rule.m_Specificity += 100;
                        }
                        else if (prefix == ':')
                        {
                            word = Lower(word);
                            if (word == "hover")
                                part.m_Required |= Hover;
                            else if (word == "pressed" || word == "active")
                                part.m_Required |= Pressed;
                            else if (word == "focus")
                                part.m_Required |= Focus;
                            else if (word == "disabled")
                                part.m_Required |= Disabled;
                            else if (word == "enabled")
                                part.m_Forbidden |= Disabled;
                            else if (word == "read-only")
                                part.m_Required |= ReadOnly;
                            else if (word == "selected" || word == "checked")
                                part.m_Required |= Selected;
                            else
                                throw std::runtime_error("Unsupported pseudo-class: " + word);
                            rule.m_Specificity += 100;
                        }
                        else
                        {
                            if (!part.m_Type.empty())
                                throw std::runtime_error("Invalid type selector.");
                            part.m_Type = Lower(word);
                            ++rule.m_Specificity;
                        }
                    }
                    if (any)
                        rule.m_Parts.push_back(std::move(part));
                    if (rule.m_Parts.size() > 32)
                        throw std::runtime_error("Selector nesting exceeds 32 levels.");
                }
                if (child || rule.m_Parts.empty())
                    throw std::runtime_error("Incomplete selector.");
                rules.push_back(std::move(rule));
                if (comma == std::string::npos)
                    break;
                selectorStart = comma + 1;
            }
            offset = close + 1;
        }
        m_Rules = std::move(rules);
        m_Keyframes = std::move(keyframes);
        ++m_Revision;
        error.clear();
        return true;
    }
    catch (const std::exception& exception)
    {
        const auto limit = std::min(offset, css.size());
        const auto line = 1 + std::count(css.begin(), css.begin() + static_cast<std::ptrdiff_t>(limit), '\n');
        error = "CSS line " + std::to_string(line) + ": " + exception.what();
        return false;
    }
}

bool StyleSheet::LoadFile(const std::filesystem::path& path, std::string& error)
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
    {
        error = "Cannot read stylesheet: " + path.string();
        return false;
    }
    return Parse(std::string(std::istreambuf_iterator<char>(input), {}), error);
}

bool StyleSheet::Matches(const Rule& rule, const StyleNode& node)
{
    const auto matchesPart = [](const SelectorPart& part, const StyleNode& target)
    {
        if ((!part.m_Type.empty() && part.m_Type != target.m_Type) ||
            (!part.m_Id.empty() && part.m_Id != target.m_Id) || (target.m_State & part.m_Required) != part.m_Required ||
            (target.m_State & part.m_Forbidden) != 0)
            return false;
        return std::all_of(
            part.m_Classes.begin(), part.m_Classes.end(), [&](const auto& name)
            { return std::find(target.m_Classes.begin(), target.m_Classes.end(), name) != target.m_Classes.end(); });
    };
    const auto match = [&](auto&& self, std::size_t index, const StyleNode* target) -> bool
    {
        if (!target || !matchesPart(rule.m_Parts[index], *target))
            return false;
        if (index == 0)
            return true;
        if (rule.m_Parts[index].m_Child)
            return self(self, index - 1, target->m_Parent);
        for (auto* ancestor = target->m_Parent; ancestor; ancestor = ancestor->m_Parent)
            if (self(self, index - 1, ancestor))
                return true;
        return false;
    };
    return match(match, rule.m_Parts.size() - 1, &node);
}

Style StyleSheet::Compute(const StyleNode& node, const Style* inherited, int pseudoElement) const
{
    Style style;
    if (inherited)
    {
        style.m_Color = inherited->m_Color;
        style.m_FontSize = inherited->m_FontSize;
        style.m_TextAlign = inherited->m_TextAlign;
    }
    if (pseudoElement == 0 && (node.m_Type == "button" || node.m_Type == "toolbutton" || node.m_Type == "textbox"))
    {
        style.m_Height = {Length::Unit::Pixels, 44};
        style.m_Padding = {10, 14, 10, 14};
        style.m_Radius = 7;
        style.m_BorderWidth = 1;
        style.m_Background = {0.12f, 0.16f, 0.25f, 1};
        style.m_BorderColor = {0.3f, 0.4f, 0.6f, 1};
        if (node.m_Type == "button" || node.m_Type == "toolbutton")
            style.m_TextAlign = "center";
    }

    struct Match
    {
        const Declaration* m_Declaration;
        unsigned m_Specificity;
    };

    std::vector<Match> matches;
    for (const auto& rule : m_Rules)
        if (rule.m_PseudoElement == pseudoElement && Matches(rule, node))
            for (const auto& declaration : rule.m_Declarations)
                matches.push_back({&declaration, rule.m_Specificity});
    std::stable_sort(matches.begin(), matches.end(),
                     [](const auto& a, const auto& b)
                     {
                         if (a.m_Declaration->m_Important != b.m_Declaration->m_Important)
                             return !a.m_Declaration->m_Important;
                         return a.m_Specificity < b.m_Specificity;
                     });
    for (const auto& match : matches)
        match.m_Declaration->m_Apply(style);
    style.m_MaxWidth = std::max(style.m_MaxWidth, style.m_MinWidth);
    style.m_MaxHeight = std::max(style.m_MaxHeight, style.m_MinHeight);
    return style;
}

const std::vector<StyleSheet::Keyframe>* StyleSheet::FindKeyframes(std::string_view name) const
{
    const auto found = m_Keyframes.find(name);
    return found == m_Keyframes.end() ? nullptr : &found->second;
}

unsigned StyleSheet::GetRevision() const
{
    return m_Revision;
}
} // namespace EStudioUI
