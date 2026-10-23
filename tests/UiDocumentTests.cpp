#include <EStudioUI/UiDocument.h>
#include <EStudioUI/UiContext.h>
#include <EStudioUI/Button.h>

#include <iostream>
#include <stdexcept>

namespace
{
using namespace EStudioUI;
int s_Checks = 0;

void Check(bool condition, const char* message)
{
    ++s_Checks;
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

template <typename F> void Throws(F action, const char* message)
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
} // namespace

int main()
{
    using namespace EStudioUI;
    try
    {
        WidgetRegistry registry;
        UiDocument document;
        document.m_Root.m_Layout = "Constraints";
        document.m_Root.m_Children.emplace_back(
            "accept", "Button",
            std::map<std::string, PropertyValue>{{"text", std::string("Accept")}, {"width", 120.0}, {"height", 36.0}});
        UiConstraint left;
        left.m_Item = "accept";
        left.m_Offset = 20;
        left.m_Name = "left margin";
        UiConstraint top = left;
        top.m_Anchor = top.m_TargetAnchor = "Top";
        top.m_Offset = 30;
        document.m_Root.m_Constraints = {left, top};
        const auto json = document.ToJson(registry);
        Check(UiDocument::FromJson(json, registry) == document,
              "Open format round trips widget properties and named constraints");
        auto duplicateId = document;
        duplicateId.m_Root.m_Children.push_back(duplicateId.m_Root.m_Children.front());
        Throws([&] { duplicateId.Validate(registry); }, "Duplicate design IDs reject");
        Throws([&] { UiDocument::FromJson("{\"format\":\"EStudioUI\",\"version\":1,\"version\":2}", registry); },
               "Duplicate JSON keys reject rather than overriding silently");
        Throws([&] { UiDocument::FromJson("{\"format\":\"EStudioUI\",\"version\":99,\"root\":{}}", registry); },
               "Unsupported document versions reject");
        Throws([&] { UiDocument::FromJson(std::string(4 * 1024 * 1024 + 1, ' '), registry); },
               "Input byte limits precede JSON parsing");
        auto bad = document;
        bad.Find("accept")->m_Properties["surprise"] = true;
        Throws([&] { bad.Validate(registry); }, "Unknown widget properties reject with diagnostics");
        bad = document;
        bad.Find("accept")->m_Type = "QPushButton";
        Throws([&] { bad.Validate(registry); }, "Unknown types never become silent panel placeholders");
        bad = document;
        bad.m_Root.m_Constraints.front().m_Target = "absent";
        Throws([&] { bad.Validate(registry); }, "Dangling constraint references reject before construction");
        bad = document;
        bad.m_Root.m_Children.front().m_Children.emplace_back("nested", "Label");
        Throws([&] { bad.Validate(registry); }, "Leaf adapters reject unsupported content ownership");
        bad = document;
        bad.m_Root.m_Id = "bad id; injection";
        Throws([&] { bad.ToCpp(registry); }, "Invalid selector identities reject before source generation");
        bad = document;
        bad.Find("accept")->m_Properties["text"] = std::string("line\nbreak");
        Throws([&] { bad.Validate(registry); }, "Single-line property text cannot silently normalize on construction");
        auto fractional = json;
        const auto columns = fractional.find("\"columns\": 2");
        fractional.replace(columns, std::string("\"columns\": 2").size(), "\"columns\": 2.5");
        Throws([&] { UiDocument::FromJson(fractional, registry); },
               "Fractional grid column counts reject instead of truncating");
        FontFace font(ESTUDIOUI_FONT_PATH);
        UiContext ui(font);
        auto& parent = ui.Root().Add<Widget>();
        parent.SetWidth(SizeRule::Fill());
        parent.SetHeight(SizeRule::Fill());
        auto built = BuildUiDocument(parent, document, registry);
        ui.Prepare(600, 400);
        auto* button = dynamic_cast<Button*>(built.m_Widgets.at("accept").Get());
        Check(button && button->GetText() == "Accept", "Runtime documents construct real typed controls");
        Check(button->GetBounds().m_Width == 120 && button->GetBounds().m_Height == 36,
              "Explicit size contracts survive runtime construction");
        Check(button->GetBounds().m_X >= 20 && button->GetBounds().m_Y >= 30,
              "Named anchor constraints reach the production layout solver");
        const auto children = parent.GetChildren().size();
        Throws([&] { BuildUiDocument(parent, bad, registry); }, "Invalid runtime documents reject");
        Check(parent.GetChildren().size() == children, "Invalid builds leave the host tree unchanged");
        UiWidgetDescriptor custom;
        custom.m_Type = "CustomControl";
        custom.m_Category = "Application";
        custom.m_CppType = "CustomControl";
        custom.m_Include = "CustomControl.h";
        custom.m_Create = []() -> std::unique_ptr<Widget> { throw std::runtime_error("Injected factory failure"); };
        registry.Register(std::move(custom));
        bad = document;
        bad.m_Root.m_Children.emplace_back("failure", "CustomControl");
        Throws([&] { BuildUiDocument(parent, bad, registry); }, "Application factory failures propagate usefully");
        Check(parent.GetChildren().size() == children,
              "Factory failure destroys the staged tree and preserves the host");
        Throws([&] { registry.Register(registry.Find("Button")); }, "Duplicate type registrations reject");
        Check(UiCppString("\"\\\n") == "\"\\\"\\\\\\012\"",
              "C++ escaping preserves quotes, backslashes and line breaks");
        Check(document.ToCpp(registry).find(".Add<Button>()") != std::string::npos,
              "C++ output uses ordinary templated construction");
        std::cout << "UI document: " << s_Checks << " checks passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
