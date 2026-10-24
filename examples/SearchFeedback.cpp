#include <EStudioUI/BackgroundTasks.h>
#include <EStudioUI/Bindings.h>
#include <EStudioUI/CommandPalette.h>
#include <EStudioUI/LogView.h>
#include <EStudioUI/NotificationCenter.h>
#include <EStudioUI/UiContext.h>
using namespace EStudioUI;

//! Composes application-owned state and services; all receivers expire through typed handles.
void BuildSearch(Widget& parent, BackgroundTasks& workers, State<std::string>& query,
                 std::vector<ScopedBinding>& bindings)
{
    auto& ui = *parent.GetContext();
    auto& search = parent.Add<SearchBox>();
    bindings.emplace_back(BindText(search, query));
    const auto handle = ui.GetHandle(search);
    search.QueryRequested().Connect(
        [handle, &workers](std::string text, std::uint64_t request)
        {
            workers.Run(
                handle,
                [text = std::move(text)](std::stop_token stop)
                {
                    std::vector<CompletionItem> result;
                    if (!stop.stop_requested() && !text.empty())
                    {
                        result.push_back({"result", text + " result", "Worker-owned text"});
                    }
                    return result;
                },
                [request](SearchBox& live, std::vector<CompletionItem> result)
                { live.SetCompletions(request, std::move(result)); });
        });
    auto& log = parent.Add<LogView>();
    auto sink = log.GetSink();
    sink.Append({"Application ready", "Startup", FeedbackSeverity::Information});
    NotificationCenter::Create(ui).Show("Ready", FeedbackSeverity::Success);
}

int main()
{
    return 0;
}
