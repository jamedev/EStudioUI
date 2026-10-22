#pragma once

#include "ModelSnapshot.h"
#include "Style.h"

namespace EStudioUI
{
class Renderer;
class FontFace;

//! Customizes virtual item presentation and typed editing without allocating widgets per record.
class ItemDelegate
{
  public:
    //! Releases an immutable application delegate.
    virtual ~ItemDelegate();

    //! Returns owned display text; the default uses the model's Display role and formatter.
    virtual std::string GetText(const ModelSnapshot& snapshot, ItemId item, std::size_t column) const;

    //! Applies semantic foreground/background/enabled roles over the view's computed CSS part.
    virtual Style GetStyle(const Style& css, const ModelSnapshot& snapshot, ItemId item, std::size_t column,
                           bool selected) const;

    //! Returns true after drawing custom GPU contents; false requests the view's cached text renderer.
    virtual bool Paint(Renderer& renderer, FontFace& font, const ModelSnapshot& snapshot, ItemId item,
                       std::size_t column, Rect bounds, const Style& style, Rect clip, float opacity) const;

    //! Returns raw editor text independently of formatted display units.
    virtual std::string GetEditorText(const ModelSnapshot& snapshot, ItemId item, std::size_t column) const;

    //! Parses and validates committed editor text into an owned typed value.
    virtual ModelValue ParseEdit(const ModelSnapshot& snapshot, ItemId item, std::size_t column,
                                 std::string_view text) const;
};
} // namespace EStudioUI
