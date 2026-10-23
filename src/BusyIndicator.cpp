#include <EStudioUI/BusyIndicator.h>

namespace EStudioUI
{
BusyIndicator::BusyIndicator() : ProgressBar("busyindicator")
{
    SetTextVisible(false);
    SetIndeterminate(true);
}

Point BusyIndicator::Measure(FontFace&) const
{
    return {72, 8};
}
} // namespace EStudioUI
