#include "CreatorWindow.h"

#include <filesystem>
#include <fstream>
#include <iostream>

int main(int argc, char** argv)
{
    try
    {
        if (argc != 3)
        {
            return 2;
        }
        EStudioUI::WidgetRegistry registry;
        auto sample = EStudioUI::CreatorWindow::ExampleDocument();
        EStudioUI::UiDocument catalog;
        catalog.m_Root.m_Id = "catalog-root";
        catalog.m_Root.m_Layout = "Flow";
        catalog.m_StyleSheet = sample.m_StyleSheet;
        catalog.m_Root.m_Children.push_back(sample.m_Root);
        for (const auto& [type, descriptor] : registry.GetTypes())
        {
            (void)descriptor;
            EStudioUI::UiNode node("catalog-" + type, type, {{"width", 180.0}, {"height", 100.0}});
            catalog.m_Root.m_Children.push_back(std::move(node));
        }
        // Quotes, backslashes, UTF-8 and apparent source syntax remain ordinary label bytes.
        catalog.Find("headline")->m_Properties["text"] = std::string("Quotes: \"; escaped \\ path; UTF-8: ni\xc3\xb1o");
        std::filesystem::create_directories(std::filesystem::path(argv[1]).parent_path());
        std::ofstream source(argv[1], std::ios::binary);
        source << catalog.ToCpp(registry);
        const auto pair = catalog.ToCppClass(registry, "GeneratedComposite");
        const auto folder = std::filesystem::path(argv[1]).parent_path();
        std::ofstream classHeader(folder / "GeneratedComposite.h", std::ios::binary);
        std::ofstream classSource(folder / "GeneratedComposite.cpp", std::ios::binary);
        classHeader << pair.m_Header;
        classSource << pair.m_Source;
        if (!classHeader || !classSource) return 1;
        std::ofstream json(argv[2], std::ios::binary);
        json << catalog.ToJson(registry);
        if (!source || !json)
        {
            return 1;
        }
        std::cout << "Generated direct C++ and JSON for " << registry.GetTypes().size() << " registered widget types\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
