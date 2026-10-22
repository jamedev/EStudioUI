#include <EStudioUI/Application.h>
#include <EStudioUI/ConstraintLayout.h>
#include <EStudioUI/Label.h>
#include <EStudioUI/TextBox.h>
#include <EStudioUI/Window.h>

int main()
{
    using namespace EStudioUI;
    Application app;
    Window window(WindowOptions{}, "assets/NotoSans.ttf");
    auto& root = window.Ui().Root();
    root.SetStyleProperty(StyleProperty::Padding, Insets{16, 16, 16, 16});
    auto& sidebar = root.Add<Label>("Project properties");
    sidebar.SetWidth(SizeRule::Fill(1, 120, 240));
    sidebar.SetHeight(SizeRule::Fixed(36));
    auto& editor = root.Add<TextBox>("A working constrained editor");
    editor.SetWidth(SizeRule::Fill(1, 80));
    editor.SetHeight(SizeRule::Fixed(36));

    const ConstraintItem parent = ConstraintItem::Parent(), side(sidebar), field(editor);
    ConstraintLayout layout;
    layout.Add(side.Left() == parent.Left());
    layout.Add(side.Top() == parent.Top());
    layout.Add(side.Width() == 200, ConstraintStrength::Strong, "sidebar preference");
    layout.Add(field.Left() == side.Right() + 12);
    layout.Add(field.Right() == parent.Right());
    layout.Add(field.Top() == side.Top());
    root.SetLayout(std::move(layout));
    return app.Run();
}
