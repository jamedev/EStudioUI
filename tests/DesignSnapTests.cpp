#include "DesignSnap.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

int main()
{
    using namespace EStudioUI;
    try
    {
        std::size_t checks = 0;
        const auto check = [&](bool value, const char* why)
        {
            if (!value)
            {
                throw std::runtime_error(why);
            }
            ++checks;
        };
        DesignSnapSettings settings;
        const Rect parent{20, 30, 800, 600};
        auto result = SnapDesign({23, 100, 120, 40}, false, parent, {}, settings);
        check(result.m_Bounds.m_X == 20 && result.m_Guides[0].m_Rule.m_Target == "parent",
              "A close container edge supplies a persistent parent anchor");
        result = SnapDesign({197, 102, 50, 40}, false, parent, {{"reference", {200, 100, 60, 40}}}, settings);
        check(result.m_Bounds.m_X == 200 && result.m_Bounds.m_Y == 100 && result.m_Guides.size() == 2,
              "Sibling edges snap independently on both axes");
        result = SnapDesign({100, 200, 97, 50}, true, parent, {{"reference", {200, 400, 80, 40}}}, settings);
        check(result.m_Bounds.m_X == 100 && result.m_Bounds.m_Width == 100,
              "Resize snapping retains the opposite corner");
        settings.m_Grid = true;
        result = SnapDesign({111, 113, 40, 40}, false, parent, {}, settings);
        check(result.m_Bounds.m_X == 108 && result.m_Bounds.m_Y == 110,
              "Grid coordinates are relative to the actual content origin");
        settings.m_Enabled = false;
        const Rect original{111, 113, 40, 40};
        result = SnapDesign(original, false, parent, {}, settings);
        check(result.m_Bounds == original && result.m_Guides.empty(),
              "Snapping can be disabled without geometry changes");
        std::cout << "Design snapping: " << checks << " checks passed\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
