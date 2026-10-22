#pragma once

#include <exception>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <stop_token>
#include <string>
#include <vector>

namespace EStudioUI
{
class PlatformServices;

//! The native picker operation requested by an application.
enum class FileDialogKind
{
    OpenFile,
    SaveFile,
    SelectFolder
};

//! One owned native filename filter.
struct FileDialogFilter
{
    //! UTF-8 caption, such as Images.
    std::string m_Name{};

    //! Semicolon-separated shell patterns, such as *.png;*.jpg.
    std::string m_Pattern{};
};

//! Options copied before opening an asynchronous native picker.
struct FileDialogOptions
{
    //! Picker operation.
    FileDialogKind m_Kind = FileDialogKind::OpenFile;

    //! UTF-8 window caption; an empty caption uses the native default.
    std::string m_Title{};

    //! Optional initial folder.
    std::filesystem::path m_InitialFolder{};

    //! Initial filename for a save request.
    std::filesystem::path m_FileName{};

    //! Native filters; folder requests omit them.
    std::vector<FileDialogFilter> m_Filters{};

    //! Optional save extension without a leading dot.
    std::string m_DefaultExtension{};

    //! Allows several selections for an OpenFile request.
    bool m_Multiple = false;

    //! Rejects invalid combinations, malformed text, and excessive filters.
    void Validate() const;
};

//! Terminal outcomes of a native or injected picker.
enum class FileDialogStatus
{
    Accepted,
    Cancelled,
    Unsupported,
    Busy,
    OwnerClosed,
    Failed
};

//! Owns a completed picker outcome independently of the picker and its owner.
struct FileDialogResult
{
    //! Terminal result.
    FileDialogStatus m_Status = FileDialogStatus::Cancelled;

    //! Selected filesystem paths on acceptance.
    std::vector<std::filesystem::path> m_Paths{};

    //! Native failure diagnostic.
    std::string m_Message{};

    //! OS error code when available.
    std::int64_t m_NativeCode = 0;
};

//! Copyable owner-side picker result with no nested framework event loop.
class FileDialogSession
{
  public:
    //! Creates an empty result handle.
    FileDialogSession();

    //! Returns whether this valid picker still awaits owner-side completion.
    bool IsPending() const;

    //! Returns the terminal result, or nullopt for a pending/empty handle.
    std::optional<FileDialogResult> GetResult() const;

    //! Delivers one owner-side completion; completed sessions call late observers immediately.
    void OnFinished(std::function<void(const FileDialogResult&)> callback) const;

    //! Requests native cancellation and completes a pending session once.
    bool Cancel() const;

    //! Returns the first contained completion-observer exception.
    std::exception_ptr GetCallbackError() const;

  private:
    friend class PlatformServices;

    //! Owns completion and cancellation without retaining a native window.
    struct Data;

    //! Creates one pending owner-side session.
    static FileDialogSession Create();

    //! Commits one validated terminal outcome and safely delivers observers.
    void Complete(FileDialogResult result) const;

    //! Returns the native worker's cooperative cancellation token.
    std::stop_token GetStopToken() const;

    //! Shares result state independently of the platform adapter.
    std::shared_ptr<Data> m_Data;
};
} // namespace EStudioUI
