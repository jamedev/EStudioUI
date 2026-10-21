#include <EStudioUI/CanvasWidget.h>
#include <EStudioUI/SketchWidget.h>
#include <EStudioUI/Button.h>
#include <EStudioUI/UiContext.h>

#include <cmath>
#include <iostream>
#include <sstream>
#include <stdexcept>

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

bool Near(double a, double b, double tolerance = 0.002)
{
    return std::abs(a - b) < tolerance;
}

void Drag(UiContext& ui, Point from, Point to, Modifiers modifiers = {})
{
    ui.PointerDown(from, modifiers);
    ui.PointerMove(to, modifiers);
    ui.PointerUp(to, modifiers);
}

void CameraAndHosts(FontFace& font)
{
    UiContext ui(font);
    auto& canvas = ui.Root().Add<CanvasWidget>();
    canvas.SetWidth(SizeRule::Fill());
    canvas.SetHeight(SizeRule::Fill());
    ui.Prepare(800, 600);
    canvas.SetCamera({1e12, -1e12}, 8);
    const CanvasPoint world{1e12 + 12.25, -1e12 - 14.5};
    const auto screen = canvas.ToScreen(world);
    const auto restored = canvas.ToWorld(screen);
    Check(Near(restored.m_X, world.m_X) && Near(restored.m_Y, world.m_Y),
          "camera-relative doubles retain small offsets at large coordinates");
    canvas.SetCamera({}, 1);
    Drag(ui, {400, 300}, {440, 320}, {false, false, true});
    Check(Near(canvas.GetCenter().m_X, -40) && Near(canvas.GetCenter().m_Y, -20), "Alt drag pans in scene coordinates");
    ui.PointerMove({300, 200});
    const auto anchor = canvas.ToWorld({300, 200});
    ui.Scroll({300, 200}, {0, 2});
    const auto zoomAnchor = canvas.ToWorld({300, 200});
    Check(canvas.GetZoom() > 1 && Near(anchor.m_X, zoomAnchor.m_X) && Near(anchor.m_Y, zoomAnchor.m_Y),
          "wheel zoom preserves the pointer world position");
    const auto directAnchor = canvas.ToWorld({600, 450});
    ui.Scroll({600, 450}, {0, -1});
    const auto afterDirectScroll = canvas.ToWorld({600, 450});
    Check(Near(directAnchor.m_X, afterDirectScroll.m_X) && Near(directAnchor.m_Y, afterDirectScroll.m_Y),
          "wheel uses its actual event position without requiring a pointer move first");
    canvas.SetCamera({}, 1);
    auto button = std::make_unique<Button>("Hosted");
    auto* buttonPtr = button.get();
    int clicks = 0;
    buttonPtr->Clicked().Connect([&] { ++clicks; });
    canvas.HostWidget(std::move(button), {-50, -20, 100, 40});
    ui.Prepare(800, 600);
    Drag(ui, {400, 300}, {400, 300});
    Check(clicks == 1, "hosted regular button receives its normal input");
    const auto hostedAnchor = canvas.ToWorld({420, 300});
    ui.Scroll({420, 300}, {0, 1});
    const auto afterHostedScroll = canvas.ToWorld({420, 300});
    Check(Near(hostedAnchor.m_X, afterHostedScroll.m_X) && Near(hostedAnchor.m_Y, afterHostedScroll.m_Y),
          "wheel bubbling through a hosted widget keeps its pointer anchor");
    canvas.SetCamera({200, 100}, 2);
    ui.Prepare(800, 600);
    const auto bounds = buttonPtr->GetBounds();
    Check(Near(bounds.m_X, -100) && Near(bounds.m_Y, 60) && Near(bounds.m_Width, 200),
          "hosted widget geometry follows camera and zoom");
    Check(canvas.RemoveHostedWidget(*buttonPtr), "hosted child can be removed safely");
    canvas.SetCamera({}, 1);
    ui.Prepare(800, 600);
    std::string error;
    Check(canvas.SetItemStyleSheet("canvas-item { color: red; border-width: 2px; }", error), "item CSS compiles");
    Check(!canvas.SetItemStyleSheet("canvas-item { color: definitely-not-a-color; }", error),
          "invalid item CSS rejects atomically");
    Check(!ui.HasAnimations(), "static canvas requests no continuous frames");
    canvas.GetScene().Add({CanvasPrimitive::Line, {-10, 0}, {10, 0}, 0});
    canvas.SceneChanged();
    Check(canvas.GetVisibleItemCount() == 1, "visible query caches one entity");
    canvas.SetFloatingBounds({900, 0, 100, 100});
    ui.Prepare(800, 600);
    Check(canvas.GetVisibleItemCount() == 0, "clipped viewport empties visible query");
    canvas.ClearFloatingBounds();
    ui.Prepare(800, 600);
    Check(canvas.GetVisibleItemCount() == 1, "restoring identical view repopulates culled cache");
}

void SketchInput(FontFace& font)
{
    UiContext ui(font);
    auto& sketch = ui.Root().Add<SketchWidget>();
    sketch.SetWidth(SizeRule::Fill());
    sketch.SetHeight(SizeRule::Fill());
    ui.Prepare(800, 600);
    sketch.SetCamera({}, 1);
    sketch.SetSnapping(false);
    sketch.SetTool(SketchTool::Line);
    Drag(ui, {300, 250}, {500, 350});
    Check(sketch.GetDocument().GetScene().Size() == 1, "line drag creates one model entity");
    sketch.Undo();
    Check(sketch.GetDocument().GetScene().Size() == 0, "toolbar-equivalent undo removes creation");
    sketch.Redo();
    Check(sketch.GetDocument().GetScene().Size() == 1, "redo restores creation");
    sketch.SetTool(SketchTool::Rectangle);
    ui.PointerDown({250, 200});
    ui.PointerMove({350, 250});
    ui.KeyEvent(Key::Escape, true);
    ui.PointerUp({350, 250});
    Check(sketch.GetDocument().GetScene().Size() == 1, "Escape cancels preview without committing a shape");
    sketch.SetTool(SketchTool::Select);
    Drag(ui, {400, 300}, {420, 320});
    Check(sketch.GetDocument().GetSelection().size() == 1, "pointer picks and moves an outline");
    const auto item = sketch.GetDocument().GetScene().Get(sketch.GetDocument().GetSelection().front());
    Check(item && Near(item->m_Geometry.m_A.m_X, -80) && Near(item->m_Geometry.m_A.m_Y, -30),
          "one drag commits the expected model translation");
    ui.KeyEvent(Key::Delete, true);
    Check(sketch.GetDocument().GetScene().Size() == 0, "Delete removes selected geometry");
    ui.KeyEvent(Key::Z, true, false, {false, true, false});
    Check(sketch.GetDocument().GetScene().Size() == 1, "Ctrl+Z restores deletion");
    const auto before = sketch.GetDocument().GetScene().Size();
    sketch.SetTool(SketchTool::Circle);
    Drag(ui, {400, 300}, {450, 300}, {false, false, true});
    Check(sketch.GetDocument().GetScene().Size() == before, "Alt panning does not create a circle");
    std::stringstream saved;
    sketch.GetDocument().Save(saved);
    const auto* sceneAddress = &sketch.GetScene();
    const auto oldId = sketch.GetScene().GetItems().front().m_Id;
    sketch.GetDocument().Load(saved);
    sketch.SceneChanged();
    ui.Prepare(800, 600);
    Check(&sketch.GetScene() == sceneAddress && sketch.GetScene().Size() == before,
          "loading preserves the viewport's scene alias");
    Check(!sketch.GetScene().Get(oldId), "loading invalidates previously issued scene handles");
}

void CallbackLifetimes(FontFace& font)
{
    UiContext ui(font);
    auto& canvas = ui.Root().Add<CanvasWidget>();
    const WidgetRef canvasRef(&canvas);
    canvas.ViewChanged().Connect([&] { ui.Root().RemoveChild(canvas); });
    ui.Prepare(800, 600);
    Drag(ui, {400, 300}, {420, 300}, {false, false, true});
    Check(!canvasRef.Get() && !ui.GetFocus(), "camera callback may delete its canvas during captured pan");
    auto& sketch = ui.Root().Add<SketchWidget>();
    const WidgetRef sketchRef(&sketch);
    sketch.Changed().Connect([&] { ui.Root().RemoveChild(sketch); });
    ui.Prepare(800, 600);
    ui.PointerDown({400, 300});
    ui.PointerUp({400, 300});
    Check(!sketchRef.Get() && !ui.GetFocus(), "selection callback may delete its sketch during pointer delivery");
}
} // namespace

int main()
{
    try
    {
        FontFace font(ESTUDIOUI_FONT_PATH);
        CameraAndHosts(font);
        SketchInput(font);
        CallbackLifetimes(font);
        std::cout << "Canvas widget PASS / " << s_Checks << " checks\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Canvas widget FAIL / " << error.what() << '\n';
        return 1;
    }
}
