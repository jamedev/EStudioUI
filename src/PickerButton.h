#pragma once
#include <EStudioUI/ToolButton.h>
#include <EStudioUI/Renderer.h>

namespace EStudioUI::Detail
{
//! Small internal GPU symbol command, independent of registered font coverage.
class PickerButton final : public ToolButton
{
  public:
    //! Supported analytic symbols.
    enum class Symbol
    {
        Calendar,
        Clock,
        Down,
        Close
    };

    //! Creates a symbol-only ordinary command.
    explicit PickerButton(Symbol symbol) : ToolButton(""), m_Symbol(symbol)
    {
        SetWidth(SizeRule::Fixed(32));
    }

  protected:
    //! Supplies a bounded icon measurement.
    Point Measure(FontFace&) const override
    {
        return {16, 16};
    }

    //! Paints normal CSS chrome and an analytic icon.
    void Paint(Renderer& renderer, FontFace& font, double time, float opacity) const override
    {
        Widget::Paint(renderer, font, time, opacity);
        const auto b = GetBounds();
        const Point c{b.m_X + b.m_Width * .5f, b.m_Y + b.m_Height * .5f};
        auto color = GetStyle().m_Color;
        color.m_A *= opacity;
        const auto clip = GetClip();
        const auto line = [&](float x1, float y1, float x2, float y2)
        { renderer.DrawLine({c.m_X + x1, c.m_Y + y1}, {c.m_X + x2, c.m_Y + y2}, color, 1.4f, clip); };
        if (m_Symbol == Symbol::Clock)
        {
            Style style;
            style.m_BorderWidth = 1.4f;
            style.m_BorderColor = color;
            renderer.DrawCircle(c, 7, style, clip);
            line(0, -4, 0, 0);
            line(0, 0, 3, 2);
        }
        else if (m_Symbol == Symbol::Calendar)
        {
            line(-6, -5, 6, -5);
            line(-6, -5, -6, 6);
            line(6, -5, 6, 6);
            line(-6, 6, 6, 6);
            line(-6, -1, 6, -1);
            line(-3, -7, -3, -3);
            line(3, -7, 3, -3);
            line(-2, 1, -2, 4);
            line(2, 1, 2, 4);
        }
        else if (m_Symbol == Symbol::Down)
        {
            line(-4, -2, 0, 2);
            line(0, 2, 4, -2);
        }
        else
        {
            line(-4, -4, 4, 4);
            line(-4, 4, 4, -4);
        }
    }

  private:
    //! Selected symbol.
    Symbol m_Symbol;
};
} // namespace EStudioUI::Detail
