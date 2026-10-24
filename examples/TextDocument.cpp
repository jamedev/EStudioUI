#include <EStudioUI/TextDocument.h>

#include <iostream>
#include <thread>

int main()
{
    using namespace EStudioUI;
    UiDispatcher dispatcher;
    StateStore states(dispatcher);
    TextDocument document(states);
    document.Reset("Engineering notes\nBeam A\tLoad case 42\n");

    const auto saved = document.GetSnapshot();
    const auto position = saved.GetPosition(1, saved.GetParagraph(1)->m_Text.size());
    document.Replace(position, position, " — reviewed", "Review beam");
    const auto edited = document.GetSnapshot();
    const auto change = edited.GetChangeFrom(saved);
    std::cout << "Changed paragraph: " << change.m_FirstParagraph << '\n';

    // Immutable values can be retained for worker-side persistence without a UI or GL context.
    std::thread writer([saved] { std::cout << "Saved snapshot:\n" << saved.GetText(); });
    writer.join();
    document.GetHistory().Undo();
    if (!(document.GetSnapshot() == saved))
    {
        return 1;
    }
    document.GetHistory().Redo();
    if (!(document.GetSnapshot() == edited))
    {
        return 1;
    }

    // Workers request live changes through the owner dispatcher; they do not mutate document storage.
    const auto endpoint = document.GetHandle();
    std::thread producer([endpoint]
                         { endpoint.Post([](TextDocument& live) { live.SetText("Owner-side queued edit\n"); }); });
    producer.join();
    dispatcher.Drain();
    std::cout << "Current snapshot:\n" << document.GetSnapshot().GetText();
    return 0;
}
