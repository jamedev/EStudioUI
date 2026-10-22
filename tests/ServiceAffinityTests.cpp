#include <EStudioUI/AnimationSystem.h>
#include <EStudioUI/UiContext.h>

#include <exception>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace
{
using namespace EStudioUI;
int s_Checks = 0;

template <typename Callback> void Rejects(Callback callback)
{
    ++s_Checks;
    std::exception_ptr error;
    std::thread worker(
        [&]
        {
            try
            {
                callback();
            }
            catch (...)
            {
                error = std::current_exception();
            }
        });
    worker.join();
    if (!error)
        throw std::runtime_error("foreign-thread service access was accepted");
    try
    {
        std::rethrow_exception(error);
    }
    catch (const std::logic_error&)
    {
        return;
    }
    throw std::runtime_error("foreign-thread service access used an unexpected error type");
}
} // namespace

int main()
{
    try
    {
        FontFace font(ESTUDIOUI_FONT_PATH);
        UiContext ui(font);
        auto& scheduler = ui.Animations();
        Rejects([&] { font.Shape("owner font", 16); });
        Rejects([&] { font.GetGlyph(1); });
        Rejects([&] { scheduler.GetTime(); });
        Rejects([&] { scheduler.Update(2); });
        Rejects([&] { scheduler.StopAll(); });
        Rejects([&] { scheduler.Finished(); });
        Rejects([&] { UiContext wrongOwner(font); });
        ui.Prepare(300, 200);
        std::cout << "Service affinity PASS / " << s_Checks << " checks\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Service affinity FAIL / " << error.what() << '\n';
        return 1;
    }
}
