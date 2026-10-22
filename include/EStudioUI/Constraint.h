#pragma once

#include "ConstraintExpression.h"

namespace EStudioUI
{
//! Required rules are hard; other strengths minimize weighted absolute error.
enum class ConstraintStrength
{
    Required,
    Strong,
    Medium,
    Weak
};

//! Linear comparison of an expression with zero.
enum class ConstraintRelation
{
    Equal,
    LessEqual,
    GreaterEqual
};

//! A reusable linear equality or inequality, constructed with ==, <= or >=.
class Constraint
{
  public:
    //! Creates a relation whose right-hand side is zero.
    Constraint(ConstraintExpression expression, ConstraintRelation relation);
    //! Returns the normalized left minus right expression.
    const ConstraintExpression& GetExpression() const;
    //! Returns the relation.
    ConstraintRelation GetRelation() const;

  private:
    //! Owned linear expression.
    ConstraintExpression m_Expression;
    //! Comparison against zero.
    ConstraintRelation m_Relation;
};

//! Creates an equality without evaluating widget geometry.
Constraint operator==(ConstraintExpression left, ConstraintExpression right);
//! Creates an upper-bound relation.
Constraint operator<=(ConstraintExpression left, ConstraintExpression right);
//! Creates a lower-bound relation.
Constraint operator>=(ConstraintExpression left, ConstraintExpression right);
} // namespace EStudioUI
