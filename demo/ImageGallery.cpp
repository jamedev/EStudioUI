#include "ImageGallery.h"
#include <EStudioUI/CheckBox.h>
#include <EStudioUI/ComboBox.h>
#include <EStudioUI/ImageView.h>
#include <EStudioUI/MenuBar.h>
#include <EStudioUI/PaintWidget.h>
#include <EStudioUI/ScrollArea.h>
#include <EStudioUI/Slider.h>
#include <EStudioUI/ToolBar.h>
#include <glad/gl.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace EStudioUI
{
namespace
{
void Check(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(std::string("Image GPU: ") + message);
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

std::vector<std::uint8_t> Pixels()
{
    std::vector<std::uint8_t> bytes(320 * 240 * 4);
    glReadPixels(0, 0, 320, 240, GL_RGBA, GL_UNSIGNED_BYTE, bytes.data());
    return bytes;
}

Image Solid(std::uint8_t red, std::uint8_t green, std::uint8_t blue, int size = 2, float scale = 1)
{
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(size * size * 4));
    for (std::size_t i = 0; i < pixels.size(); i += 4)
    {
        pixels[i] = red;
        pixels[i + 1] = green;
        pixels[i + 2] = blue;
        pixels[i + 3] = 255;
    }
    return Image::FromRgba(size, size, pixels, scale);
}
} // namespace

ImageGallery::ImageGallery(Window& window, Widget& page, const std::filesystem::path& assets) : m_Window(window)
{
    page.AddClass("image-gallery");
    auto& scroller = page.Add<ScrollArea>();
    scroller.SetHeight(SizeRule::Fill());
    auto& body = scroller.Content();
    body.SetGap(12);
    auto& heading = body.Add<Label>("Images, previews and command icons");
    heading.AddClass("image-heading");
    heading.SetHeight(SizeRule::Content());
    auto& note = body.Add<Label>("Choose how the preview fits its frame. Change alignment, opacity and corners; "
                                 "the same image can appear in several controls.");
    note.SetWordWrap(true);
    note.SetHeight(SizeRule::Content());
    const auto image = Image::Load(assets / "images/workspace-preview.png");
    const Icon icon{Image::Load(assets / "images/sketch-icon.png"),
                    Image::Load(assets / "images/sketch-icon-2x.png", 2)};
    auto& toolbar = body.Add<ToolBar>();
    toolbar.AddButton("Drawing").SetImageIcon(icon);
    toolbar.AddButton("Inspect").SetImageIcon(icon);
    auto& disabled = toolbar.AddButton("Unavailable");
    disabled.SetImageIcon(icon);
    disabled.SetEnabled(false);
    auto& menu = body.Add<MenuBar>();
    auto& file = menu.AddMenu("Preview commands");
    file.AddAction("Open drawing").SetImageIcon(icon);
    file.AddAction("Export preview").SetImageIcon(icon);
    auto& row = body.Add<Widget>();
    row.SetLayoutDirection(Axis::Horizontal);
    row.SetHeight(SizeRule::Fixed(240));
    row.SetGap(14);
    auto& preview = row.Add<ImageView>(image);
    preview.AddClass("image-preview");
    preview.SetWidth(SizeRule::Fill());
    preview.SetHeight(SizeRule::Fill());
    preview.SetAccessibleName("Sketch drawing preview");
    auto& cover = row.Add<ImageView>(image);
    cover.AddClass("image-preview");
    cover.SetWidth(SizeRule::Fixed(230));
    cover.SetHeight(SizeRule::Fill());
    cover.SetFit("cover");
    cover.SetAccessibleName("Aspect-filled crop of the same sketch");
    const WidgetRef target(&preview);
    auto& tools = body.Add<Widget>();
    tools.AddClass("image-tools");
    tools.SetLayoutDirection(Axis::Horizontal);
    tools.SetHeight(SizeRule::Content());
    tools.SetGap(10);
    auto& fit = tools.Add<ComboBox>();
    fit.SetItems({"contain", "cover", "fill", "none", "scale-down"});
    fit.SetCurrentIndex(0);
    fit.SetWidth(SizeRule::Fixed(180));
    fit.SetAccessibleName("Preview fit");
    fit.CurrentChanged().Connect(
        [target](int index)
        {
            static const char* names[]{"contain", "cover", "fill", "none", "scale-down"};
            if (auto* live = dynamic_cast<ImageView*>(target.Get()); live && index >= 0 && index < 5)
                live->SetFit(names[index]);
        });
    auto& rounded = tools.Add<CheckBox>("Rounded frame");
    rounded.SetChecked(true);
    rounded.Toggled().Connect(
        [target](bool enabled)
        {
            if (auto* live = target.Get())
                live->SetStyleProperty(StyleProperty::Radius, enabled ? 18.0f : 0.0f);
        });
    auto& sliders = body.Add<Widget>();
    sliders.SetLayoutDirection(Axis::Horizontal);
    sliders.SetHeight(SizeRule::Fixed(45));
    sliders.SetGap(16);
    auto& alignment = sliders.Add<Slider>();
    alignment.SetRange(0, 100);
    alignment.SetValue(50);
    alignment.SetWidth(SizeRule::Fill());
    alignment.SetAccessibleName("Horizontal image alignment");
    alignment.SetToolTip("Move the preview within the frame; visible with contain, cover or natural-size fitting.");
    alignment.ValueChanged().Connect(
        [target](double value)
        {
            if (auto* live = dynamic_cast<ImageView*>(target.Get()))
                live->SetAlignment(static_cast<float>(value / 100), .5f);
        });
    auto& opacity = sliders.Add<Slider>();
    opacity.SetRange(0, 100);
    opacity.SetValue(100);
    opacity.SetWidth(SizeRule::Fill());
    opacity.SetAccessibleName("Preview opacity");
    opacity.SetToolTip("Blend the image and its styled frame over the workspace.");
    opacity.ValueChanged().Connect(
        [target](double value)
        {
            if (auto* live = target.Get())
                live->SetStyleProperty(StyleProperty::Opacity, static_cast<float>(value / 100));
        });
    auto& captions =
        body.Add<Label>("Left slider: image alignment. Right slider: opacity. The right preview uses cover.");
    captions.SetWordWrap(true);
    captions.SetHeight(SizeRule::Content());
}

void ImageGallery::Verify(const std::filesystem::path& assets, const std::filesystem::path& output)
{
    WindowOptions options;
    options.m_Width = 640;
    options.m_Height = 480;
    options.m_Headless = m_Window.IsHeadless();
    Window probe(options, assets / "NotoSans.ttf");
    probe.RenderFrame();
    auto& gpu = probe.Graphics();
    const Rect clip{0, 0, 320, 240};
    const auto image = Image::Load(assets / "images/pixel-fixture.png");
    const std::array<std::uint8_t, 8> alphaBytes{255, 0, 0, 255, 0, 0, 255, 0};
    const auto alpha = Image::FromRgba(2, 1, alphaBytes);
    const auto render = [&](Renderer& renderer, int density, bool alternate)
    {
        renderer.BeginFrame(320, 240, 320 * density, 240 * density, {0, 0, 0, 1});
        renderer.DrawImage({10, 10, 160, 80}, image, clip);
        renderer.DrawImage({180, 10, 120, 80}, alpha, clip, alternate ? .6f : 1);
        renderer.PushState({1, .15f, -.15f, 1, 8, 0});
        renderer.PushClip({30, 105, 150, 80}, 14);
        renderer.DrawImage({30, 105, 150, 80}, image, clip, .8f);
        renderer.PopState();
        renderer.DrawImage({210, 110, 80, 80}, image, clip, 1, {0, 0, .5f, 1});
        renderer.DrawSolid({200, 150, 60, 55}, {0, 1, 1, .35f}, clip);
        renderer.EndFrame();
    };
    render(gpu, 1, false);
    Check(gpu.GetStats().m_CachedImages == 2 && gpu.GetStats().m_ImageUploadBytes == 40,
          "Repeated image uses upload once per source and context");
    auto color = gpu.ReadPixel({30, 25});
    Check(color.m_R > .98f && color.m_G < .02f && color.m_B < .02f, "Decoded top-left is displayed at the top-left");
    color = gpu.ReadPixel({140, 25});
    Check(color.m_G > .98f && color.m_R < .02f, "Decoded right side keeps horizontal orientation");
    color = gpu.ReadPixel({30, 75});
    Check(color.m_B > .98f && color.m_R < .02f, "Decoded bottom row keeps vertical orientation");
    color = gpu.ReadPixel({140, 75});
    Check(std::abs(color.m_R - .502f) < .03f && std::abs(color.m_G - .502f) < .03f,
          "Straight alpha blends exactly once over the framebuffer");
    color = gpu.ReadPixel({240, 50});
    Check(color.m_R > .4f && color.m_R < .6f && color.m_B < .02f,
          "Premultiplied filtering avoids hidden transparent-color fringes");
    render(gpu, 1, false);
    Check(gpu.GetStats().m_RepaintPixels == 0 && gpu.GetStats().m_ImageUploadBytes == 0 &&
              gpu.GetStats().m_DrawCalls == 0,
          "Unchanged images retain pixels and do not upload or draw again");
    Renderer reference(probe.Ui().GetFont());
    reference.SetPartialRedrawEnabled(false);
    for (int frame = 0; frame < 6; ++frame)
    {
        render(gpu, 1, frame % 2 != 0);
        const auto pixels = Pixels();
        render(reference, 1, frame % 2 != 0);
        Check(pixels == Pixels(), "Retained image frames exactly match full-redraw composition");
    }
    render(gpu, 2, false);
    Check(gpu.ReadPixel({30, 25}).m_R > .98f && gpu.ReadPixel({30, 75}).m_B > .98f,
          "Image coordinates and clipping survive two-times physical scale");
    gpu.SavePpm(output / "image-primitives-2x.ppm");
    const auto red = Solid(255, 0, 0), green = Solid(0, 255, 0, 4, 2);
    const Icon densities{red, green};
    for (int density : {1, 2})
    {
        gpu.BeginFrame(320, 240, 320 * density, 240 * density, {0, 0, 0, 1});
        gpu.DrawIcon({10, 10, 2, 2}, densities, clip);
        gpu.EndFrame();
        const auto sample = gpu.ReadPixel({11, 11});
        Check(density == 1 ? sample.m_R > .98f : sample.m_G > .98f,
              "Actual framebuffer density selects the matching source image");
    }
    gpu.SetImageCacheLimit(16);
    for (const auto* next : {&red, &green, &red})
    {
        gpu.BeginFrame(320, 240, 320, 240, {0, 0, 0, 1});
        if (next == &green)
        {
            gpu.DrawImage({10, 10, 30, 30}, *next, clip);
            Throws([&] { gpu.EndFrame(); }, "A visible resource larger than the budget fails explicitly");
            continue;
        }
        gpu.DrawImage({10, 10, 30, 30}, *next, clip);
        gpu.EndFrame();
        Check(gpu.ReadPixel({20, 20}).m_R > .98f, "Renderer recovers at the next frame after a budget failure");
    }
    const auto blue = Solid(0, 0, 255);
    for (const auto* next : {&red, &blue, &red})
    {
        gpu.BeginFrame(320, 240, 320, 240, {0, 0, 0, 1});
        gpu.DrawImage({10, 10, 30, 30}, *next, clip);
        gpu.EndFrame();
        Check(gpu.GetStats().m_CachedImages == 1 && gpu.GetStats().m_ImageBytes == 16,
              "Unused textures retire within a one-image working budget");
        Check(next == &red ? gpu.ReadPixel({20, 20}).m_R > .98f : gpu.ReadPixel({20, 20}).m_B > .98f,
              "Eviction/reload never reuses stale retained pixels or texture identity");
    }
    gpu.SetImageCacheLimit(1024 * 1024);
    {
        auto temporary = Solid(255, 255, 0);
        gpu.BeginFrame(320, 240, 320, 240, {0, 0, 0, 1});
        gpu.DrawImage({10, 10, 30, 30}, temporary, clip);
        temporary = {};
        gpu.EndFrame();
        Check(gpu.ReadPixel({20, 20}).m_R > .98f, "Recorded image owns pixels until deferred upload completes");
    }
    gpu.BeginFrame(320, 240, 320, 240, {0, 0, 0, 1});
    gpu.EndFrame();
    Check(gpu.GetStats().m_CachedImages == 0, "Expired CPU resources retire their textures at a safe context boundary");
    GLuint unpack = 0;
    glGenBuffers(1, &unpack);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, unpack);
    glBufferData(GL_PIXEL_UNPACK_BUFFER, 16, nullptr, GL_STATIC_DRAW);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 8);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 17);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, 3);
    glPixelStorei(GL_UNPACK_SKIP_ROWS, 2);
    gpu.BeginFrame(320, 240, 320, 240, {0, 0, 0, 1});
    gpu.DrawImage({10, 10, 30, 30}, red, clip);
    gpu.EndFrame();
    GLint binding = 0, rowLength = 0, skipPixels = 0;
    glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &binding);
    glGetIntegerv(GL_UNPACK_ROW_LENGTH, &rowLength);
    glGetIntegerv(GL_UNPACK_SKIP_PIXELS, &skipPixels);
    Check(binding == static_cast<GLint>(unpack) && rowLength == 17 && skipPixels == 3,
          "Managed image upload restores the caller's unpack PBO and pixel-store settings");
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
    glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
    glDeleteBuffers(1, &unpack);
    Check(gpu.ReadPixel({20, 20}).m_R > .98f, "PBO/pixel-store settings cannot corrupt uploaded pixels");

    {
        Window second(options, assets / "NotoSans.ttf");
        second.RenderFrame();
        auto& independent = second.Graphics();
        independent.BeginFrame(320, 240, 320, 240, {0, 0, 0, 1});
        independent.DrawImage({10, 10, 30, 30}, red, clip);
        independent.EndFrame();
        Check(independent.GetStats().m_ImageUploadBytes == 16 && independent.ReadPixel({20, 20}).m_R > .98f,
              "A shared CPU resource acquires independent residency in a second native context");
    }
    auto& ui = probe.Ui();
    std::string error;
    Check(ui.SetStyleSheet("window{background:#101b2b;padding:0;} imageview{border-radius:12px;object-fit:cover;} "
                           "button{background:#28415a;color:white;border-radius:8px;padding:6px;text-align:center;} "
                           "button::image-icon{width:24px;height:24px;gap:8px;}",
                           error),
          "Image probe CSS parses");
    auto& view = ui.Root().Add<ImageView>(image);
    view.SetFloatingBounds({20, 20, 180, 180});
    auto& button = ui.Root().Add<Button>("Inspect sketch");
    button.SetFloatingBounds({20, 230, 250, 44});
    button.SetImageIcon(
        Icon{Image::Load(assets / "images/sketch-icon.png"), Image::Load(assets / "images/sketch-icon-2x.png", 2)});
    auto& menu = ui.Root().Add<MenuItem>("Export preview");
    menu.SetImageIcon(button.GetImageIcon());
    menu.SetFloatingBounds({20, 285, 280, 44});
    probe.RenderFrame(ui.GetTime());
    Check(probe.Graphics().ReadPixel({35, 35}).m_R > .9f,
          "ImageView cover crops its source through the actual widget renderer");
    const auto measures = ui.GetStats().m_Measurements;
    view.SetAlignment(1, 0);
    probe.RenderFrame(ui.GetTime());
    Check(probe.Graphics().ReadPixel({100, 35}).m_G > .9f, "CSS image position changes the visible cover crop");
    Check(ui.GetStats().m_Measurements == measures, "Image positioning retains unrelated measurement caches");
    Check(!ui.HasAnimations(), "Static image controls do not request continuous frames");
    probe.Graphics().SavePpm(output / "image-widgets.ppm");
    std::ofstream report(output / "image-report.txt");
    report << "Image resources / actual GPU: PASS\nPNG orientation and alpha, premultiplied filtering, painter order, "
              "retained/full exact pixels, clipping/transforms, 2x density selection, bounded eviction/reload, "
              "recorded/expired resource lifetime, budget failure recovery, ImageView cover positioning, button/menu "
              "icons.\n";
}
} // namespace EStudioUI
