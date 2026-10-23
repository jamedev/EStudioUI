#pragma once

#include <EStudioUI/Style.h>

namespace EStudioUI::Detail
{
//! Insets scrollbar geometry by the same border and padding used for painting.
Rect ScrollTrackContent(Rect bounds, const Style& style);

//! Resolves a proportional, CSS-constrained thumb within a track interior.
Rect ScrollThumb(Rect track, const Style& style, bool horizontal, double visibleFraction, double fraction);

//! Returns the finite normalized position of an ordered double range.
double ScrollFraction(double value, double minimum, double maximum);

//! Maps a captured pointer displacement to a normalized scroll position.
double ScrollDragFraction(double origin, float start, float current, Rect track, Rect thumb, bool horizontal);
} // namespace EStudioUI::Detail
