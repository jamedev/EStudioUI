#include <EStudioUI/ConstraintExpression.h>

#include <cmath>
#include <stdexcept>
#include <utility>

namespace EStudioUI
{
namespace
{
double Finite(double value)
{
    if (!std::isfinite(value) || std::abs(value) > 1e12)
        throw std::invalid_argument("Constraint coefficients and constants must be finite and within +/-1e12.");
    return value;
}
} // namespace

ConstraintExpression::ConstraintExpression(double constant) : m_Constant(Finite(constant))
{
}

ConstraintExpression::ConstraintExpression(ConstraintTerm term) : m_Terms{term}
{
    Finite(term.m_Coefficient);
    if (term.m_Coordinate < ConstraintCoordinate::X || term.m_Coordinate > ConstraintCoordinate::Height)
        throw std::invalid_argument("Invalid constraint coordinate.");
}

const std::vector<ConstraintTerm>& ConstraintExpression::GetTerms() const
{
    return m_Terms;
}

double ConstraintExpression::GetConstant() const
{
    return m_Constant;
}

ConstraintExpression operator+(ConstraintExpression left, const ConstraintExpression& right)
{
    left.m_Constant = Finite(left.m_Constant + right.m_Constant);
    left.m_Terms.insert(left.m_Terms.end(), right.m_Terms.begin(), right.m_Terms.end());
    return left;
}

ConstraintExpression operator-(ConstraintExpression left, const ConstraintExpression& right)
{
    return std::move(left) + right * -1;
}

ConstraintExpression operator*(ConstraintExpression value, double factor)
{
    Finite(factor);
    value.m_Constant = Finite(value.m_Constant * factor);
    for (auto& term : value.m_Terms)
        term.m_Coefficient = Finite(term.m_Coefficient * factor);
    return value;
}

ConstraintExpression operator*(double factor, ConstraintExpression value)
{
    return std::move(value) * factor;
}

ConstraintExpression operator/(ConstraintExpression value, double divisor)
{
    Finite(divisor);
    if (divisor == 0)
        throw std::invalid_argument("Constraint division by zero.");
    return std::move(value) * (1 / divisor);
}

ConstraintExpression operator-(ConstraintExpression value)
{
    return std::move(value) * -1;
}
} // namespace EStudioUI
