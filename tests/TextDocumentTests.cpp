#include <EStudioUI/TextDocument.h>

#include <algorithm>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace EStudioUI;

namespace
{
int s_Checks = 0;

void Check(bool value, const char* message)
{
    ++s_Checks;
    if (!value)
    {
        throw std::runtime_error(message);
    }
}

template <typename Callback> void Throws(Callback callback, const char* message)
{
    bool rejected = false;
    try
    {
        callback();
    }
    catch (const std::exception&)
    {
        rejected = true;
    }
    Check(rejected, message);
}

DocumentPosition End(const DocumentSnapshot& snapshot)
{
    const auto index = snapshot.GetParagraphCount() - 1;
    return snapshot.GetPosition(index, snapshot.GetParagraph(index)->m_Text.size());
}

DocumentPosition PositionAt(const DocumentSnapshot& snapshot, const std::string& text, std::size_t offset)
{
    std::size_t paragraph = 0, first = 0;
    for (std::size_t i = 0; i < offset; ++i)
    {
        if (text[i] == '\n')
        {
            ++paragraph;
            first = i + 1;
        }
    }
    return snapshot.GetPosition(paragraph, offset - first);
}

void TextAndPositions()
{
    UiDispatcher dispatcher;
    StateStore store(dispatcher);
    TextDocument document(store);
    auto snapshot = document.GetSnapshot();
    Check(snapshot.IsValid() && snapshot.GetDocumentId() != 0 && snapshot.GetRevision() != 0 &&
              snapshot.GetParagraphCount() == 1 && snapshot.GetText().empty() && snapshot.GetByteCount() == 0,
          "An empty document owns one addressable paragraph without initializing a UI");
    DocumentSnapshot invalid;
    Check(!invalid.IsValid() && invalid.GetDocumentId() == 0 && invalid.GetRevision() == 0,
          "A default snapshot is an explicit invalid handle");
    document.Reset("a\r\nb\rc\n");
    snapshot = document.GetSnapshot();
    Check(snapshot.GetText() == "a\nb\nc\n" && snapshot.GetParagraphCount() == 4 && snapshot.GetByteCount() == 6,
          "Plain-text loading normalizes CRLF and CR while preserving the trailing paragraph");
    Check(snapshot.GetParagraph(3)->m_Text.empty() && !document.GetHistory().GetState().m_Modified,
          "The final empty paragraph is canonical and loading starts clean");
    const std::string exact = "  a\tb  e\u0301 🙂\u2028尾  ";
    document.Reset(exact);
    snapshot = document.GetSnapshot();
    Check(snapshot.GetText() == exact && snapshot.GetByteCount() == exact.size() && snapshot.GetParagraphCount() == 1,
          "Unicode, combining sequences, tabs, repeated spaces and hard breaks are preserved exactly");
    const std::string nulText("left\0right", 10);
    document.Reset(nulText);
    snapshot = document.GetSnapshot();
    Check(snapshot.GetText() == nulText && snapshot.GetByteCount() == 10 && snapshot.GetParagraphCount() == 1,
          "Valid Unicode NUL is retained; file-format representability belongs to adapters");
    document.Reset("ab");
    snapshot = document.GetSnapshot();
    document.Replace(snapshot.GetPosition(0, 1), snapshot.GetPosition(0, 1), "\u0301");
    snapshot = document.GetSnapshot();
    Check(snapshot.GetText() == "a\u0301b" &&
              snapshot.GetParagraph(0)->m_Boundaries == std::vector<std::size_t>({0, 3, 4}),
          "Insertion can merge adjacent code points into a grapheme without normalization");
    document.Reset("alpha\nbeta\n");
    const auto normalized = document.GetSnapshot();
    const auto normalizedHistory = document.GetHistory().GetState();
    Check(!document.SetText("alpha\r\nbeta\r\n") && document.GetSnapshot() == normalized &&
              document.GetHistory().GetState() == normalizedHistory,
          "An equivalent CRLF whole-story replacement preserves the exact root and history");
    document.Reset("e\u0301🙂");
    snapshot = document.GetSnapshot();
    Check(snapshot.GetParagraph(0)->m_Boundaries == std::vector<std::size_t>({0, 3, 7}),
          "Paragraph indexes use complete extended graphemes and UTF-8 byte coordinates");
    Throws([&] { snapshot.GetPosition(0, 1); }, "Editing cannot split a combining grapheme");
    Throws([&] { snapshot.GetPosition(0, 2); }, "Editing cannot split a UTF-8 code point");
    Throws([&] { snapshot.GetPosition(0, 4); }, "Editing cannot split a supplementary code point");
    Throws([&] { snapshot.GetPosition(0, 8); }, "An offset beyond the paragraph is rejected");
    Throws([&] { snapshot.GetPosition(1); }, "A paragraph beyond the snapshot is rejected");
    const auto before = snapshot;
    const auto history = document.GetHistory().GetState();
    for (const std::string& malformed :
         {std::string("\xff"), std::string("\xc0\xaf"), std::string("\xed\xa0\x80"), std::string("\xf0\x9f")})
    {
        Throws([&] { document.SetText(malformed); }, "Malformed UTF-8 is rejected instead of silently replaced");
    }
    Check(document.GetSnapshot() == before && document.GetHistory().GetState() == history,
          "Rejected Unicode never changes canonical content or history");
    TextDocument foreign(store);
    Throws([&] { document.Replace(foreign.GetSnapshot().GetPosition(0), End(before), "x"); },
           "Coordinates from another document cannot edit this story");
    const auto oldPosition = before.GetPosition(0);
    document.Replace(oldPosition, oldPosition, "x");
    const auto current = document.GetSnapshot();
    Throws([&] { document.Replace(oldPosition, oldPosition, "y"); },
           "An old offset is rejected even when its paragraph remains at the same ordinal");
    Throws([&] { document.Replace(current.GetPosition(0), current.GetPosition(0), "z", "Stale edit", before); },
           "Explicit stale expected snapshots are rejected before editing");
    Check(document.GetHistory().Undo() && document.GetSnapshot() == before,
          "Undo restores the exact immutable root and its content token");
    Check(document.Replace(oldPosition, oldPosition, "z"),
          "A coordinate becomes valid again when undo restores its exact root");
    snapshot = document.GetSnapshot();
    Throws([&] { document.Replace(End(snapshot), snapshot.GetPosition(0), "x"); },
           "A reversed range does not silently exchange endpoints");
    auto forged = snapshot.GetPosition(0);
    ++forged.m_ParagraphId;
    Throws([&] { document.Replace(forged, End(snapshot), "x"); },
           "The indexed coordinate validates its paragraph identity");
}

void StructuralEditsAndHistory()
{
    UiDispatcher dispatcher;
    StateStore store(dispatcher);
    TextDocument document(store);
    document.Reset("alpha\nbeta\ngamma");
    const auto original = document.GetSnapshot();
    const auto alpha = original.GetParagraph(0), beta = original.GetParagraph(1), gamma = original.GetParagraph(2);
    const auto split = original.GetPosition(0, 2);
    Check(document.Replace(split, split, "\n", "Split paragraph"), "A paragraph split records one real edit");
    auto snapshot = document.GetSnapshot();
    const auto splitId = snapshot.GetParagraph(1)->m_Id;
    Check(snapshot.GetText() == "al\npha\nbeta\ngamma" && snapshot.GetParagraph(0)->m_Id == alpha->m_Id &&
              snapshot.GetParagraph(2) == beta && snapshot.GetParagraph(3) == gamma,
          "Splitting preserves the first identity and shares untouched paragraph payloads");
    auto change = snapshot.GetChangeFrom(original);
    Check(!change.m_FullReset && change.m_FirstParagraph == 0 && change.m_RemovedParagraphs == 1 &&
              change.m_InsertedParagraphs == 2 && change.m_BeforeRevision == original.GetRevision() &&
              change.m_AfterRevision == snapshot.GetRevision(),
          "A direct split publishes the narrow paragraph invalidation and exact root tokens");
    const auto splitSnapshot = snapshot;
    Check(document.GetHistory().Undo() && document.GetSnapshot() == original,
          "Split undo restores the original value and identities");
    change = original.GetChangeFrom(splitSnapshot);
    Check(!change.m_FullReset && change.m_RemovedParagraphs == 2 && change.m_InsertedParagraphs == 1,
          "Undo exposes the inverse paragraph invalidation");
    Check(document.GetHistory().Redo() && document.GetSnapshot() == splitSnapshot,
          "Redo restores the same immutable split rather than allocating replacement identities");
    document.Replace(splitSnapshot.GetPosition(0, 2), splitSnapshot.GetPosition(1), "", "Join paragraphs");
    snapshot = document.GetSnapshot();
    Check(snapshot.GetText() == original.GetText() && snapshot.GetParagraph(0)->m_Id == alpha->m_Id &&
              snapshot.GetParagraph(1) == beta && !snapshot.FindParagraph(splitId),
          "Joining removes the second paragraph while preserving first and unrelated identities");
    document.Reset("alpha\nbeta\ngamma");
    snapshot = document.GetSnapshot();
    const auto firstId = snapshot.GetParagraph(0)->m_Id;
    const auto lastPayload = snapshot.GetParagraph(2);
    document.Replace(snapshot.GetPosition(0, 2), snapshot.GetPosition(1, 2), "X\r\nY", "Replace across paragraphs");
    snapshot = document.GetSnapshot();
    Check(snapshot.GetText() == "alX\nYta\ngamma" && snapshot.GetParagraph(0)->m_Id == firstId &&
              snapshot.GetParagraph(2) == lastPayload,
          "Cross-paragraph replacement retains exact prefixes/suffixes and unaffected payloads");
    auto point = snapshot.GetPosition(0);
    document.Replace(point, point, "\n", "First branch");
    const auto retiredId = document.GetSnapshot().GetParagraph(1)->m_Id;
    document.GetHistory().Undo();
    snapshot = document.GetSnapshot();
    point = snapshot.GetPosition(0);
    document.Replace(point, point, "\n", "New branch");
    Check(document.GetSnapshot().GetParagraph(1)->m_Id != retiredId && !document.GetHistory().Redo(),
          "A new edit branch cannot reuse a retired paragraph identity");
    const auto saved = document.GetHistory().CaptureCheckpoint();
    document.GetHistory().MarkSaved(saved);
    document.SetText("new value", "Whole story");
    document.GetHistory().MarkSaved(saved);
    Check(document.GetHistory().GetState().m_Modified,
          "Completing an older save does not mark a newer document value clean");
    Check(document.GetHistory().Undo() && !document.GetHistory().GetState().m_Modified,
          "Undo to a captured saved position restores clean state");
    snapshot = document.GetSnapshot();
    document.Replace(snapshot.GetPosition(0), End(snapshot), "");
    Check(document.GetSnapshot().GetParagraphCount() == 1 && document.GetSnapshot().GetText().empty(),
          "Deleting all content preserves one valid empty paragraph");
    int notifications = 0;
    document.Subscribe([&](const StateChange<DocumentSnapshot>&) { ++notifications; });
    const auto empty = document.GetSnapshot();
    const auto beforeNoOp = document.GetHistory().GetState();
    Check(!document.Replace(empty.GetPosition(0), empty.GetPosition(0), "") && document.GetSnapshot() == empty &&
              document.GetHistory().GetState() == beforeNoOp && notifications == 0,
          "An empty splice creates no root, history entry or notification");
    Check(!document.SetText(""), "Setting identical text is also a no-op");
}

void TransactionsAndNotifications()
{
    UiDispatcher dispatcher;
    StateStore store(dispatcher);
    TextDocument document(store);
    document.Reset("base");
    bool forbid = true;
    const auto validator = document.AddValidator(
        [&](const DocumentSnapshot& candidate)
        {
            if (forbid && candidate.GetText().find("forbidden") != std::string::npos)
            {
                throw std::invalid_argument("Rejected document candidate");
            }
        });
    int notifications = 0;
    document.Subscribe(
        [&](const StateChange<DocumentSnapshot>& change)
        {
            ++notifications;
            const auto timeline = document.GetHistory().GetState();
            Check(change.GetAfter() == document.GetSnapshot() && timeline.m_UndoCount + timeline.m_RedoCount > 0,
                  "Document observers see canonical content and coherent history");
        });
    const auto original = document.GetSnapshot();
    const auto initialHistory = document.GetHistory().GetState();
    Throws([&] { document.SetText("forbidden"); }, "A document validator vetoes ordinary replacement");
    Check(document.GetSnapshot() == original && document.GetHistory().GetState() == initialHistory &&
              notifications == 0,
          "A rejected candidate rolls back content, timeline and notification delivery");
    Throws([&] { document.Reset("forbidden"); }, "Load/reset also validates before history can be cleared");
    Check(document.GetSnapshot() == original && document.GetHistory().GetState() == initialHistory,
          "Rejected reset preserves both document and saved/history state");
    Throws(
        [&]
        {
            document.GetHistory().Group("Rejected group",
                                        [&]
                                        {
                                            document.SetText("staged");
                                            document.SetText("forbidden");
                                        });
        },
        "A grouped final-candidate veto discards the complete document command");
    Check(document.GetSnapshot() == original && document.GetHistory().GetState() == initialHistory &&
              notifications == 0,
          "A failed grouped edit does not leak an earlier staged document value");
    document.GetHistory().Group("Two document changes",
                                [&]
                                {
                                    document.SetText("intermediate");
                                    document.SetText("final");
                                });
    Check(document.GetSnapshot().GetText() == "final" && document.GetHistory().GetState().m_UndoCount == 1 &&
              notifications == 1,
          "Successful groups publish only their final root and one undo command");
    Check(document.GetHistory().Undo() && document.GetSnapshot() == original,
          "Grouped undo restores the complete original root atomically");
    document.GetHistory().Redo();
    document.GetHistory().MarkSaved();
    const auto clean = document.GetSnapshot();
    const auto cleanHistory = document.GetHistory().GetState();
    Throws([&] { document.GetHistory().Group("Invalid nested reset", [&] { document.Reset("loaded"); }); },
           "Loading cannot silently discard an enclosing command group");
    Check(document.GetSnapshot() == clean && document.GetHistory().GetState() == cleanHistory,
          "A prohibited grouped reset has no side effects");
    forbid = false;
    document.SetText("forbidden");
    const auto beforeUndo = document.GetHistory().GetState();
    forbid = true;
    // Undo reaches the accepted clean root; redo must revalidate its stored candidate.
    document.GetHistory().Undo();
    const auto afterUndo = document.GetSnapshot();
    const auto afterUndoHistory = document.GetHistory().GetState();
    Throws([&] { document.GetHistory().Redo(); }, "Redo cannot bypass current document validators");
    Check(document.GetSnapshot() == afterUndo && document.GetHistory().GetState() == afterUndoHistory &&
              beforeUndo.m_UndoCount == afterUndoHistory.m_UndoCount + 1,
          "Rejected redo preserves both sides of the timeline");

    TextDocument observed(store);
    std::vector<std::string> seen;
    bool nestedRejected = false;
    observed.Subscribe(
        [&](const StateChange<DocumentSnapshot>& change)
        {
            seen.push_back(change.GetAfter().GetText());
            if (change.GetAfter().GetText() == "first")
            {
                try
                {
                    observed.SetText("unqueued");
                }
                catch (const std::logic_error&)
                {
                    nestedRejected = true;
                }
                observed.GetHandle().Post([](TextDocument& live) { live.SetText("queued"); });
            }
        });
    observed.SetText("first");
    Check(nestedRejected && observed.GetSnapshot().GetText() == "first" && seen == std::vector<std::string>{"first"},
          "Synchronous notification mutations reject without losing the current document edit");
    dispatcher.Drain();
    Check(observed.GetSnapshot().GetText() == "queued" && seen == std::vector<std::string>({"first", "queued"}) &&
              observed.GetHistory().GetState().m_UndoCount == 2,
          "Explicit producer handles deliver observer follow-up edits in a later owner turn");
    TextDocument timelineDocument(store);
    timelineDocument.SetText("one");
    timelineDocument.SetText("two");
    timelineDocument.GetHistory().Undo();
    const auto timelineRoot = timelineDocument.GetSnapshot();
    const auto timelineState = timelineDocument.GetHistory().GetState();
    State<int> external(store, 0);
    bool crossStateRejected = false;
    bool notificationUndoRejected = false, notificationRedoRejected = false;
    external.Subscribe(
        [&](const StateChange<int>&)
        {
            try
            {
                observed.SetText("cross-state mutation");
            }
            catch (const std::logic_error&)
            {
                crossStateRejected = true;
            }
            try
            {
                timelineDocument.GetHistory().Undo();
            }
            catch (const std::logic_error&)
            {
                notificationUndoRejected = true;
            }
            try
            {
                timelineDocument.GetHistory().Redo();
            }
            catch (const std::logic_error&)
            {
                notificationRedoRejected = true;
            }
        });
    external.Set(1);
    Check(crossStateRejected && observed.GetSnapshot().GetText() == "queued",
          "Notification guards cover observers of other values in the same state domain");
    Check(notificationUndoRejected && notificationRedoRejected && timelineDocument.GetSnapshot() == timelineRoot &&
              timelineDocument.GetHistory().GetState() == timelineState,
          "Available undo and redo commands reject notification-time document mutation without timeline drift");
}

void LimitsOwnershipAndSharing()
{
    UiDispatcher dispatcher;
    StateStore store(dispatcher);
    DocumentLimits limits;
    limits.m_MaxBytes = 12;
    limits.m_MaxParagraphBytes = 5;
    limits.m_MaxParagraphs = 3;
    TextDocument bounded(store, limits);
    bounded.Reset("abc\ndef");
    const auto previous = bounded.GetSnapshot();
    const auto history = bounded.GetHistory().GetState();
    Throws([&] { bounded.SetText("abcdef"); }, "One paragraph cannot exceed its configured byte limit");
    Throws([&] { bounded.SetText("a\nb\nc\nd"); }, "Paragraph admission includes mandatory empty paragraphs");
    Throws([&] { bounded.SetText("abcde\nfghij\nk"); }, "Whole-story byte admission includes LF separators");
    Check(bounded.GetSnapshot() == previous && bounded.GetHistory().GetState() == history,
          "Every resource rejection preserves canonical text and undo metadata");
    HistoryLimits shortHistory;
    shortHistory.m_MaxCommands = 2;
    TextDocument pruned(store, {}, shortHistory);
    pruned.SetText("a");
    pruned.SetText("b");
    pruned.SetText("c");
    Check(pruned.GetHistory().GetState().m_UndoCount == 2 && pruned.GetHistory().Undo() && pruned.GetHistory().Undo() &&
              !pruned.GetHistory().Undo() && pruned.GetSnapshot().GetText() == "a" &&
              pruned.GetHistory().GetState().m_Modified,
          "History prunes whole oldest commands while preserving current values and dirty state");
    TextDocument unsaved(store);
    unsaved.Reset("unsaved import", false);
    Check(unsaved.GetSnapshot().GetText() == "unsaved import" && unsaved.GetHistory().GetState().m_Modified &&
              unsaved.GetHistory().GetState().m_UndoCount == 0,
          "An explicitly unsaved load clears command retention while preserving dirty state");
    bool rejectedWorkerMutation = false;
    std::thread invalidWriter(
        [&]
        {
            try
            {
                bounded.SetText("x");
            }
            catch (const std::logic_error&)
            {
                rejectedWorkerMutation = true;
            }
        });
    invalidWriter.join();
    Check(rejectedWorkerMutation && bounded.GetSnapshot() == previous,
          "The live document remains owner-thread guarded");

    DocumentSnapshot retained;
    std::shared_ptr<const DocumentParagraph> retainedParagraph;
    auto dying = std::make_unique<TextDocument>(store);
    dying->Reset("saved\nworker");
    retained = dying->GetSnapshot();
    retainedParagraph = retained.GetParagraph(1);
    const auto endpoint = dying->GetHandle();
    const auto pending = endpoint.Post([](TextDocument& live) { live.SetText("expired target"); });
    dying->Subscribe(
        [&](const StateChange<DocumentSnapshot>& change)
        {
            Check(change.GetAfter().GetText() == "published", "A deletion observer receives owned published content");
            dying.reset();
        });
    dying->SetText("published");
    Check(!dying && !endpoint.IsConnected(), "Owner deletion during publication expires document producer handles");
    dispatcher.Drain();
    Check(pending.GetStatus() == DispatchStatus::TargetExpired,
          "Queued commands do not revive a deleted document receiver");
    std::exception_ptr workerFailure;
    std::thread reader(
        [&]
        {
            try
            {
                if (retained.GetText() != "saved\nworker" || retainedParagraph->m_Text != "worker" ||
                    retained.GetPosition(1).m_ParagraphId != retainedParagraph->m_Id)
                {
                    throw std::runtime_error("Retained worker snapshot changed after owner deletion");
                }
            }
            catch (...)
            {
                workerFailure = std::current_exception();
            }
        });
    reader.join();
    if (workerFailure)
    {
        std::rethrow_exception(workerFailure);
    }
    Check(retained.GetParagraphCount() == 2, "Owned snapshots and paragraph payloads survive document destruction");

    auto domain = std::make_unique<StateStore>(dispatcher);
    auto orphan = std::make_unique<TextDocument>(*domain);
    orphan->Reset("retained after domain closure");
    const auto detachedSnapshot = orphan->GetSnapshot();
    const auto detachedEndpoint = orphan->GetHandle();
    domain.reset();
    Throws([&] { orphan->GetSnapshot(); }, "A live wrapper diagnoses destruction of its state domain");
    Throws([&] { orphan->SetText("unavailable"); },
           "A closed state domain rejects document mutation before dereferencing its former owner");
    const auto expired = detachedEndpoint.Post([](TextDocument& live) { live.SetText("unavailable"); });
    dispatcher.Drain();
    Check(expired.GetStatus() == DispatchStatus::TargetExpired &&
              detachedSnapshot.GetText() == "retained after domain closure",
          "Closed-domain producer delivery expires while owned snapshots remain readable");
    orphan.reset();

    std::string large;
    large.reserve(3000000);
    for (std::size_t row = 0; row < 100000; ++row)
    {
        if (row)
        {
            large += '\n';
        }
        large += "line " + std::to_string(row) + " = 123.456 mm";
    }
    TextDocument document(store);
    document.Reset(large);
    const auto before = document.GetSnapshot();
    const auto middle = before.GetPosition(50000);
    document.Replace(middle, middle, "X", "One visible paragraph");
    const auto after = document.GetSnapshot();
    Check(before.GetParagraphCount() == 100000 && after.GetParagraphCount() == 100000 &&
              after.GetByteCount() == before.GetByteCount() + 1,
          "A local edit preserves the large paragraph inventory and exact byte accounting");
    Check(before.GetParagraph(0) == after.GetParagraph(0) && before.GetParagraph(49999) == after.GetParagraph(49999) &&
              before.GetParagraph(99999) == after.GetParagraph(99999) &&
              before.GetParagraph(50000) != after.GetParagraph(50000) &&
              before.GetParagraph(50000)->m_Id == after.GetParagraph(50000)->m_Id,
          "A local edit shares unrelated owned payloads while keeping the edited paragraph identity");
    Check(document.GetHistory().GetState().m_Bytes < before.GetByteCount() / 8,
          "One local command does not charge or copy the entire large document into history");
    const auto change = after.GetChangeFrom(before);
    Check(!change.m_FullReset && change.m_FirstParagraph == 50000 && change.m_RemovedParagraphs == 1 &&
              change.m_InsertedParagraphs == 1,
          "A large-document edit invalidates only its affected paragraph extent");
    Check(document.GetHistory().Undo() && document.GetSnapshot() == before && before.GetText() == large,
          "Undo and retained snapshots preserve the complete large document exactly");
}

void ReferenceEditing()
{
    UiDispatcher dispatcher;
    StateStore store(dispatcher);
    HistoryLimits historyLimits;
    historyLimits.m_MaxCommands = 1024;
    TextDocument document(store, {}, historyLimits);
    document.Reset("first\nsecond\n");
    std::string reference = document.GetSnapshot().GetText();
    std::vector<std::string> values{reference};
    const std::vector<std::string> inserts{"", "x", "\n", "\t ", "ab\ncd", "  ", "xyz"};
    std::uint32_t seed = 0x93a427d1U;
    const auto next = [&]()
    {
        seed = seed * 1664525U + 1013904223U;
        return seed;
    };
    for (int step = 0; step < 300; ++step)
    {
        auto first = static_cast<std::size_t>(next()) % (reference.size() + 1);
        auto last = static_cast<std::size_t>(next()) % (reference.size() + 1);
        if (first > last)
        {
            std::swap(first, last);
        }
        const auto& insert = inserts[next() % inserts.size()];
        const auto snapshot = document.GetSnapshot();
        const auto before = reference;
        reference.replace(first, last - first, insert);
        const bool changed = document.Replace(PositionAt(snapshot, before, first), PositionAt(snapshot, before, last),
                                              insert, "Reference splice");
        Check(changed == (reference != before) && document.GetSnapshot().GetText() == reference &&
                  document.GetSnapshot().GetByteCount() == reference.size(),
              "Deterministic cross-paragraph editing matches an independent flat-string reference");
        if (changed)
        {
            values.push_back(reference);
        }
        if (changed && step % 7 == 0)
        {
            Check(document.GetHistory().Undo() && document.GetSnapshot().GetText() == before,
                  "An intermediate randomized undo restores the reference predecessor");
            Check(document.GetHistory().Redo() && document.GetSnapshot().GetText() == reference,
                  "An intermediate randomized redo restores the reference successor");
        }
    }
    bool undoMatches = true;
    for (std::size_t i = values.size() - 1; i > 0; --i)
    {
        undoMatches &= document.GetHistory().Undo() && document.GetSnapshot().GetText() == values[i - 1];
    }
    Check(undoMatches && !document.GetHistory().Undo(),
          "The complete randomized undo timeline matches reference values");
    bool redoMatches = true;
    for (std::size_t i = 1; i < values.size(); ++i)
    {
        redoMatches &= document.GetHistory().Redo() && document.GetSnapshot().GetText() == values[i];
    }
    Check(redoMatches && !document.GetHistory().Redo(),
          "The complete randomized redo timeline matches reference values");
}
} // namespace

int main()
{
    try
    {
        TextAndPositions();
        StructuralEditsAndHistory();
        TransactionsAndNotifications();
        LimitsOwnershipAndSharing();
        ReferenceEditing();
        std::cout << "Text document: " << s_Checks << " checks passed\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << "Text document: " << error.what() << '\n';
        return 1;
    }
}
