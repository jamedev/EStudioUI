#pragma once

#include <cstdint>
#include <limits>
#include <string_view>

namespace EStudioUI
{
//! Describes complete input, an editable prefix, or a rejected representation.
enum class ValidationState
{
    //! Cannot be accepted as an edit.
    Invalid,
    //! Remains editable but cannot be submitted.
    Intermediate,
    //! Is complete and satisfies the validation policy.
    Acceptable
};

//! An immutable, GUI-thread input policy shared by one or more editors.
class InputValidator
{
  public:
    //! Releases a concrete validation policy.
    virtual ~InputValidator() = default;

    //! Classifies the complete candidate text without modifying it.
    virtual ValidationState Validate(std::string_view text) const = 0;
};

//! Accepts signed decimal integers within an inclusive range.
class IntegerValidator : public InputValidator
{
  public:
    //! Creates a bounded integer policy; inverted ranges throw.
    explicit IntegerValidator(std::int64_t minimum = std::numeric_limits<std::int64_t>::min(),
                              std::int64_t maximum = std::numeric_limits<std::int64_t>::max());

    //! Accepts valid in-range values; empty/sign-only and out-of-range numbers are intermediate.
    ValidationState Validate(std::string_view text) const override;

  private:
    //! Inclusive lower bound.
    std::int64_t m_Minimum;

    //! Inclusive upper bound.
    std::int64_t m_Maximum;
};

//! Accepts finite decimal numbers, including scientific notation, using a dot separator.
class DoubleValidator : public InputValidator
{
  public:
    //! Creates a finite inclusive range; invalid bounds throw.
    explicit DoubleValidator(double minimum = std::numeric_limits<double>::lowest(),
                             double maximum = std::numeric_limits<double>::max());

    //! Allows editable prefixes and out-of-range finite numbers but rejects malformed/nonfinite input.
    ValidationState Validate(std::string_view text) const override;

    //! Parses a complete finite ASCII number without whitespace or trailing characters.
    static bool TryParse(std::string_view text, double& value);

  private:
    //! Inclusive lower bound.
    double m_Minimum;

    //! Inclusive upper bound.
    double m_Maximum;
};
} // namespace EStudioUI
