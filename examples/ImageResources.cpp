#include <EStudioUI/Application.h>
#include <EStudioUI/Button.h>
#include <EStudioUI/ImageView.h>
#include <EStudioUI/Window.h>

//! A complete image-loading flow using the framework's existing receiver-bound task API.
int main(int argc, char** argv)
{
    if (argc < 3)
        return 0;
    EStudioUI::Application app;
    EStudioUI::Window window({}, argv[1]);
    auto& preview = window.Ui().Root().Add<EStudioUI::ImageView>();
    preview.SetHeight(EStudioUI::SizeRule::Fill());
    preview.SetAccessibleName("Image preview");
    auto& status = window.Ui().Root().Add<EStudioUI::Button>("Loading image...");
    const auto statusRef = EStudioUI::WidgetRef(&status);
    EStudioUI::UiTaskOptions<EStudioUI::ImageView> options;
    options.m_Error = [statusRef](EStudioUI::ImageView&, std::exception_ptr)
    {
        if (auto* live = dynamic_cast<EStudioUI::Button*>(statusRef.Get()))
            live->SetText("Image could not be loaded");
    };
    const auto path = std::filesystem::path(argv[2]);
    auto loading = app.Tasks().Run(
        window.Ui().GetHandle(preview), [path] { return EStudioUI::Image::Load(path); },
        [statusRef](EStudioUI::ImageView& live, EStudioUI::Image image)
        {
            live.SetImage(std::move(image));
            if (auto* label = dynamic_cast<EStudioUI::Button*>(statusRef.Get()))
                label->SetText("Image loaded");
        },
        std::move(options));
    return app.Run();
}
