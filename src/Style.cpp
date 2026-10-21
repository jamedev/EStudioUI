#include <EStudioUI/Style.h>

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
    if (value == "auto")
        return {};
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
        style.m_Background = ParseColor(value);
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
        if (value == "none")
            style.m_Shadow = {};
        else
        {
            const auto parts = Words(value);
            if (parts.size() != 4)
                throw std::runtime_error("box-shadow requires: x y blur color.");
            style.m_Shadow = {Number(parts[0], true), Number(parts[1], true), Number(parts[2]), ParseColor(parts[3])};
        }
    }
    else
        throw std::runtime_error("Unsupported CSS property: " + name);
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
        while (offset < source.size())
        {
            const auto begin = source.find_first_not_of(" \t\r\n", offset);
            if (begin == std::string::npos)
                break;
            offset = begin;
            const auto open = source.find('{', begin);
            const auto close = open == std::string::npos ? std::string::npos : source.find('}', open + 1);
            if (open == std::string::npos || close == std::string::npos)
                throw std::runtime_error("Expected a selector followed by { declarations }.");

            std::vector<Declaration> declarations;
            std::size_t position = open + 1;
            while (position < close)
            {
                const auto next = std::min(source.find(';', position), close);
                const auto text = Trim(std::string_view(source).substr(position, next - position));
                if (!text.empty())
                {
                    offset = position;
                    const auto colon = text.find(':');
                    if (colon == std::string::npos)
                        throw std::runtime_error("Expected property: value.");
                    Declaration declaration{Lower(Trim(text.substr(0, colon))), Trim(text.substr(colon + 1)), false};
                    if (declaration.m_Value.ends_with("!important"))
                    {
                        declaration.m_Important = true;
                        declaration.m_Value = Trim(declaration.m_Value.substr(0, declaration.m_Value.size() - 10));
                    }
                    Style validation;
                    Apply(validation, declaration);
                    declarations.push_back(std::move(declaration));
                }
                position = next + 1;
            }

            const auto selectors = source.substr(begin, open - begin);
            std::size_t selectorStart = 0;
            while (selectorStart <= selectors.size())
            {
                const auto comma = selectors.find(',', selectorStart);
                const auto selector = Trim(std::string_view(selectors).substr(selectorStart, comma - selectorStart));
                if (selector.empty())
                    throw std::runtime_error("Empty CSS selector.");
                Rule rule;
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

Style StyleSheet::Compute(const StyleNode& node, const Style* inherited) const
{
    Style style;
    if (inherited)
    {
        style.m_Color = inherited->m_Color;
        style.m_FontSize = inherited->m_FontSize;
        style.m_TextAlign = inherited->m_TextAlign;
    }
    if (node.m_Type == "button" || node.m_Type == "textbox")
    {
        style.m_Height = {Length::Unit::Pixels, 44};
        style.m_Padding = {10, 14, 10, 14};
        style.m_Radius = 7;
        style.m_BorderWidth = 1;
        style.m_Background = {0.12f, 0.16f, 0.25f, 1};
        style.m_BorderColor = {0.3f, 0.4f, 0.6f, 1};
        if (node.m_Type == "button")
            style.m_TextAlign = "center";
    }

    struct Match
    {
        const Declaration* m_Declaration;
        unsigned m_Specificity;
    };

    std::vector<Match> matches;
    for (const auto& rule : m_Rules)
        if (Matches(rule, node))
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
        Apply(style, *match.m_Declaration);
    style.m_MaxWidth = std::max(style.m_MaxWidth, style.m_MinWidth);
    style.m_MaxHeight = std::max(style.m_MaxHeight, style.m_MinHeight);
    return style;
}

unsigned StyleSheet::GetRevision() const
{
    return m_Revision;
}
} // namespace EStudioUI
