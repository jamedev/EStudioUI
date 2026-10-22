#include <EStudioUI/ConstraintLayout.h>

#include "LayoutDetail.h"

#include <kiwi/kiwi.h>

#include <array>
#include <map>
#include <sstream>
#include <unordered_map>

namespace EStudioUI
{
namespace
{
struct Rule
{
    Constraint m_Constraint;
    ConstraintStrength m_Strength;
    std::string m_Name;
    std::string m_Description;
};

struct Row
{
    std::vector<std::pair<std::size_t, double>> m_Terms;
    double m_Constant = 0;
    ConstraintRelation m_Relation = ConstraintRelation::Equal;
    double m_Strength = kiwi::strength::required;
    std::optional<std::size_t> m_Rule;
    bool m_Pinned = false;

    bool operator==(const Row&) const = default;
};

bool IsEdit(const Row& row, bool exact)
{
    return (!exact && row.m_Pinned) ||
           (!row.m_Rule && row.m_Relation == ConstraintRelation::Equal && row.m_Strength < kiwi::strength::required &&
            row.m_Terms.size() == 1 && row.m_Terms.front().second == 1);
}

std::string Describe(const Constraint& constraint)
{
    const char* coordinates[]{"left", "top", "width", "height"};
    std::ostringstream out;
    for (const auto& term : constraint.GetExpression().GetTerms())
    {
        out << term.m_Coefficient << " * ";
        if (term.m_Target)
            out << '@' << term.m_Target;
        else
            out << "parent";
        out << '.' << coordinates[static_cast<unsigned>(term.m_Coordinate)] << " + ";
    }
    out << constraint.GetExpression().GetConstant();
    out << (constraint.GetRelation() == ConstraintRelation::Equal       ? " == 0"
            : constraint.GetRelation() == ConstraintRelation::LessEqual ? " <= 0"
                                                                        : " >= 0");
    return out.str();
}

LayoutProblem Problem(const Row& row, LayoutIssue issue, float error, std::string message)
{
    LayoutProblem problem{issue, Axis::Horizontal, {}, 0, error, std::move(message)};
    for (const auto& [index, coefficient] : row.m_Terms)
    {
        (void)coefficient;
        if (index / 4 > 0)
        {
            problem.m_Child = index / 4 - 1;
            problem.m_Axis = index % 4 == 1 || index % 4 == 3 ? Axis::Vertical : Axis::Horizontal;
            break;
        }
    }
    return problem;
}

double Strength(ConstraintStrength strength)
{
    switch (strength)
    {
    case ConstraintStrength::Required:
        return kiwi::strength::required;
    case ConstraintStrength::Strong:
        return kiwi::strength::strong;
    case ConstraintStrength::Medium:
        return kiwi::strength::medium;
    case ConstraintStrength::Weak:
        return kiwi::strength::weak;
    }
    throw std::invalid_argument("Invalid constraint strength.");
}

kiwi::Constraint Compile(const Row& row, const std::vector<kiwi::Variable>& variables)
{
    std::vector<kiwi::Term> terms;
    for (const auto& [index, coefficient] : row.m_Terms)
        terms.emplace_back(variables[index], coefficient);
    return {kiwi::Expression(std::move(terms), row.m_Constant),
            row.m_Relation == ConstraintRelation::Equal       ? kiwi::OP_EQ
            : row.m_Relation == ConstraintRelation::LessEqual ? kiwi::OP_LE
                                                              : kiwi::OP_GE,
            row.m_Strength};
}

double Residual(const Row& row, const std::vector<double>& values)
{
    double value = row.m_Constant;
    for (const auto& [index, coefficient] : row.m_Terms)
        value += coefficient * values[index];
    return row.m_Relation == ConstraintRelation::Equal       ? std::abs(value)
           : row.m_Relation == ConstraintRelation::LessEqual ? std::max(0.0, value)
                                                             : std::max(0.0, -value);
}

struct Tableau
{
    kiwi::Solver m_Solver;
    std::vector<kiwi::Variable> m_Variables;
    std::vector<std::uint64_t> m_Targets;
    std::vector<Row> m_Rows;
    std::vector<kiwi::Constraint> m_Compiled;
    std::vector<std::size_t> m_Rejected;
    std::vector<double> m_Values;
    bool m_Exact = false;

    void Add(const Row& row, kiwi::Constraint& compiled)
    {
        if (IsEdit(row, m_Exact))
        {
            const auto& variable = m_Variables[row.m_Terms.front().first];
            m_Solver.addEditVariable(variable, row.m_Pinned ? 100 * kiwi::strength::strong : row.m_Strength);
            m_Solver.suggestValue(variable, -row.m_Constant);
        }
        else
        {
            compiled = Compile(row, m_Variables);
            m_Solver.addConstraint(compiled);
        }
    }

    void Build(const std::vector<Row>& rows, ConstraintLayoutStats& stats, bool exact = false)
    {
        ++stats.m_Builds;
        m_Exact = exact;
        m_Solver.reset();
        m_Compiled.clear();
        m_Compiled.resize(rows.size());
        m_Rejected.clear();
        for (std::size_t i = 0; i < rows.size(); ++i)
        {
            try
            {
                Add(rows[i], m_Compiled[i]);
            }
            catch (const kiwi::UnsatisfiableConstraint&)
            {
                if (!rows[i].m_Rule)
                    throw std::logic_error("Constraint layout encountered inconsistent internal bounds.");
                m_Rejected.push_back(i);
                // A failed Kiwi addition may have modified its tableau. Reconstruct
                // the accepted prefix rather than retaining partially installed rows.
                m_Solver.reset();
                for (std::size_t j = 0; j < i; ++j)
                    if (std::find(m_Rejected.begin(), m_Rejected.end(), j) == m_Rejected.end())
                        Add(rows[j], m_Compiled[j]);
            }
        }
    }

    void Solve(const std::vector<std::uint64_t>& targets, const std::vector<Row>& rows, ConstraintLayoutStats& stats)
    {
        if (m_Targets == targets && m_Rows == rows && !m_Variables.empty())
        {
            ++stats.m_CacheHits;
            return;
        }
        ++stats.m_Solves;
        bool rebuild = m_Targets != targets || m_Rows.size() != rows.size() || !m_Rejected.empty() || m_Exact;
        if (m_Targets != targets || m_Variables.empty())
        {
            m_Solver.reset();
            m_Variables.clear();
            m_Variables.resize((targets.size() + 1) * 4);
            m_Targets = targets;
            rebuild = true;
        }
        if (!rebuild)
            for (std::size_t i = 0; i < rows.size(); ++i)
                if (m_Rows[i].m_Terms != rows[i].m_Terms || m_Rows[i].m_Relation != rows[i].m_Relation ||
                    m_Rows[i].m_Strength != rows[i].m_Strength || m_Rows[i].m_Rule != rows[i].m_Rule ||
                    m_Rows[i].m_Pinned != rows[i].m_Pinned)
                    rebuild = true;
        if (!rebuild)
        {
            try
            {
                // Remove every changed row before inserting replacements so an
                // otherwise feasible resize cannot conflict with its old dimensions.
                for (std::size_t i = 0; i < rows.size(); ++i)
                    if (m_Rows[i].m_Constant != rows[i].m_Constant && !IsEdit(rows[i], m_Exact))
                        m_Solver.removeConstraint(m_Compiled[i]);
                for (std::size_t i = 0; i < rows.size(); ++i)
                    if (m_Rows[i].m_Constant != rows[i].m_Constant)
                    {
                        if (IsEdit(rows[i], m_Exact))
                            m_Solver.suggestValue(m_Variables[rows[i].m_Terms.front().first], -rows[i].m_Constant);
                        else
                            Add(rows[i], m_Compiled[i]);
                    }
                ++stats.m_Updates;
            }
            catch (const kiwi::UnsatisfiableConstraint&)
            {
                rebuild = true;
            }
        }
        if (rebuild)
            Build(rows, stats);
        m_Rows = rows;
        const auto read = [&]
        {
            m_Solver.updateVariables();
            m_Values.clear();
            for (const auto& variable : m_Variables)
            {
                const double value = variable.value();
                if (!std::isfinite(value) || std::abs(value) > 1e9)
                    throw std::runtime_error("Constraint solution is nonfinite or exceeds +/-1e9 logical pixels.");
                m_Values.push_back(value);
            }
        };
        read();
        // Hard edit targets are a speculative fast path. Check every target
        // before exposing a plan; real required rows handle infeasibility.
        bool exactNeeded = !m_Rejected.empty();
        for (const auto& row : rows)
            exactNeeded |= row.m_Pinned && Residual(row, m_Values) > 0.001;
        if (!m_Exact && exactNeeded)
        {
            Build(rows, stats, true);
            read();
        }
    }
};

struct Cache
{
    unsigned m_Kind = 0;
    bool m_Valid = false;
    Tableau m_Width;
    Tableau m_Final;
    bool m_PinHorizontal = false;
};

void AddRow(std::vector<Row>& rows, std::size_t index, double value, ConstraintRelation relation,
            double strength = kiwi::strength::required)
{
    rows.push_back({{{index, 1}}, -value, relation, strength, {}});
}

std::vector<Row> SystemRows(const LayoutContext& context, const std::vector<MeasureResult>& measures,
                            const std::vector<double>* horizontal)
{
    using namespace Detail;
    std::vector<Row> rows;
    AddRow(rows, 0, 0, ConstraintRelation::Equal);
    AddRow(rows, 1, 0, ConstraintRelation::Equal);
    for (const bool width : {true, false})
    {
        const auto value = width ? context.m_Constraints.m_Width : context.m_Constraints.m_Height;
        const auto index = width ? 2u : 3u;
        AddRow(rows, index, 0, ConstraintRelation::GreaterEqual);
        AddRow(rows, index, value.value_or(0), ConstraintRelation::Equal, value ? kiwi::strength::required : 0.00001);
        rows.back().m_Pinned = value.has_value();
    }
    for (std::size_t i = 0; i < measures.size(); ++i)
    {
        const auto& child = context.m_Children[i];
        const auto& measured = measures[i];
        const auto base = (i + 1) * 4;
        for (const bool width : {true, false})
        {
            const auto size = base + (width ? 2 : 3);
            const auto position = base + (width ? 0 : 1);
            const auto length = width ? child.m_Width : child.m_Height;
            const auto maximum = width ? child.m_Maximum.m_X : child.m_Maximum.m_Y;
            const auto minimum = Minimum(child, measured, width);
            const double before = width ? child.m_Margin.m_Left : child.m_Margin.m_Top;
            const double after = width ? child.m_Margin.m_Right : child.m_Margin.m_Bottom;
            const auto reference = width ? context.m_Constraints.m_Width : context.m_Constraints.m_Height;
            const bool definite = width ? context.m_DefiniteWidth : context.m_DefiniteHeight;
            AddRow(rows, size, minimum, ConstraintRelation::GreaterEqual);
            AddRow(rows, size, maximum, ConstraintRelation::LessEqual);
            const bool fixed = length.m_Unit == Length::Unit::Pixels ||
                               (length.m_Unit == Length::Unit::Percent && definite && reference);
            if (fixed)
                AddRow(rows, size, std::clamp(length.Resolve(reference.value_or(0), 0), minimum, maximum),
                       ConstraintRelation::Equal);
            else if (horizontal && width)
            {
                // The vertical pass pins this width instead of adding a second
                // edit preference for the same variable.
            }
            else if (length.m_Unit == Length::Unit::Fraction && reference)
                AddRow(rows, size, std::clamp(*reference - static_cast<float>(before + after), minimum, maximum),
                       ConstraintRelation::Equal, 0.1);
            else
                AddRow(rows, size, width ? measured.m_Preferred.m_X : measured.m_Preferred.m_Y,
                       ConstraintRelation::Equal, 1);
            if (!(horizontal && width))
                AddRow(rows, position, before, ConstraintRelation::Equal, 0.0001);
            AddRow(rows, position, before, ConstraintRelation::GreaterEqual, 10);
            rows.push_back(
                {{{position, 1}, {size, 1}, {width ? 2u : 3u, -1}}, after, ConstraintRelation::LessEqual, 10, {}});
        }
        if (horizontal)
        {
            if ((*horizontal)[base + 2] < Minimum(child, measured, true) - 0.01)
                throw std::logic_error("A width-dependent measurement increased its horizontal minimum after width "
                                       "resolution; horizontal minima must be independent of allocated width.");
            AddRow(rows, base, (*horizontal)[base], ConstraintRelation::Equal);
            rows.back().m_Pinned = true;
            AddRow(rows, base + 2, (*horizontal)[base + 2], ConstraintRelation::Equal);
            rows.back().m_Pinned = true;
        }
    }
    return rows;
}
} // namespace

struct ConstraintLayout::Impl
{
    std::vector<Rule> m_Rules;
    mutable std::array<Cache, 3> m_Caches;
    mutable std::size_t m_NextCache = 0;
    mutable ConstraintLayoutStats m_Stats;
};

ConstraintLayout::ConstraintLayout() : m_Impl(std::make_unique<Impl>())
{
}

ConstraintLayout::~ConstraintLayout() = default;

ConstraintLayout::ConstraintLayout(const ConstraintLayout& other) : ConstraintLayout()
{
    if (other.m_Impl)
        m_Impl->m_Rules = other.m_Impl->m_Rules;
}

ConstraintLayout& ConstraintLayout::operator=(const ConstraintLayout& other)
{
    if (this != &other)
    {
        ConstraintLayout copy(other);
        m_Impl.swap(copy.m_Impl);
    }
    return *this;
}

ConstraintLayout::ConstraintLayout(ConstraintLayout&& other) noexcept = default;

ConstraintLayout& ConstraintLayout::operator=(ConstraintLayout&& other) noexcept = default;

ConstraintLayout& ConstraintLayout::Add(Constraint constraint, ConstraintStrength strength, std::string name)
{
    Strength(strength);
    if (!m_Impl)
        m_Impl = std::make_unique<Impl>();
    if (m_Impl->m_Rules.size() >= 10000 || constraint.GetExpression().GetTerms().size() > 10000)
        throw std::length_error("A constraint layout supports at most 10000 rules and 10000 terms per rule.");
    if (name.empty())
        name = "rule " + std::to_string(m_Impl->m_Rules.size() + 1);
    auto description = Describe(constraint);
    m_Impl->m_Rules.push_back({std::move(constraint), strength, std::move(name), std::move(description)});
    for (auto& cache : m_Impl->m_Caches)
        cache.m_Valid = false;
    return *this;
}

std::string_view ConstraintLayout::GetName() const
{
    return "Constraints";
}

ConstraintLayoutStats ConstraintLayout::GetStats() const
{
    return m_Impl ? m_Impl->m_Stats : ConstraintLayoutStats{};
}

LayoutPlan ConstraintLayout::Compute(const LayoutContext& context) const
{
    using namespace Detail;
    context.m_Constraints.Validate();
    if (!m_Impl)
        throw std::logic_error("Cannot compute with a moved-from constraint layout.");
    LayoutPlan plan;
    const auto count = context.m_Children.size();
    std::vector<std::uint64_t> targets;
    std::unordered_map<std::uint64_t, std::size_t> indices{{0, 0}};
    std::vector<MeasureResult> measures;
    for (std::size_t i = 0; i < count; ++i)
    {
        const auto target = context.m_Children[i].m_Target;
        if (!target || !indices.emplace(target, i + 1).second)
            throw std::invalid_argument("Constraint children require unique, nonzero widget identities.");
        targets.push_back(target);
        measures.push_back(MeasureChild(context, i));
    }
    std::vector<Row> userRows;
    for (std::size_t i = 0; i < m_Impl->m_Rules.size(); ++i)
    {
        const auto& rule = m_Impl->m_Rules[i];
        const char* strengthNames[]{"Required", "Strong", "Medium", "Weak"};
        plan.m_Constraints.push_back(
            {rule.m_Name, rule.m_Description, strengthNames[static_cast<unsigned>(rule.m_Strength)], "inactive", 0});
        std::map<std::size_t, double> terms;
        bool active = true;
        for (const auto& term : rule.m_Constraint.GetExpression().GetTerms())
        {
            if (term.m_Coefficient == 0)
                continue;
            const auto found = indices.find(term.m_Target);
            if (found == indices.end())
            {
                active = false;
                break;
            }
            terms[found->second * 4 + static_cast<std::size_t>(term.m_Coordinate)] += term.m_Coefficient;
        }
        if (!active)
        {
            plan.m_Problems.push_back({LayoutIssue::ConstraintInactive,
                                       Axis::Horizontal,
                                       {},
                                       0,
                                       0,
                                       "Constraint '" + rule.m_Name +
                                           "' is inactive: an anchor is not a displayed, in-flow immediate child."});
            continue;
        }
        Row row;
        row.m_Constant = rule.m_Constraint.GetExpression().GetConstant();
        row.m_Relation = rule.m_Constraint.GetRelation();
        row.m_Strength = Strength(rule.m_Strength);
        row.m_Rule = i;
        for (const auto& [index, coefficient] : terms)
            if (coefficient != 0)
                row.m_Terms.emplace_back(index, coefficient);
        double scale = 0;
        for (const auto& [index, coefficient] : row.m_Terms)
        {
            (void)index;
            if (!std::isfinite(coefficient) || std::abs(coefficient) > 1e12)
                throw std::invalid_argument("Combined constraint coefficient exceeds +/-1e12.");
            scale = std::max(scale, std::abs(coefficient));
        }
        // Equivalent scalar multiples should have the same numerical tolerance
        // and soft-error units, including coefficients below Kiwi's epsilon.
        if (scale > 0)
        {
            for (auto& [index, coefficient] : row.m_Terms)
            {
                (void)index;
                coefficient /= scale;
            }
            row.m_Constant /= scale;
            if (!std::isfinite(row.m_Constant) || std::abs(row.m_Constant) > 1e12)
                throw std::invalid_argument("Normalized constraint constant exceeds +/-1e12.");
        }
        userRows.push_back(std::move(row));
    }
    const unsigned kind = (context.m_Constraints.m_Width ? 1u : 0u) | (context.m_Constraints.m_Height ? 2u : 0u);
    Cache* selected = nullptr;
    for (auto& cache : m_Impl->m_Caches)
        if (cache.m_Valid && cache.m_Kind == kind)
            selected = &cache;
    if (!selected)
    {
        selected = &m_Impl->m_Caches[m_Impl->m_NextCache++ % m_Impl->m_Caches.size()];
        selected->m_Valid = true;
        selected->m_Kind = kind;
        selected->m_PinHorizontal = false;
    }
    auto rows = SystemRows(context, measures, nullptr);
    rows.insert(rows.end(), userRows.begin(), userRows.end());
    selected->m_Width.Solve(targets, rows, m_Impl->m_Stats);
    const auto& widths = selected->m_Width.m_Values;
    bool heightForWidth = false;
    for (std::size_t i = 0; i < count; ++i)
        if (measures[i].m_HeightForWidth)
        {
            heightForWidth = true;
            measures[i] =
                MeasureChild(context, i, {static_cast<float>(widths[(i + 1) * 4 + 2]), {}}, context.m_DefiniteWidth);
        }
    auto* result = &selected->m_Width;
    if (heightForWidth)
    {
        bool coupled = selected->m_PinHorizontal;
        for (const auto& row : userRows)
        {
            bool horizontal = false, vertical = false;
            for (const auto& [index, coefficient] : row.m_Terms)
            {
                (void)coefficient;
                horizontal |= index % 4 == 0 || index % 4 == 2;
                vertical |= index % 4 == 1 || index % 4 == 3;
            }
            coupled |= horizontal && vertical;
        }
        rows = SystemRows(context, measures, coupled ? &widths : nullptr);
        rows.insert(rows.end(), userRows.begin(), userRows.end());
        selected->m_Final.Solve(targets, rows, m_Impl->m_Stats);
        if (!coupled)
        {
            bool changed = false;
            for (std::size_t i = 0; i < count; ++i)
                for (const auto coordinate : {0u, 2u})
                    changed |= std::abs(selected->m_Final.m_Values[(i + 1) * 4 + coordinate] -
                                        widths[(i + 1) * 4 + coordinate]) > 0.001;
            if (changed)
            {
                selected->m_PinHorizontal = true;
                rows = SystemRows(context, measures, &widths);
                rows.insert(rows.end(), userRows.begin(), userRows.end());
                selected->m_Final.Solve(targets, rows, m_Impl->m_Stats);
            }
        }
        result = &selected->m_Final;
    }
    const auto& solved = result->m_Values;
    for (const auto& row : rows)
        if (row.m_Rule)
        {
            auto& decision = plan.m_Constraints[*row.m_Rule];
            decision.m_Error = static_cast<float>(std::min(Residual(row, solved), 1e9));
            decision.m_Status = decision.m_Error > 0.01f ? "relaxed" : "satisfied";
        }
    for (const auto rejected : result->m_Rejected)
    {
        const auto& rule = m_Impl->m_Rules[*rows[rejected].m_Rule];
        plan.m_Constraints[*rows[rejected].m_Rule].m_Status = "conflict";
        plan.m_Problems.push_back(
            Problem(rows[rejected], LayoutIssue::ConstraintConflict,
                    static_cast<float>(std::min(Residual(rows[rejected], solved), 1e9)),
                    "Required constraint '" + rule.m_Name +
                        "' conflicts with hard widget limits, the viewport, width-first allocation, or "
                        "earlier required rules; it is omitted. Earlier required rules are preserved."));
    }
    for (const auto& row : rows)
        if (row.m_Rule && row.m_Strength < kiwi::strength::required && Residual(row, solved) > 0.01)
            plan.m_Problems.push_back(Problem(row, LayoutIssue::ConstraintRelaxed,
                                              static_cast<float>(std::min(Residual(row, solved), 1e9)),
                                              "Soft constraint '" + m_Impl->m_Rules[*row.m_Rule].m_Name +
                                                  "' is relaxed by the weighted priority solution."));
    plan.m_Measure.m_Preferred = {static_cast<float>(solved[2]), static_cast<float>(solved[3])};
    // Arbitrary anchors may overlap or extend beyond the container. Its own hard
    // minimum is explicit; child hard bounds are enforced and overflow is reported.
    plan.m_Measure.m_HeightForWidth = true;
    for (std::size_t i = 0; i < count; ++i)
    {
        const auto base = (i + 1) * 4;
        const auto& child = context.m_Children[i];
        Rect bounds{static_cast<float>(solved[base]), static_cast<float>(solved[base + 1]),
                    std::max(0.0f, static_cast<float>(solved[base + 2])),
                    std::max(0.0f, static_cast<float>(solved[base + 3]))};
        const bool definiteWidth = context.m_DefiniteWidth || child.m_Width.m_Unit == Length::Unit::Pixels;
        const bool definiteHeight = context.m_DefiniteHeight || child.m_Height.m_Unit == Length::Unit::Pixels;
        if (measures[i].m_Baseline)
            measures[i] = MeasureChild(context, i, {bounds.m_Width, bounds.m_Height}, definiteWidth, definiteHeight);
        plan.m_Slots.push_back({i, bounds, measures[i], definiteWidth, definiteHeight});
        if (!context.m_Constraints.m_Width)
            plan.m_Measure.m_Preferred.m_X =
                std::max(plan.m_Measure.m_Preferred.m_X, bounds.m_X + bounds.m_Width + child.m_Margin.m_Right);
        if (!context.m_Constraints.m_Height)
            plan.m_Measure.m_Preferred.m_Y =
                std::max(plan.m_Measure.m_Preferred.m_Y, bounds.m_Y + bounds.m_Height + child.m_Margin.m_Bottom);
        if (!plan.m_Measure.m_Baseline && measures[i].m_Baseline)
            plan.m_Measure.m_Baseline = bounds.m_Y + *measures[i].m_Baseline;
    }
    PercentProblems(context, plan);
    BoundsProblems(context, plan);
    return plan;
}
} // namespace EStudioUI
