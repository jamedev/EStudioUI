#include <EStudioUI/Icon.h>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace EStudioUI
{
Icon::Icon(Image image)
{
    if (!image.IsEmpty())
        m_Images.push_back(std::move(image));
}

Icon::Icon(std::initializer_list<Image> images)
{
    if (images.size() > 16)
        throw std::length_error("An icon supports at most sixteen source densities.");
    for (const auto& image : images)
    {
        if (image.IsEmpty())
            throw std::invalid_argument("Icon density lists cannot contain empty images.");
        const auto size = image.GetSize();
        const auto natural = m_Images.empty() ? size : m_Images.front().GetSize();
        if (std::abs(size.m_X - natural.m_X) > .001f || std::abs(size.m_Y - natural.m_Y) > .001f)
            throw std::invalid_argument("Icon densities must have identical natural logical dimensions.");
        if (std::any_of(m_Images.begin(), m_Images.end(),
                        [&](const Image& existing) { return existing.GetPixelScale() == image.GetPixelScale(); }))
            throw std::invalid_argument("Icon source densities must be distinct.");
        m_Images.push_back(image);
    }
    std::sort(m_Images.begin(), m_Images.end(),
              [](const Image& left, const Image& right) { return left.GetPixelScale() < right.GetPixelScale(); });
}

bool Icon::IsEmpty() const noexcept
{
    return m_Images.empty();
}

Point Icon::GetSize() const noexcept
{
    return m_Images.empty() ? Point{} : m_Images.front().GetSize();
}

const Image& Icon::Select(float pixelScale) const
{
    if (!std::isfinite(pixelScale) || pixelScale <= 0)
        throw std::invalid_argument("Icon destination density must be finite and positive.");
    if (m_Images.empty())
    {
        static const Image empty;
        return empty;
    }
    const auto found = std::lower_bound(m_Images.begin(), m_Images.end(), pixelScale,
                                        [](const Image& image, float scale) { return image.GetPixelScale() < scale; });
    return found == m_Images.end() ? m_Images.back() : *found;
}
} // namespace EStudioUI
