#pragma once

#include "Types.h"

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace EStudioUI
{
//! Owns exact binary bytes without formatting them as text.
using ModelBlob = std::vector<std::uint8_t>;

//! Owns a typed cell value without UI, font, or graphics resources.
using ModelValue = std::variant<std::monostate, bool, std::int64_t, double, std::string, Color, ModelBlob>;

//! Names the raw value alternatives accepted by one column.
enum class ModelValueType
{

    //! Accepts any owned value alternative.
    Any,

    //! Stores a logical true/false value.
    Boolean,

    //! Stores an exact signed 64-bit integer.
    Integer,

    //! Stores a finite double-precision value.
    Real,

    //! Stores owned UTF-8 text.
    Text,

    //! Stores finite RGBA components.
    Color,

    //! Stores exact binary bytes.
    Blob
};

//! Identifies semantic data independently of CSS presentation.
enum class ModelRole
{
    Display,
    Edit,
    Foreground,
    Background,
    ToolTip,
    Enabled,
    Checked
};

//! Describes one model field and its pure formatting, parsing, and validation policies.
struct ModelColumn
{

    //! Human-readable UTF-8 field name.
    std::string m_Title;

    //! Raw type accepted by the default editor and final-value validation.
    ModelValueType m_ValueType = ModelValueType::Text;

    //! Allows edits through the model's immutable storage adapter.
    bool m_Editable = true;

    //! Optional logical-pixel width hint, excluding view padding and borders.
    std::optional<float> m_WidthHint;

    //! Optional pure conversion from a raw value to UTF-8 display text.
    std::function<std::string(const ModelValue&)> m_Format;

    //! Optional pure conversion from committed editor text to a raw value.
    std::function<ModelValue(std::string_view)> m_Parse;

    //! Rejects a candidate by throwing before model publication.
    std::function<void(const ModelValue&)> m_Validate;

    //! Allows a null value independently of the declared non-null type.
    bool m_Nullable = false;

    //! Opts into a built-in typed editor; Any follows the actual cell value.
    std::optional<ModelValueType> m_EditorType;

    //! Optional owned choices for a combo editor; empty leaves the declared editor type.
    std::vector<ModelValue> m_Choices;
};

//! Encodes exact bytes as hexadecimal for bounded application editors.
std::string EncodeModelBlob(const ModelBlob& value);

//! Parses exact hexadecimal, accepting an optional 0x prefix.
ModelBlob DecodeModelBlob(std::string_view text);

//! Formats an owned value without locale-dependent numeric conversion.
std::string FormatModelValue(const ModelValue& value);

//! Parses an exact, finite typed editor value and applies the column validator.
ModelValue ParseModelValue(const ModelColumn& column, std::string_view text);

//! Validates the declared type, finite values, and the column's custom policy.
void ValidateModelValue(const ModelColumn& column, const ModelValue& value);

//! Orders raw numeric values numerically and other types deterministically.
int CompareModelValues(const ModelValue& left, const ModelValue& right);
} // namespace EStudioUI
