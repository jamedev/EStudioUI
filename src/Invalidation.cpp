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
    if (property == StyleProperty::FontSize)
        return Invalidation::InheritedStyle | Invalidation::Content;
    return Invalidation::Content;
}
} // namespace EStudioUI
