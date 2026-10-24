#include <EStudioUI/DocumentSnapshot.h>
#include <EStudioUI/TextBuffer.h>
#include <utf8proc.h>

#include <algorithm>
#include <atomic>
#include <limits>
#include <stdexcept>
#include <utility>

namespace EStudioUI
{
namespace Detail
{
struct ParagraphNode
{
    std::shared_ptr<const DocumentParagraph> m_Paragraph;
    std::shared_ptr<const ParagraphNode> m_Left, m_Right;
    std::size_t m_Count = 1, m_TextBytes = 0, m_PayloadBytes = 0;
    int m_Height = 1;
};

struct TextDocumentData
{
    std::shared_ptr<const ParagraphNode> m_Root;
    std::uint64_t m_Identity = 0, m_Revision = 0, m_ParentRevision = 0;
    DocumentChange m_Edit;
};
} // namespace Detail

namespace
{
using Node = Detail::ParagraphNode;
using Tree = std::shared_ptr<const Node>;
using Paragraph = std::shared_ptr<const DocumentParagraph>;
std::atomic<std::uint64_t> s_NextIdentity{1};

std::uint64_t Identity()
{
    auto value = s_NextIdentity.load(std::memory_order_relaxed);
    for (;;)
    {
        if (value == std::numeric_limits<std::uint64_t>::max())
        {
            throw std::overflow_error("Text document identity space is exhausted.");
        }
        if (s_NextIdentity.compare_exchange_weak(value, value + 1, std::memory_order_relaxed))
        {
            return value;
        }
    }
}

std::size_t Count(const Tree& tree)
{
    return tree ? tree->m_Count : 0;
}

std::size_t Bytes(const Tree& tree)
{
    return tree ? tree->m_TextBytes : 0;
}

std::size_t Payload(const Tree& tree)
{
    return tree ? tree->m_PayloadBytes : 0;
}

int Height(const Tree& tree)
{
    return tree ? tree->m_Height : 0;
}

std::size_t ParagraphBytes(const DocumentParagraph& paragraph)
{
    return sizeof(DocumentParagraph) + 64 + paragraph.m_Text.capacity() + 1 +
           paragraph.m_Boundaries.capacity() * sizeof(std::size_t);
}

Tree Make(Paragraph paragraph, Tree left, Tree right, std::size_t& allocated)
{
    auto node = std::make_shared<Node>();
    node->m_Paragraph = std::move(paragraph);
    node->m_Left = std::move(left);
    node->m_Right = std::move(right);
    node->m_Count = 1 + Count(node->m_Left) + Count(node->m_Right);
    node->m_TextBytes = node->m_Paragraph->m_Text.size() + Bytes(node->m_Left) + Bytes(node->m_Right);
    node->m_PayloadBytes = ParagraphBytes(*node->m_Paragraph) + Payload(node->m_Left) + Payload(node->m_Right);
    node->m_Height = 1 + std::max(Height(node->m_Left), Height(node->m_Right));
    allocated += sizeof(Node) + 64;
    return node;
}

Tree Balance(Paragraph paragraph, Tree left, Tree right, std::size_t& allocated)
{
    if (Height(left) > Height(right) + 1)
    {
        if (Height(left->m_Left) >= Height(left->m_Right))
        {
            return Make(left->m_Paragraph, left->m_Left,
                        Make(std::move(paragraph), left->m_Right, std::move(right), allocated), allocated);
        }
        const auto pivot = left->m_Right;
        return Make(pivot->m_Paragraph, Make(left->m_Paragraph, left->m_Left, pivot->m_Left, allocated),
                    Make(std::move(paragraph), pivot->m_Right, std::move(right), allocated), allocated);
    }
    if (Height(right) > Height(left) + 1)
    {
        if (Height(right->m_Right) >= Height(right->m_Left))
        {
            return Make(right->m_Paragraph, Make(std::move(paragraph), std::move(left), right->m_Left, allocated),
                        right->m_Right, allocated);
        }
        const auto pivot = right->m_Left;
        return Make(pivot->m_Paragraph, Make(std::move(paragraph), std::move(left), pivot->m_Left, allocated),
                    Make(right->m_Paragraph, pivot->m_Right, right->m_Right, allocated), allocated);
    }
    return Make(std::move(paragraph), std::move(left), std::move(right), allocated);
}

Tree Join(Tree left, Paragraph middle, Tree right, std::size_t& allocated)
{
    if (Height(left) > Height(right) + 1)
    {
        return Balance(left->m_Paragraph, left->m_Left,
                       Join(left->m_Right, std::move(middle), std::move(right), allocated), allocated);
    }
    if (Height(right) > Height(left) + 1)
    {
        return Balance(right->m_Paragraph, Join(std::move(left), std::move(middle), right->m_Left, allocated),
                       right->m_Right, allocated);
    }
    return Make(std::move(middle), std::move(left), std::move(right), allocated);
}

std::pair<Tree, Tree> Split(Tree tree, std::size_t count, std::size_t& allocated)
{
    if (!tree)
    {
        return {};
    }
    if (!count)
    {
        return {{}, std::move(tree)};
    }
    if (count == Count(tree))
    {
        return {std::move(tree), {}};
    }
    const auto leftCount = Count(tree->m_Left);
    if (count <= leftCount)
    {
        auto [first, second] = Split(tree->m_Left, count, allocated);
        return {std::move(first), Join(std::move(second), tree->m_Paragraph, tree->m_Right, allocated)};
    }
    auto [first, second] = Split(tree->m_Right, count - leftCount - 1, allocated);
    return {Join(tree->m_Left, tree->m_Paragraph, std::move(first), allocated), std::move(second)};
}

Tree Concat(Tree left, Tree right, std::size_t& allocated)
{
    if (!left)
    {
        return right;
    }
    if (!right)
    {
        return left;
    }
    auto current = right;
    while (current->m_Left)
    {
        current = current->m_Left;
    }
    auto [head, tail] = Split(right, 1, allocated);
    (void)head;
    return Join(std::move(left), current->m_Paragraph, std::move(tail), allocated);
}

Tree Build(const std::vector<Paragraph>& paragraphs, std::size_t first, std::size_t last, std::size_t& allocated)
{
    if (first == last)
    {
        return {};
    }
    const auto middle = first + (last - first) / 2;
    auto left = Build(paragraphs, first, middle, allocated);
    auto right = Build(paragraphs, middle + 1, last, allocated);
    return Make(paragraphs[middle], std::move(left), std::move(right), allocated);
}

Paragraph At(Tree tree, std::size_t index)
{
    while (tree)
    {
        const auto left = Count(tree->m_Left);
        if (index == left)
        {
            return tree->m_Paragraph;
        }
        if (index < left)
        {
            tree = tree->m_Left;
        }
        else
        {
            index -= left + 1;
            tree = tree->m_Right;
        }
    }
    throw std::out_of_range("Document paragraph index is outside this snapshot.");
}

template <class Callback> void Visit(const Tree& tree, Callback&& callback)
{
    if (!tree)
    {
        return;
    }
    Visit(tree->m_Left, callback);
    callback(tree->m_Paragraph);
    Visit(tree->m_Right, callback);
}

void ValidateLimits(DocumentLimits limits)
{
    if (!limits.m_MaxBytes || limits.m_MaxBytes > 1024ull * 1024 * 1024 || !limits.m_MaxParagraphBytes ||
        limits.m_MaxParagraphBytes > 1024 * 1024 || !limits.m_MaxParagraphs || limits.m_MaxParagraphs > 1000000)
    {
        throw std::invalid_argument(
            "Document limits require 1..1 GiB total bytes, 1..1 MiB paragraph bytes and 1..1,000,000 paragraphs.");
    }
}

std::vector<std::string> Parse(std::string_view text, DocumentLimits limits)
{
    // Validate scalars before segmentation; source bytes are never sanitized or
    // normalized to NFC. File-format representability belongs to an adapter.
    std::vector<std::string> paragraphs(1);
    std::size_t bytes = 0;
    for (std::size_t offset = 0; offset < text.size();)
    {
        utf8proc_int32_t scalar = 0;
        const auto count =
            utf8proc_iterate(reinterpret_cast<const utf8proc_uint8_t*>(text.data() + offset),
                             static_cast<utf8proc_ssize_t>(std::min<std::size_t>(4, text.size() - offset)), &scalar);
        if (count <= 0)
        {
            throw std::invalid_argument("Document text requires well-formed UTF-8 scalar values.");
        }
        if (scalar == '\r' || scalar == '\n')
        {
            offset += static_cast<std::size_t>(count);
            if (scalar == '\r' && offset < text.size() && text[offset] == '\n')
            {
                ++offset;
            }
            if (paragraphs.size() == limits.m_MaxParagraphs || bytes == limits.m_MaxBytes)
            {
                throw std::length_error("Document paragraph count or total byte limit exceeded.");
            }
            ++bytes;
            paragraphs.emplace_back();
        }
        else
        {
            const auto size = static_cast<std::size_t>(count);
            if (size > limits.m_MaxBytes - bytes || size > limits.m_MaxParagraphBytes - paragraphs.back().size())
            {
                throw std::length_error("Document total or paragraph UTF-8 byte limit exceeded.");
            }
            paragraphs.back().append(text.data() + offset, size);
            bytes += size;
            offset += size;
        }
    }
    return paragraphs;
}

Paragraph MakeParagraph(std::string text, std::uint64_t identity, std::size_t& allocated)
{
    auto paragraph = std::make_shared<DocumentParagraph>();
    paragraph->m_Id = identity ? identity : Identity();
    paragraph->m_Text = std::move(text);
    paragraph->m_Boundaries = TextBuffer::Graphemes(paragraph->m_Text);
    allocated += ParagraphBytes(*paragraph);
    return paragraph;
}

void ValidatePosition(const DocumentSnapshot& snapshot, DocumentPosition position)
{
    if (position.m_Document != snapshot.GetDocumentId() || position.m_Revision != snapshot.GetRevision())
    {
        throw std::invalid_argument("Document position belongs to another story or immutable revision.");
    }
    const auto paragraph = snapshot.GetParagraph(position.m_ParagraphIndex);
    if (position.m_ParagraphId != paragraph->m_Id ||
        !std::binary_search(paragraph->m_Boundaries.begin(), paragraph->m_Boundaries.end(), position.m_ByteOffset))
    {
        throw std::invalid_argument(
            "Document position requires the current paragraph identity and a grapheme boundary.");
    }
}
} // namespace

bool DocumentSnapshot::IsValid() const noexcept
{
    return bool(m_Data);
}

std::uint64_t DocumentSnapshot::GetDocumentId() const noexcept
{
    return m_Data ? m_Data->m_Identity : 0;
}

std::uint64_t DocumentSnapshot::GetRevision() const noexcept
{
    return m_Data ? m_Data->m_Revision : 0;
}

std::size_t DocumentSnapshot::GetParagraphCount() const noexcept
{
    return m_Data ? Count(m_Data->m_Root) : 0;
}

std::size_t DocumentSnapshot::GetByteCount() const noexcept
{
    return m_Data ? Bytes(m_Data->m_Root) + Count(m_Data->m_Root) - 1 : 0;
}

std::shared_ptr<const DocumentParagraph> DocumentSnapshot::GetParagraph(std::size_t index) const
{
    if (!m_Data)
    {
        throw std::logic_error("An invalid document snapshot has no paragraphs.");
    }
    return At(m_Data->m_Root, index);
}

std::optional<std::size_t> DocumentSnapshot::FindParagraph(std::uint64_t paragraphId) const
{
    std::optional<std::size_t> result;
    std::size_t index = 0;
    if (m_Data)
    {
        Visit(m_Data->m_Root,
              [&](const Paragraph& paragraph)
              {
                  if (paragraph->m_Id == paragraphId)
                  {
                      result = index;
                  }
                  ++index;
              });
    }
    return result;
}

DocumentPosition DocumentSnapshot::GetPosition(std::size_t paragraphIndex, std::size_t byteOffset) const
{
    const auto paragraph = GetParagraph(paragraphIndex);
    if (!std::binary_search(paragraph->m_Boundaries.begin(), paragraph->m_Boundaries.end(), byteOffset))
    {
        throw std::invalid_argument("Document positions must be paragraph-local UTF-8 grapheme boundaries.");
    }
    return {GetDocumentId(), GetRevision(), paragraph->m_Id, paragraphIndex, byteOffset};
}

std::string DocumentSnapshot::GetText() const
{
    std::string result;
    result.reserve(GetByteCount());
    bool first = true;
    if (m_Data)
    {
        Visit(m_Data->m_Root,
              [&](const Paragraph& paragraph)
              {
                  if (!first)
                  {
                      result.push_back('\n');
                  }
                  first = false;
                  result.append(paragraph->m_Text);
              });
    }
    return result;
}

DocumentChange DocumentSnapshot::GetChangeFrom(const DocumentSnapshot& before) const
{
    DocumentChange result;
    result.m_BeforeRevision = before.GetRevision();
    result.m_AfterRevision = GetRevision();
    if (*this == before)
    {
        return result;
    }
    if (m_Data && before.m_Data && GetDocumentId() == before.GetDocumentId())
    {
        if (m_Data->m_ParentRevision == before.GetRevision())
        {
            result = m_Data->m_Edit;
            return result;
        }
        if (before.m_Data->m_ParentRevision == GetRevision())
        {
            result = before.m_Data->m_Edit;
            std::swap(result.m_BeforeRevision, result.m_AfterRevision);
            std::swap(result.m_RemovedParagraphs, result.m_InsertedParagraphs);
            return result;
        }
    }
    result.m_RemovedParagraphs = before.GetParagraphCount();
    result.m_InsertedParagraphs = GetParagraphCount();
    result.m_FullReset = true;
    return result;
}

bool DocumentSnapshot::operator==(const DocumentSnapshot& other) const noexcept
{
    return m_Data == other.m_Data;
}

DocumentSnapshot DocumentSnapshot::Create(std::uint64_t identity, std::string_view text, DocumentLimits limits)
{
    ValidateLimits(limits);
    auto parts = Parse(text, limits);
    std::vector<Paragraph> paragraphs;
    paragraphs.reserve(parts.size());
    std::size_t allocated = 0;
    for (auto& part : parts)
    {
        paragraphs.push_back(MakeParagraph(std::move(part), 0, allocated));
    }
    auto data = std::make_shared<Detail::TextDocumentData>();
    data->m_Identity = identity ? identity : Identity();
    data->m_Revision = Identity();
    data->m_Root = Build(paragraphs, 0, paragraphs.size(), allocated);
    DocumentSnapshot result;
    result.m_Data = std::move(data);
    return result;
}

DocumentSnapshot DocumentSnapshot::Replace(DocumentPosition first, DocumentPosition last, std::string_view text,
                                           DocumentLimits limits, std::size_t& historyBytes) const
{
    ValidatePosition(*this, first);
    ValidatePosition(*this, last);
    if (first.m_ParagraphIndex > last.m_ParagraphIndex ||
        (first.m_ParagraphIndex == last.m_ParagraphIndex && first.m_ByteOffset > last.m_ByteOffset))
    {
        throw std::invalid_argument("Document replacement endpoints must be ordered.");
    }
    auto parts = Parse(text, limits);
    const auto oldFirst = GetParagraph(first.m_ParagraphIndex), oldLast = GetParagraph(last.m_ParagraphIndex);
    const auto removedCount = last.m_ParagraphIndex - first.m_ParagraphIndex + 1;
    if (parts.size() == removedCount)
    {
        bool equal = true;
        for (std::size_t i = 0; i < removedCount; ++i)
        {
            const auto paragraph = GetParagraph(first.m_ParagraphIndex + i);
            const auto begin = i ? 0 : first.m_ByteOffset;
            const auto end = i + 1 == removedCount ? last.m_ByteOffset : paragraph->m_Text.size();
            if (std::string_view(paragraph->m_Text).substr(begin, end - begin) != parts[i])
            {
                equal = false;
                break;
            }
        }
        if (equal)
        {
            historyBytes = 0;
            return *this;
        }
    }
    if (parts.size() > limits.m_MaxParagraphs - (GetParagraphCount() - removedCount))
    {
        throw std::length_error("Document paragraph count limit exceeded.");
    }
    parts.front().insert(0, oldFirst->m_Text, 0, first.m_ByteOffset);
    parts.back().append(oldLast->m_Text, last.m_ByteOffset, std::string::npos);
    for (const auto& part : parts)
    {
        if (part.size() > limits.m_MaxParagraphBytes)
        {
            throw std::length_error("Edited paragraph UTF-8 byte limit exceeded.");
        }
    }
    std::size_t allocated = 0;
    auto [prefix, rest] = Split(m_Data->m_Root, first.m_ParagraphIndex, allocated);
    auto [removed, suffix] = Split(std::move(rest), removedCount, allocated);
    std::size_t addedBytes = parts.size() - 1;
    for (const auto& part : parts)
    {
        addedBytes += part.size();
    }
    const auto removedBytes = Bytes(removed) + removedCount - 1;
    if (addedBytes > limits.m_MaxBytes - (GetByteCount() - removedBytes))
    {
        throw std::length_error("Edited document UTF-8 byte limit exceeded.");
    }
    std::vector<Paragraph> paragraphs;
    paragraphs.reserve(parts.size());
    for (std::size_t i = 0; i < parts.size(); ++i)
    {
        paragraphs.push_back(MakeParagraph(std::move(parts[i]), i ? 0 : oldFirst->m_Id, allocated));
    }
    auto middle = Build(paragraphs, 0, paragraphs.size(), allocated);
    auto root = Concat(Concat(std::move(prefix), std::move(middle), allocated), std::move(suffix), allocated);
    auto data = std::make_shared<Detail::TextDocumentData>();
    data->m_Identity = GetDocumentId();
    data->m_Revision = Identity();
    data->m_ParentRevision = GetRevision();
    data->m_Root = std::move(root);
    data->m_Edit = {GetRevision(), data->m_Revision, first.m_ParagraphIndex, removedCount, paragraphs.size(), false};
    // Charge new allocations, removed payloads and both retained tree-path sets.
    // Shared untouched paragraph payloads are not charged per local edit. This is
    // conservative estimated storage, not allocator-resident/process memory.
    historyBytes = allocated * 2 + Payload(removed) + Count(removed) * (sizeof(Node) + 64) +
                   2 * (sizeof(Detail::TextDocumentData) + 64);
    DocumentSnapshot result;
    result.m_Data = std::move(data);
    return result;
}
} // namespace EStudioUI
