#include <EStudioUI/Constraint.h>

#include <stdexcept>
#include <utility>

namespace EStudioUI
{
Constraint::Constraint(ConstraintExpression expression, ConstraintRelation relation)
    : m_Expression(std::move(expression)), m_Relation(relation)
{
    if (relation < ConstraintRelation::Equal || relation > ConstraintRelation::GreaterEqual)
        throw std::invalid_argument("Invalid constraint relation.");
}

const ConstraintExpression& Constraint::GetExpression() const
{
    return m_Expression;
}

ConstraintRelation Constraint::GetRelation() const
{
    return m_Relation;
}

Constraint operator==(ConstraintExpression left, ConstraintExpression right)
{
    return {std::move(left) - right, ConstraintRelation::Equal};
}

Constraint operator<=(ConstraintExpression left, ConstraintExpression right)
{
    return {std::move(left) - right, ConstraintRelation::LessEqual};
}

Constraint operator>=(ConstraintExpression left, ConstraintExpression right)
{
    return {std::move(left) - right, ConstraintRelation::GreaterEqual};
}
} // namespace EStudioUI
