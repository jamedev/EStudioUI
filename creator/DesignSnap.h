#pragma once
#include <EStudioUI/UiDocument.h>

namespace EStudioUI
{
//! Captured container or sibling geometry used during one design gesture.
struct DesignReference
{
    //! Stable sibling identity, or parent.
    std::string m_Id;
    //! Bounds in common layout coordinates.
    Rect m_Bounds;
};

//! Owned snapping preferences, independent of the layout solver.
struct DesignSnapSettings
{
    //! Enables container and sibling edge/center alignment.
    bool m_Enabled = true;
    //! Adds a container-relative grid as a lower-priority candidate.
    bool m_Grid = false;
    //! Stores snapped references as ordinary layout constraints when requested.
    bool m_KeepRelations = false;
    //! Grid spacing and capture distance in logical pixels.
    float m_GridSpacing = 8, m_Distance = 6;
};

//! One visible guide and optional persistent anchor relationship.
struct DesignGuide
{
    //! Selects a vertical guide for X alignment.
    bool m_X = true;
    //! Guide coordinate in the preview's layout coordinate system.
    float m_Position = 0;
    //! Describes an anchor match; an empty target identifies a grid match.
    UiConstraint m_Rule;
};

//! Complete provisional geometry and at most two alignment guides.
struct DesignSnapResult
{
    //! Snapped geometry before any canonical document mutation.
    Rect m_Bounds;
    //! Contains at most one guide per axis.
    std::vector<DesignGuide> m_Guides;
};

//! Resolves closest edges/centers or grid points against one captured sibling set.
DesignSnapResult SnapDesign(Rect bounds, bool resize, Rect container, const std::vector<DesignReference>& references,
                            const DesignSnapSettings& settings);
} // namespace EStudioUI
