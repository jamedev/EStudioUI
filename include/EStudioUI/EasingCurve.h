#pragma once

namespace EStudioUI
{
//! A time-to-progress curve, including CSS-compatible cubic Bezier curves.
class EasingCurve
{
  public:
    //! Built-in curves; Back and Elastic deliberately overshoot.
    enum class Kind
    {
        Linear,
        Ease,
        EaseIn,
        EaseOut,
        EaseInOut,
        InQuad,
        OutQuad,
        InOutQuad,
        InCubic,
        OutCubic,
        InOutCubic,
        InSine,
        OutSine,
        InOutSine,
        InExpo,
        OutExpo,
        InOutExpo,
        InBack,
        OutBack,
        InOutBack,
        OutBounce,
        OutElastic,
        CubicBezier
    };

    //! Selects a built-in curve; custom control points use CubicBezier().
    explicit EasingCurve(Kind kind = Kind::Linear);

    //! Creates a curve with x in [0,1] and finite y in [-1000,1000].
    static EasingCurve CubicBezier(double x1, double y1, double x2, double y2);

    //! Maps finite normalized time to progress, preserving exact endpoints.
    double Evaluate(double time) const;

  private:
    //! Evaluates y after finding the Bezier parameter for the supplied x.
    double EvaluateBezier(double time) const;

    //! Stores the selected curve.
    Kind m_Kind;

    //! Stores the first Bezier control point's x coordinate.
    double m_X1 = 0;

    //! Stores the first Bezier control point's y coordinate.
    double m_Y1 = 0;

    //! Stores the second Bezier control point's x coordinate.
    double m_X2 = 1;

    //! Stores the second Bezier control point's y coordinate.
    double m_Y2 = 1;
};
} // namespace EStudioUI
