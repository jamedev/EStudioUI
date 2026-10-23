#include <EStudioUI/UiDocument.h>
#include <EStudioUI/UiContext.h>
#include <EStudioUI/Label.h>

#include <fstream>
#include <iostream>
#include <stdexcept>

EStudioUI::Widget& BuildUi(EStudioUI::Widget& parent);
std::string_view UiStyleSheet();

int main()
{
    using namespace EStudioUI;
    try
    {
        FontFace font(ESTUDIOUI_FONT_PATH);
        UiContext runtime(font), compiled(font);
        std::ifstream input(ESTUDIOUI_GENERATED_JSON);
        const std::string json{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
        WidgetRegistry registry;
        const auto document = UiDocument::FromJson(json, registry);
        std::string error;
        if (!runtime.SetStyleSheet(document.m_StyleSheet, error) || !compiled.SetStyleSheet(UiStyleSheet(), error))
        {
            throw std::runtime_error(error);
        }
        const auto built = BuildUiDocument(runtime.Root(), document, registry);
        auto& actual = BuildUi(compiled.Root());
        runtime.Prepare(1800, 1400);
        compiled.Prepare(1800, 1400);
        std::size_t checks = 0;
        for (const auto& [id, reference] : built.m_Widgets)
        {
            const auto* emitted = actual.FindById(id);
            if (!emitted || !reference.Get() || emitted->GetBounds() != reference.Get()->GetBounds())
            {
                throw std::runtime_error("Generated/runtime widget geometry differs: " + id);
            }
            ++checks;
        }
        const auto* label = dynamic_cast<Label*>(actual.FindById("headline"));
        if (!label || label->GetText() != std::get<std::string>(document.Find("headline")->m_Properties.at("text")))
        {
            throw std::runtime_error("C++ string generation did not preserve exact UTF-8 bytes.");
        }
        for (const auto& [type, descriptor] : registry.GetTypes())
        {
            (void)descriptor;
            if (!actual.FindById("catalog-" + type))
            {
                throw std::runtime_error("Missing generated widget: " + type);
            }
            ++checks;
        }
        std::cout << "Compiled C++ / JSON equivalence: " << checks + 1 << " checks passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
