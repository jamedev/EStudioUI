#pragma once

#include "CanvasScene.h"

#include <cstddef>
#include <iosfwd>
#include <memory>
#include <vector>

namespace EStudioUI
{
//! An editable 2D sketch with BVH picking, snapping, selection, and bounded command history.
class SketchDocument
{
  public:
    //! Creates an empty document without any platform or rendering dependency.
    SketchDocument();

    //! Releases geometry and retained edit commands.
    ~SketchDocument();

    //! Documents own their scene and command identities.
    SketchDocument(const SketchDocument&) = delete;

    //! Documents own their scene and command identities.
    SketchDocument& operator=(const SketchDocument&) = delete;

    //! Returns the scene; direct bulk edits bypass history and should be followed by ClearHistory.
    CanvasScene& GetScene();

    //! Returns read-only access to the scene.
    const CanvasScene& GetScene() const;

    //! Removes all geometry, selection, and command history.
    void Clear();

    //! Discards undo/redo commands after a direct import or external scene edit.
    void ClearHistory();

    //! Writes versioned geometry and style indices in painter order, throwing on stream failure.
    void Save(std::ostream& output) const;

    //! Atomically replaces the document from a valid versioned stream of at most one million items.
    void Load(std::istream& input);

    //! Adds and selects a primitive; an empty ID means its selection history exceeds the memory budget.
    CanvasItemId AddShape(const CanvasGeometry& geometry, std::uint32_t style = 0);

    //! Returns the nearest outline within world tolerance, preferring the top item on exact ties.
    CanvasItemId Pick(CanvasPoint point, double tolerance) const;

    //! Replaces selection, or toggles one item when additive is true.
    void Select(CanvasItemId item, bool additive = false);

    //! Selects primitives wholly contained by the world rectangle, optionally retaining selection.
    void SelectBox(CanvasBounds bounds, bool additive = false);

    //! Clears selected flags only on selected items.
    void ClearSelection();

    //! Returns live selected identities; removed scene identities never escape this method.
    std::vector<CanvasItemId> GetSelection() const;

    //! Translates the selection in one command; returns false on no change or an exceeded history budget.
    bool MoveSelection(CanvasPoint delta);

    //! Deletes the selection in one command; returns false on empty selection or an exceeded history budget.
    bool DeleteSelection();

    //! Reverses the most recent edit and restores its selection.
    bool Undo();

    //! Reapplies the most recently undone edit and restores its selection.
    bool Redo();

    //! Returns whether an undo command is retained.
    bool CanUndo() const;

    //! Returns whether an undone command can be reapplied.
    bool CanRedo() const;

    //! Snaps to nearby endpoints, centers, or cardinal points; otherwise rounds to an enabled grid.
    CanvasPoint Snap(CanvasPoint point, double worldTolerance, double gridSpacing, bool gridEnabled = true) const;

    //! Returns approximate retained command payload, bounded to 128 MiB.
    std::size_t GetHistoryBytes() const;

  private:
    //! Owns scene data, selected IDs, logical command identities, and bounded history.
    struct Impl;

    //! Keeps edit command machinery out of the public interface.
    std::unique_ptr<Impl> m_Impl;
};
} // namespace EStudioUI
