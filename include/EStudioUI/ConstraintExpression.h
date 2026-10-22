#pragma once

#include <cstdint>
#include <vector>

namespace EStudioUI
{
//! Independent linear coordinates; composite anchors expand into these terms.
enum class ConstraintCoordinate
{
    X,
    Y,
    Width,
    Height
};

//! An owned coefficient, without a widget pointer or solver dependency.
struct ConstraintTerm
{
    //! Stable widget identity; zero denotes the container content rectangle.
    std::uint64_t m_Target = 0;
    //! Coordinate multiplied by this term.
    ConstraintCoordinate m_Coordinate = ConstraintCoordinate::X;
    //! Finite linear coefficient.
    double m_Coefficient = 1;
    //! Compares terms for incremental solver reuse.
    bool operator==(const ConstraintTerm&) const = default;
};

//! Owned linear expression supporting arithmetic with anchors and constants.
class ConstraintExpression
{
  public:
    //! Converts a finite pixel constant into an expression.
    ConstraintExpression(double constant = 0);
    //! Creates a single anchor term.
    explicit ConstraintExpression(ConstraintTerm term);
    //! Returns the immutable coefficients.
    const std::vector<ConstraintTerm>& GetTerms() const;
    //! Returns the constant part.
    double GetConstant() const;
    //! Adds two expressions, preserving finite coefficients.
    friend ConstraintExpression operator+(ConstraintExpression left, const ConstraintExpression& right);
    //! Subtracts two expressions.
    friend ConstraintExpression operator-(ConstraintExpression left, const ConstraintExpression& right);
    //! Multiplies by a finite scalar; variable products are deliberately unavailable.
    friend ConstraintExpression operator*(ConstraintExpression value, double factor);
    //! Supports a scalar on the left.
    friend ConstraintExpression operator*(double factor, ConstraintExpression value);
    //! Divides by a finite nonzero scalar.
    friend ConstraintExpression operator/(ConstraintExpression value, double divisor);
    //! Negates an expression.
    friend ConstraintExpression operator-(ConstraintExpression value);

  private:
    //! Owned terms, normalized when entering the solver.
    std::vector<ConstraintTerm> m_Terms;
    //! Finite constant offset.
    double m_Constant = 0;
};
} // namespace EStudioUI
