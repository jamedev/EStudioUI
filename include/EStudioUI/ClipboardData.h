#pragma once

#include "PlatformTypes.h"

#include <filesystem>
#include <map>
#include <optional>
#include <string_view>

namespace EStudioUI
{
//! Owns transferable clipboard formats without borrowing native storage.
struct ClipboardData
{
    //! Optional UTF-8 text; service reads and writes use canonical LF line breaks.
    std::optional<std::string> m_Text{};

    //! Optional UTF-8 HTML fragment.
    std::optional<std::string> m_Html{};

    //! Copied file or folder paths; an empty list omits this format.
    std::vector<std::filesystem::path> m_Files{};

    //! Application MIME formats and their owned byte payloads.
    std::map<std::string, std::vector<std::uint8_t>> m_Custom{};

    //! Maximum aggregate payload bytes accepted by this boundary.
    static constexpr std::size_t s_MaxBytes = 8 * 1024 * 1024;

    //! Rejects malformed UTF-8, embedded NULs, invalid formats, or excessive payloads.
    void Validate() const;

    //! Returns whether the value contains no formats.
    bool IsEmpty() const;
};

//! Owns clipboard data together with its native read outcome.
struct ClipboardResult
{
    //! Native-service status and diagnostic.
    PlatformResult m_Result{};

    //! Independently owned clipboard formats on success.
    ClipboardData m_Data{};
};

//! Rejects invalid UTF-8 or NUL-containing native-service strings without sanitizing them.
void ValidatePlatformText(std::string_view text);
} // namespace EStudioUI
