#pragma once

#include "Types.h"
#include "EasingCurve.h"

#include <array>
#include <string>
#include <vector>

namespace EStudioUI
{
//! A column-vector affine map, composed without changing layout geometry.
struct Affine
{
    //! Matrix coefficients [a c x; b d y; 0 0 1].
    float m_A = 1, m_B = 0, m_C = 0, m_D = 1, m_X = 0, m_Y = 0;

    //! Compares all coefficients.
    bool operator==(const Affine&) const = default;

    //! Maps a logical point.
    Point Map(Point point) const;

    //! Composes this map with a local map.
    Affine operator*(const Affine& local) const;

    //! Returns false for a singular map.
    bool Inverse(Affine& inverse) const;

    //! Returns the enclosing axis-aligned rectangle.
    Rect MapBounds(Rect bounds) const;
};

//! One operation in an ordered CSS transform list.
struct TransformOperation
{
    //! Supported two-dimensional operations.
    enum class Kind
    {
        Translate,
        Scale,
        Rotate,
        SkewX,
        SkewY,
        Matrix
    };

    //! Selects the operation.
    Kind m_Kind = Kind::Translate;

    //! Translation, scale, angle, or six matrix coefficients.
    std::array<float, 6> m_Values{};

    //! Resolves translation components relative to the border box.
    bool m_PercentX = false, m_PercentY = false;

    //! Compares the complete operation.
    bool operator==(const TransformOperation&) const = default;
};

//! Up to eight ordered CSS operations and a percentage transform origin.
struct Transform
{
    //! Operations in CSS multiplication order.
    std::vector<TransformOperation> m_Operations;

    //! Border-box origin as fractions of width and height.
    Point m_Origin{0.5f, 0.5f};

    //! Compares operations and origin.
    bool operator==(const Transform&) const = default;

    //! Resolves percentages and the origin without modifying widget bounds.
    Affine Resolve(Rect bounds) const;
};

//! A normalized gradient stop.
struct GradientStop
{
    //! Fraction of the gradient axis or radial extent.
    float m_Position = 0;

    //! Straight-alpha color; the shader interpolates premultiplied values.
    Color m_Color;

    //! Compares position and color.
    bool operator==(const GradientStop&) const = default;
};

//! A bounded, analytic background image requiring no texture allocation.
struct Gradient
{
    //! Supported image types.
    enum class Kind
    {
        None,
        Linear,
        Radial
    };

    //! Image type.
    Kind m_Kind = Kind::None;

    //! CSS angle in degrees: zero points upward.
    float m_Angle = 180;

    //! Radial center relative to the border box.
    Point m_Center{0.5f, 0.5f};

    //! Radial ellipse radii relative to width and height.
    Point m_Radius{0.70710678f, 0.70710678f};

    //! Ordered stops, limited to eight during validation.
    std::vector<GradientStop> m_Stops;

    //! Compares all image parameters.
    bool operator==(const Gradient&) const = default;
};

//! Timing for an automatically generated state transition.
struct CssTransition
{
    //! Canonical property name, a supported shorthand, or all.
    std::string m_Property = "all";

    //! Duration and optional nonnegative delay in seconds.
    double m_Duration = 0, m_Delay = 0;

    //! Per-property timing curve.
    EasingCurve m_Easing{EasingCurve::Kind::Ease};

    //! Compares transition specifications.
    bool operator==(const CssTransition&) const = default;
};

//! One named CSS keyframe timeline.
struct CssAnimation
{
    //! Case-sensitive keyframe name.
    std::string m_Name;

    //! Duration and nonnegative delay in seconds.
    double m_Duration = 0, m_Delay = 0;

    //! Per-segment easing curve.
    EasingCurve m_Easing{EasingCurve::Kind::Ease};

    //! Iteration count; zero denotes infinite repetition.
    unsigned m_Iterations = 1;

    //! Playback direction.
    bool m_Reverse = false, m_Alternate = false;

    //! Whether endpoint samples apply outside the active interval.
    bool m_FillBefore = false, m_FillAfter = false;

    //! Suspends elapsed active time.
    bool m_Paused = false;

    //! Compares timeline specifications.
    bool operator==(const CssAnimation&) const = default;
};

//! Tests the interior of a uniformly rounded rectangle.
bool ContainsRounded(Rect bounds, float radius, Point point);
} // namespace EStudioUI
