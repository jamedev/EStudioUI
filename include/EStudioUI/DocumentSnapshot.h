#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace EStudioUI
{
class TextDocument;

namespace Detail
{
struct TextDocumentData;
}

//! Admission limits for one plain-text story, independent of widgets and file formats.
struct DocumentLimits
{
    //! Maximum UTF-8 bytes including canonical LF paragraph separators.
    std::size_t m_MaxBytes = 64 * 1024 * 1024;
    //! Maximum bytes in a single paragraph; paragraph edits rebuild its local index.
    std::size_t m_MaxParagraphBytes = 1024 * 1024;
    //! Maximum logical paragraphs; a story always contains at least one paragraph.
    std::size_t m_MaxParagraphs = 1000000;
};

//! Immutable paragraph payload shared by snapshots and retained history.
struct DocumentParagraph
{
    //! Lifetime-unique identity, preserved for the first paragraph of a replacement.
    std::uint64_t m_Id = 0;
    //! Exact valid UTF-8 text, excluding paragraph separators.
    std::string m_Text;
    //! Extended-grapheme UTF-8 byte boundaries, including zero and text length.
    std::vector<std::size_t> m_Boundaries;
};

//! A validated coordinate in one exact immutable story revision.
struct DocumentPosition
{
    //! Lifetime-unique story identity.
    std::uint64_t m_Document = 0;
    //! Content-root token; undo may restore this exact historical token.
    std::uint64_t m_Revision = 0;
    //! Stable paragraph identity used to validate the indexed coordinate.
    std::uint64_t m_ParagraphId = 0;
    //! Zero-based paragraph index in the identified snapshot.
    std::size_t m_ParagraphIndex = 0;
    //! UTF-8 byte offset on a paragraph-local grapheme boundary.
    std::size_t m_ByteOffset = 0;
    //! Compares exact snapshot coordinates.
    bool operator==(const DocumentPosition&) const = default;
};

//! Conservative paragraph invalidation between two immutable story values.
struct DocumentChange
{
    //! Content tokens before and after the change, distinct from StateStore revisions.
    std::uint64_t m_BeforeRevision = 0, m_AfterRevision = 0;
    //! First affected paragraph in both values.
    std::size_t m_FirstParagraph = 0;
    //! Replaced paragraph counts in the old and new values.
    std::size_t m_RemovedParagraphs = 0, m_InsertedParagraphs = 0;
    //! Reports conservative whole-story invalidation for reset or unrelated roots.
    bool m_FullReset = false;
};

//! Cheap immutable paragraph-tree snapshot, safe for retained and worker-thread reads.
class DocumentSnapshot
{
  public:
    //! Creates an invalid empty handle; TextDocument always publishes a valid story.
    DocumentSnapshot() = default;
    //! Reports whether this handle owns a story root.
    bool IsValid() const noexcept;
    //! Returns the lifetime-unique story identity, or zero for an invalid handle.
    std::uint64_t GetDocumentId() const noexcept;
    //! Returns a content-root token, restored by undo rather than monotonically committed.
    std::uint64_t GetRevision() const noexcept;
    //! Returns the number of logical paragraphs in constant time.
    std::size_t GetParagraphCount() const noexcept;
    //! Returns serialized UTF-8 bytes including canonical paragraph separators.
    std::size_t GetByteCount() const noexcept;
    //! Returns an owned paragraph payload in logarithmic tree traversal time.
    std::shared_ptr<const DocumentParagraph> GetParagraph(std::size_t index) const;
    //! Finds a stable identity by an explicit linear story scan; ordinary edits use indexed positions.
    std::optional<std::size_t> FindParagraph(std::uint64_t paragraphId) const;
    //! Validates a paragraph-local grapheme boundary and captures an exact snapshot coordinate.
    DocumentPosition GetPosition(std::size_t paragraphIndex, std::size_t byteOffset = 0) const;
    //! Materializes the whole story with LF paragraph separators; this is an explicit linear copy.
    std::string GetText() const;
    //! Returns narrow direct edit/undo invalidation or a conservative whole-story fallback.
    DocumentChange GetChangeFrom(const DocumentSnapshot& before) const;
    //! Compares immutable content-root identity without scanning paragraphs.
    bool operator==(const DocumentSnapshot& other) const noexcept;

  private:
    friend class TextDocument;
    //! Builds a validated story; zero identity allocates a new lifetime-unique story.
    static DocumentSnapshot Create(std::uint64_t identity, std::string_view text, DocumentLimits limits);
    //! Prepares an immutable splice and conservative changed-storage history accounting.
    DocumentSnapshot Replace(DocumentPosition first, DocumentPosition last, std::string_view text,
                             DocumentLimits limits, std::size_t& historyBytes) const;
    //! Owns immutable tree and edit provenance; copies retain only this pointer.
    std::shared_ptr<const Detail::TextDocumentData> m_Data;
};
} // namespace EStudioUI
