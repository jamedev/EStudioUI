#include <EStudioUI/MemoryPlatformServices.h>

#include <stdexcept>

namespace EStudioUI
{
MemoryPlatformServices::MemoryPlatformServices(UiDispatcher dispatcher)
    : MemoryPlatformServices(PlatformHost{std::move(dispatcher)})
{
}

MemoryPlatformServices::MemoryPlatformServices(PlatformHost host) : PlatformServices(std::move(host))
{
}

MemoryPlatformServices::~MemoryPlatformServices()
{
    Close();
}

void MemoryPlatformServices::QueueFileDialogResult(FileDialogResult result)
{
    GetDispatcher().VerifyAccess();
    if (IsClosed())
        throw std::logic_error("A closed memory adapter cannot accept scripted results.");
    if (m_Results.size() >= 16)
        throw std::length_error("A memory adapter supports at most sixteen queued picker outcomes.");
    m_Results.push_back(std::move(result));
}

PlatformCapabilities MemoryPlatformServices::DescribeCapabilities() const
{
    PlatformCapabilities result;
    result.m_Adapter = "Deterministic memory services";
    for (const auto feature :
         {PlatformFeature::ClipboardText, PlatformFeature::ClipboardHtml, PlatformFeature::ClipboardFiles,
          PlatformFeature::ClipboardCustom, PlatformFeature::FileDialogs})
        result.Set(feature, PlatformSupport::Emulated);
    return result;
}

ClipboardResult MemoryPlatformServices::ReadClipboardCore()
{
    return {{}, m_Clipboard};
}

PlatformResult MemoryPlatformServices::WriteClipboardCore(const ClipboardData& content)
{
    auto owned = content;
    m_Clipboard = std::move(owned);
    return {};
}

void MemoryPlatformServices::BeginFileDialog(const FileDialogOptions&, std::stop_token stop,
                                             std::function<void(FileDialogResult)> complete)
{
    FileDialogResult result;
    if (!m_Results.empty())
    {
        result = std::move(m_Results.front());
        m_Results.pop_front();
    }
    complete(stop.stop_requested() ? FileDialogResult{FileDialogStatus::Cancelled} : std::move(result));
}
} // namespace EStudioUI
