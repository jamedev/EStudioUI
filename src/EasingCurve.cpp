#include <EStudioUI/EasingCurve.h>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace EStudioUI
{
namespace
{
double Bounce(double time)
{
    if (time < 1 / 2.75)
        return 7.5625 * time * time;
    if (time < 2 / 2.75)
    {
        time -= 1.5 / 2.75;
        return 7.5625 * time * time + 0.75;
    }
    if (time < 2.5 / 2.75)
    {
        time -= 2.25 / 2.75;
        return 7.5625 * time * time + 0.9375;
    }
    time -= 2.625 / 2.75;
    return 7.5625 * time * time + 0.984375;
}
} // namespace

EasingCurve::EasingCurve(Kind kind) : m_Kind(kind)
{
    if (kind < Kind::Linear || kind >= Kind::CubicBezier)
        throw std::invalid_argument("Use the CubicBezier or Steps factory for configured curves.");
    if (kind == Kind::Ease)
    {
        m_X1 = 0.25;
        m_Y1 = 0.1;
        m_X2 = 0.25;
    }
    else if (kind == Kind::EaseIn)
        m_X1 = 0.42;
    else if (kind == Kind::EaseOut)
        m_X2 = 0.58;
    else if (kind == Kind::EaseInOut)
    {
        m_X1 = 0.42;
        m_X2 = 0.58;
    }
}

EasingCurve EasingCurve::CubicBezier(double x1, double y1, double x2, double y2)
{
    if (!std::isfinite(x1) || !std::isfinite(y1) || !std::isfinite(x2) || !std::isfinite(y2) || x1 < 0 || x1 > 1 ||
        x2 < 0 || x2 > 1 || std::abs(y1) > 1000 || std::abs(y2) > 1000)
        throw std::invalid_argument("Bezier control points require x in [0,1] and finite y in [-1000,1000].");
    EasingCurve result;
    result.m_Kind = Kind::CubicBezier;
    result.m_X1 = x1;
    result.m_Y1 = y1;
    result.m_X2 = x2;
    result.m_Y2 = y2;
    return result;
}

double EasingCurve::EvaluateBezier(double time) const
{
    const auto coordinate = [](double parameter, double first, double second)
    {
        const double inverse = 1 - parameter;
        return 3 * inverse * inverse * parameter * first + 3 * inverse * parameter * parameter * second +
               parameter * parameter * parameter;
    };
    // Invert x(t), rather than using time directly as the Bezier parameter.
    // Bisection also handles vertical tangents and coincident control points.
    double lower = 0;
    double upper = 1;
    for (int i = 0; i < 40; ++i)
    {
        const double middle = (lower + upper) * 0.5;
        if (coordinate(middle, m_X1, m_X2) < time)
            lower = middle;
        else
            upper = middle;
    }
    return coordinate((lower + upper) * 0.5, m_Y1, m_Y2);
}

EasingCurve EasingCurve::Steps(unsigned count, bool jumpStart)
{
    if (count == 0 || count > 1000000)
        throw std::invalid_argument("Steps require a count in [1,1000000].");
    EasingCurve result;
    result.m_Kind = Kind::Steps;
    result.m_Steps = count;
    result.m_JumpStart = jumpStart;
    return result;
}

double EasingCurve::Evaluate(double time) const
{
    if (!std::isfinite(time))
        throw std::invalid_argument("Easing time must be finite.");
    if (m_Kind == Kind::Steps)
        return std::clamp((std::floor(time * m_Steps) + (m_JumpStart ? 1 : 0)) / m_Steps, 0.0, 1.0);
    if (time <= 0)
        return 0;
    if (time >= 1)
        return 1;
    constexpr double pi = std::numbers::pi;
    constexpr double back = 1.70158;
    const double inverse = 1 - time;
    switch (m_Kind)
    {
    case Kind::Steps:
        return 0;
    case Kind::Linear:
        return time;
    case Kind::Ease:
    case Kind::EaseIn:
    case Kind::EaseOut:
    case Kind::EaseInOut:
    case Kind::CubicBezier:
        return EvaluateBezier(time);
    case Kind::InQuad:
        return time * time;
    case Kind::OutQuad:
        return 1 - inverse * inverse;
    case Kind::InOutQuad:
        return time < 0.5 ? 2 * time * time : 1 - 2 * inverse * inverse;
    case Kind::InCubic:
        return time * time * time;
    case Kind::OutCubic:
        return 1 - inverse * inverse * inverse;
    case Kind::InOutCubic:
        return time < 0.5 ? 4 * time * time * time : 1 - 4 * inverse * inverse * inverse;
    case Kind::InSine:
        return 1 - std::cos(time * pi * 0.5);
    case Kind::OutSine:
        return std::sin(time * pi * 0.5);
    case Kind::InOutSine:
        return (1 - std::cos(time * pi)) * 0.5;
    case Kind::InExpo:
        return std::exp2(10 * time - 10);
    case Kind::OutExpo:
        return 1 - std::exp2(-10 * time);
    case Kind::InOutExpo:
        return time < 0.5 ? std::exp2(20 * time - 10) * 0.5 : 1 - std::exp2(-20 * time + 10) * 0.5;
    case Kind::InBack:
        return time * time * ((back + 1) * time - back);
    case Kind::OutBack:
        return 1 + (time - 1) * (time - 1) * ((back + 1) * (time - 1) + back);
    case Kind::InOutBack:
    {
        constexpr double amount = back * 1.525;
        const double t = time < 0.5 ? time * 2 : time * 2 - 2;
        return time < 0.5 ? t * t * ((amount + 1) * t - amount) * 0.5 : (t * t * ((amount + 1) * t + amount) + 2) * 0.5;
    }
    case Kind::OutBounce:
        return Bounce(time);
    case Kind::OutElastic:
        return std::exp2(-10 * time) * std::sin((10 * time - 0.75) * 2 * pi / 3) + 1;
    }
    return time;
}
} // namespace EStudioUI
