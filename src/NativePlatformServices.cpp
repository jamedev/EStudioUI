#include "NativePlatformServices.h"
#include "WindowsClipboard.h"
#include "WindowsFileDialog.h"

#include <EStudioUI/MemoryPlatformServices.h>

namespace EStudioUI::Detail
{
namespace
{
#ifdef _WIN32
class WindowsServices final : public PlatformServices
{
  public:
    explicit WindowsServices(PlatformHost host)
        : PlatformServices(std::move(host)),
          m_Dialog(reinterpret_cast<HWND>(GetHost().m_NativeWindow), GetHost().m_WakeNativeLoop)
    {
    }

    ~WindowsServices() override
    {
        Close();
    }

  protected:
    PlatformCapabilities DescribeCapabilities() const override
    {
        PlatformCapabilities result;
        result.m_Adapter = "Windows native services";
        for (const auto feature :
             {PlatformFeature::ClipboardText, PlatformFeature::ClipboardHtml, PlatformFeature::ClipboardFiles,
              PlatformFeature::ClipboardCustom, PlatformFeature::FileDialogs})
            result.Set(feature, PlatformSupport::Native);
        return result;
    }

    ClipboardResult ReadClipboardCore() override
    {
        return ReadWindowsClipboard(reinterpret_cast<HWND>(GetHost().m_NativeWindow));
    }

    PlatformResult WriteClipboardCore(const ClipboardData& data) override
    {
        return WriteWindowsClipboard(reinterpret_cast<HWND>(GetHost().m_NativeWindow), data);
    }

    void BeginFileDialog(const FileDialogOptions& options, std::stop_token stop,
                         std::function<void(FileDialogResult)> complete) override
    {
        m_Dialog.Begin(options, stop, std::move(complete));
    }

    void Closing() override
    {
        m_Dialog.Cancel();
    }

    bool NativeWorkPending() const override
    {
        return m_Dialog.IsRunning();
    }

  private:
    WindowsFileDialog m_Dialog;
};
#else
class TextServices final : public PlatformServices
{
  public:
    explicit TextServices(PlatformHost host) : PlatformServices(std::move(host))
    {
    }

    ~TextServices() override
    {
        Close();
    }

  protected:
    PlatformCapabilities DescribeCapabilities() const override
    {
        PlatformCapabilities result;
        result.m_Adapter = "GLFW native text services";
        if (GetHost().m_GetClipboardText && GetHost().m_SetClipboardText)
            result.Set(PlatformFeature::ClipboardText, PlatformSupport::Native);
        return result;
    }

    ClipboardResult ReadClipboardCore() override
    {
        ClipboardResult result;
        if (GetHost().m_GetClipboardText)
            result.m_Data.m_Text = GetHost().m_GetClipboardText();
        else
            result.m_Result = {PlatformStatus::Unsupported, "Native text clipboard reads are unavailable."};
        return result;
    }

    PlatformResult WriteClipboardCore(const ClipboardData& data) override
    {
        if (!data.m_Text || data.m_Html || !data.m_Files.empty() || !data.m_Custom.empty() ||
            !GetHost().m_SetClipboardText)
            return {PlatformStatus::Unsupported, "This native adapter publishes text-only clipboard values."};
        return GetHost().m_SetClipboardText(*data.m_Text);
    }

    void BeginFileDialog(const FileDialogOptions&, std::stop_token,
                         std::function<void(FileDialogResult)> complete) override
    {
        complete({FileDialogStatus::Unsupported, {}, "Native pickers are not implemented on this platform adapter."});
    }
};
#endif
} // namespace

std::unique_ptr<PlatformServices> CreateNativePlatformServices(PlatformHost host)
{
#ifdef _WIN32
    if (!host.m_NativeWindow)
        return std::make_unique<MemoryPlatformServices>(std::move(host));
    return std::make_unique<WindowsServices>(std::move(host));
#else
    return std::make_unique<TextServices>(std::move(host));
#endif
}
} // namespace EStudioUI::Detail
