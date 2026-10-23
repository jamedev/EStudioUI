#include <EStudioUI/UiDocument.h>
#include <EStudioUI/CanvasWidget.h>
#include <EStudioUI/UiContext.h>

EStudioUI::UiBuildResult LoadDashboard(EStudioUI::UiContext& ui, std::string_view json)
{
    EStudioUI::WidgetRegistry registry;
    auto document = EStudioUI::UiDocument::FromJson(json, registry);
    std::string error;
    if (!ui.SetStyleSheet(document.m_StyleSheet, error))
    {
        throw std::invalid_argument(error);
    }
    return EStudioUI::BuildUiDocument(ui.Root(), document, registry);
}

EStudioUI::UiBuildResult LoadCanvasDashboard(EStudioUI::CanvasWidget& canvas, std::string_view json)
{
    EStudioUI::WidgetRegistry registry;
    const auto document = EStudioUI::UiDocument::FromJson(json, registry);
    return EStudioUI::BuildUiDocument(canvas, EStudioUI::CanvasBounds{0, 0, 600, 400}, document, registry);
}

int main()
{
    return 0;
}
