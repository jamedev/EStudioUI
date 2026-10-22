#include <EStudioUI/Invalidation.h>

namespace EStudioUI
{
Invalidation GetStylePropertyInvalidation(StyleProperty property)
{
    // Validate the identifier even when no presentation value needs reading.
    GetStylePropertyName(property);
    if (IsPaintOnlyProperty(property))
        return property == StyleProperty::Transform || property == StyleProperty::ClipContent
                   ? Invalidation::HitTest | Invalidation::Paint
                   : Invalidation::Paint;
    if (property == StyleProperty::Color || property == StyleProperty::TextAlign)
        return Invalidation::InheritedStyle | Invalidation::Paint;
    if (property == StyleProperty::FontSize || property == StyleProperty::WordWrap ||
        property == StyleProperty::FontFamily || property == StyleProperty::FontWeight ||
        property == StyleProperty::FontStyle || property == StyleProperty::TextDirection ||
        property == StyleProperty::Language)
        return Invalidation::InheritedStyle | Invalidation::Content;
    return Invalidation::Content;
}
} // namespace EStudioUI
