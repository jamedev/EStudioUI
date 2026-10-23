#include <EStudioUI/Animation.h>
#include <EStudioUI/Button.h>
#include <EStudioUI/ImageView.h>
#include <EStudioUI/MenuItem.h>
#include <EStudioUI/SemanticTree.h>
#include <EStudioUI/UiContext.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>
#include <vector>

namespace
{
using namespace EStudioUI;
int s_Checks = 0;

void Check(bool condition, const char* message)
{
    ++s_Checks;
    if (!condition)
        throw std::runtime_error(message);
}

template <class F> void Throws(F action, const char* message)
{
    bool rejected = false;
    try
    {
        action();
    }
    catch (const std::exception&)
    {
        rejected = true;
    }
    Check(rejected, message);
}

std::filesystem::path Asset(const char* name)
{
    return std::filesystem::path(ESTUDIOUI_IMAGE_DIR) / name;
}

void Resources()
{
    Image empty;
    Check(empty.IsEmpty() && empty.GetIdentity() == 0 && empty.GetPixels().empty(), "Empty image is a cheap value");
    std::array<std::uint8_t, 8> pixels{255, 0, 0, 255, 0, 0, 255, 128};
    const auto image = Image::FromRgba(2, 1, pixels, 2);
    pixels[0] = 0;
    Check(image.GetPixels()[0] == 255 && image.GetPixels()[7] == 128, "Image owns immutable straight-alpha pixels");
    const auto copy = image;
    Check(copy == image && copy.GetPixels().data() == image.GetPixels().data(),
          "Copies share pixel storage and identity");
    Check(image.GetSize() == Point{1, .5f}, "Source density determines natural logical dimensions");
    Check(Image::FromRgba(2, 1, image.GetPixels()) != image, "Equal pixels have distinct lifetime identities");
    Throws([&] { Image::FromRgba(0, 1, pixels); }, "Zero width is rejected");
    Throws([&] { Image::FromRgba(16385, 1, pixels); }, "Oversize axes fail before allocation");
    Throws([&] { Image::FromRgba(8192, 8192, pixels); }, "Oversize decoded bytes fail before allocation");
    Throws([&] { Image::FromRgba(2, 2, pixels); }, "Incorrect RGBA length is rejected");
    Throws([&] { Image::FromRgba(2, 1, pixels, std::numeric_limits<float>::quiet_NaN()); },
           "Nonfinite density is rejected");
    Throws([&] { Image::FromRgba(2, 1, pixels, 0); }, "Zero density is rejected");
    Throws([&] { Image::Decode({}); }, "Empty encoded input is rejected");
    Throws([&] { Image::Decode(pixels); }, "Unsupported or malformed images are rejected");
    Throws([&] { Image::Load(Asset("missing.png")); }, "Missing files produce a visible failure");
    const auto png = Image::Load(Asset("pixel-fixture.png"));
    Check(png.GetWidth() == 4 && png.GetHeight() == 2 && png.GetPixels()[0] == 255 && png.GetPixels()[15] == 255 &&
              png.GetPixels()[16 + 2] == 255 && png.GetPixels().back() == 128,
          "PNG preserves top-down orientation and straight alpha");
    const auto jpeg = Image::Load(Asset("workspace-preview.jpg"));
    Check(jpeg.GetWidth() == 640 && jpeg.GetHeight() == 360 && jpeg.GetPixels()[3] == 255,
          "JPEG converts to opaque RGBA8");
    std::ifstream stream(Asset("pixel-fixture.png"), std::ios::binary);
    std::vector<std::uint8_t> encoded(std::istreambuf_iterator<char>(stream), {});
    const auto decoded = Image::Decode(encoded);
    Check(std::equal(decoded.GetPixels().begin(), decoded.GetPixels().end(), png.GetPixels().begin()),
          "Memory and file decoding produce identical pixels");
    encoded.resize(20);
    Throws([&] { Image::Decode(encoded); }, "Truncated PNG cannot publish partial pixel storage");

    std::vector<std::thread> workers;
    std::array<std::uint64_t, 8> identities{};
    std::array<bool, 8> valid{};
    for (std::size_t i = 0; i < identities.size(); ++i)
        workers.emplace_back(
            [&, i]
            {
                try
                {
                    for (int n = 0; n < 6; ++n)
                    {
                        const auto loaded = Image::Load(Asset("pixel-fixture.png"));
                        identities[i] = loaded.GetIdentity();
                        valid[i] = copy.GetPixels()[0] == 255 && loaded.GetPixels().back() == 128;
                    }
                }
                catch (...)
                {
                    valid[i] = false;
                }
            });
    for (auto& worker : workers)
        worker.join();
    Check(std::all_of(valid.begin(), valid.end(), [](bool value) { return value; }),
          "Independent workers can decode and share images");
    std::sort(identities.begin(), identities.end());
    Check(std::adjacent_find(identities.begin(), identities.end()) == identities.end(),
          "Concurrent resources retain unique identities");
}

void Densities()
{
    const auto normal = Image::Load(Asset("sketch-icon.png"));
    const auto high = Image::Load(Asset("sketch-icon-2x.png"), 2);
    Icon icon{high, normal};
    Check(icon.GetSize() == Point{24, 24}, "Icon sources agree on logical dimensions");
    Check(icon.Select(.5f) == normal && icon.Select(1) == normal && icon.Select(1.25f) == high &&
              icon.Select(8) == high,
          "Icon selection avoids unnecessary upsampling and falls back at high density");
    Check(Icon{}.Select(1).IsEmpty(), "Empty icons have an empty image fallback");
    Throws([&] { Icon invalid{normal, normal}; }, "Duplicate densities are rejected");
    Throws([&] { Icon invalid{normal, Image{}}; }, "Empty density entries are rejected");
    Throws([&] { Icon invalid{normal, Image::Load(Asset("pixel-fixture.png"), 2)}; },
           "Mismatched logical sizes are rejected");
    Throws([&] { icon.Select(0); }, "Invalid destination density is rejected");
}

void Controls()
{
    FontFace font(ESTUDIOUI_FONT_PATH);
    UiContext ui(font);
    auto& view = ui.Root().Add<ImageView>(Image::Load(Asset("pixel-fixture.png")));
    view.SetAccessibleName("Drawing preview");
    view.SetFloatingBounds({10, 10, 160, 80});
    auto& button = ui.Root().Add<Button>("Open drawing");
    button.SetFloatingBounds({10, 100, 220, 42});
    button.SetImageIcon(Image::Load(Asset("sketch-icon.png")));
    std::string error;
    Check(ui.SetStyleSheet(
              "window{padding:0;} imageview {object-fit:cover;object-position:right top;} "
              "button::image-icon {width:24px;height:20px;gap:9px;opacity:.8;} button:hover::image-icon {opacity:.6;}",
              error),
          "Image CSS and button image-icon parts parse");
    ui.Prepare(400, 250);
    Check(view.GetStyle().m_ObjectFit == "cover" && view.GetStyle().m_ObjectPositionX == 1 &&
              view.GetStyle().m_ObjectPositionY == 0,
          "Object fitting and keyword alignment reach computed style");
    Check(button.GetImageIconSize() == Point{24, 20}, "CSS controls icon dimensions");
    const auto initialMeasures = ui.GetStats().m_Measurements;
    view.SetFit("contain");
    view.SetAlignment(.25f, .75f);
    view.SetImage(Image::Load(Asset("pixel-fixture.png")));
    ui.Prepare(400, 250);
    Check(ui.GetStats().m_Measurements == initialMeasures,
          "Equal-size image replacement and positioning retain measurement caches");
    Check(!ui.HasAnimations(), "Static images do not request continuous frames");
    auto node = ui.Semantics().GetNode(ui.Semantics().GetId(view));
    Check(node && node->m_Info.m_Role == SemanticRole::Image && node->m_Info.m_Name == "Drawing preview" &&
              !node->m_Focusable,
          "Image semantics carry an accessible name without an extra Tab stop");
    button.SetImageIconSize({30, 26});
    ui.Prepare(400, 250);
    Check(button.GetImageIconSize() == Point{30, 26}, "Explicit icon size has precedence over CSS");
    button.ClearImageIconSize();
    ui.Prepare(400, 250);
    Check(button.GetImageIconSize() == Point{24, 20}, "Clearing explicit size restores CSS");
    const auto beforeIcon = ui.GetStats().m_Measurements;
    button.SetImageIcon(Image::Load(Asset("sketch-icon.png")));
    ui.Prepare(400, 250);
    Check(ui.GetStats().m_Measurements == beforeIcon, "Replacing a nonempty fixed-slot icon is paint-only");
    ui.PointerMove({20, 110});
    ui.Prepare(400, 250);
    Check(std::abs(button.GetPartStyle("image-icon").m_Opacity - .6f) < .001f,
          "Hover styling uses the ordinary part cascade");
    Throws([&] { view.SetFit("unknown"); }, "Unsupported fit cannot change computed styling");
    Throws([&] { view.SetAlignment(.1f, 2); }, "Alignment validates both coordinates before mutation");
    Check(view.GetStyle().m_ObjectPositionX == .25f, "Invalid alignment preserves the previous horizontal value");
    Throws([&] { button.SetImageIconSize({0, 20}); }, "Invalid icon sizes are rejected");
    Check(FindStyleProperties("object-position").size() == 2, "Object-position expands to two animatable properties");
    view.ClearStyleProperty(StyleProperty::ObjectPositionX);
    AnimationOptions timing;
    timing.m_Duration = 1;
    timing.m_Easing = EasingCurve(EasingCurve::Kind::Linear);
    Animation motion(timing);
    motion.AddStyleProperty(view, StyleProperty::ObjectPositionX, 0.0f, 1.0f);
    ui.Animations().Play(std::move(motion));
    ui.Prepare(400, 250);
    const auto beforeMotion = ui.GetStats().m_Measurements;
    ui.SetTime(.5);
    ui.Prepare(400, 250);
    Check(std::abs(view.GetStyle().m_ObjectPositionX - .5f) < .001f && ui.GetStats().m_Measurements == beforeMotion,
          "Animated image positioning interpolates without reshaping or measurement");
    ui.SetTime(1);
    for (const auto* css : {"object-position:bottom right;", "object-position:25% 75%;", "object-position:top;",
                            "object-fit:scale-down;"})
        Check(ui.SetStyleSheet(std::string("imageview{") + css + "}", error), "Image positioning CSS forms parse");
    for (const auto* css :
         {"object-position:120% 0%;", "object-fit:stretch;", "object-position:left left;", "object-position:10px;"})
        Check(!ui.SetStyleSheet(std::string("imageview{") + css + "}", error),
              "Unsupported image CSS fails explicitly");
    bool rejected = false;
    std::thread wrong(
        [&]
        {
            try
            {
                view.SetImage({});
            }
            catch (const std::logic_error&)
            {
                rejected = true;
            }
        });
    wrong.join();
    Check(rejected, "Image widgets preserve owner-thread mutation checks");
}
} // namespace

int main()
{
    try
    {
        Resources();
        Densities();
        Controls();
        std::cout << "Image resources and controls: " << s_Checks << " checks passed.\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Images: " << error.what() << '\n';
        return 1;
    }
}
