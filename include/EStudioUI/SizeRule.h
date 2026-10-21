#pragma once

namespace EStudioUI
{
//! An explicit outer-size contract; Fill divides free space after fixed/content items.
struct SizeRule
{
    //! Available sizing modes.
    enum class Mode
    {
        Fixed,
        Content,
        Fill
    };

    //! Creates a fixed logical-pixel size that never stretches or shrinks.
    static SizeRule Fixed(float pixels);

    //! Uses intrinsic content size, clamped to the supplied limits.
    static SizeRule Content(float minimum = 0, float maximum = 1000000);

    //! Shares remaining space by weight, respecting hard minimum/maximum sizes.
    static SizeRule Fill(float weight = 1, float minimum = 0, float maximum = 1000000);

    //! Rejects invalid manually constructed sizing contracts.
    void Validate() const;

    //! Selected sizing mode.
    Mode m_Mode = Mode::Content;

    //! Fixed pixels or positive Fill weight.
    float m_Value = 0;

    //! Hard lower bound in logical pixels.
    float m_Minimum = 0;

    //! Hard upper bound in logical pixels.
    float m_Maximum = 1000000;
};
} // namespace EStudioUI
