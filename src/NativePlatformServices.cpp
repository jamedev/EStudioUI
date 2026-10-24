#include "NativePlatformServices.h"
#include "WindowsClipboard.h"
#include "WindowsFileDialog.h"

#include <EStudioUI/MemoryPlatformServices.h>
#include <stdexcept>
#include <utility>

#ifdef _WIN32
#include <commctrl.h>
#include <dwmapi.h>
#if __has_include(<winrt/Windows.UI.ViewManagement.h>)
#include <winrt/Windows.UI.ViewManagement.h>
#include <roapi.h>
#define ESTUDIOUI_WINRT_APPEARANCE 1
#endif
#endif

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
        m_Window = reinterpret_cast<HWND>(GetHost().m_NativeWindow);
        if (m_Window && !SetWindowSubclass(m_Window, Procedure, reinterpret_cast<UINT_PTR>(this), reinterpret_cast<DWORD_PTR>(this)))
            throw std::runtime_error("Could not observe native appearance changes.");
    }

    ~WindowsServices() override
    {
        if (m_Window)
            RemoveWindowSubclass(m_Window, Procedure, reinterpret_cast<UINT_PTR>(this));
        Close();
    }

  protected:
    PlatformCapabilities DescribeCapabilities() const override
    {
        PlatformCapabilities result;
        result.m_Adapter = "Windows native services";
        result.Set(PlatformFeature::Appearance, PlatformSupport::Native);
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
    HWND m_Window = nullptr;
    bool m_AppearanceDirty = true;

    static LRESULT CALLBACK Procedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR data)
    {
        auto& self = *reinterpret_cast<WindowsServices*>(data);
        if (message == WM_NCDESTROY)
        {
            RemoveWindowSubclass(window, Procedure, reinterpret_cast<UINT_PTR>(&self));
            self.m_Window = nullptr;
        }
        else if (message == WM_SETTINGCHANGE || message == WM_THEMECHANGED || message == WM_SYSCOLORCHANGE ||
                 message == WM_DWMCOLORIZATIONCOLORCHANGED)
        {
            // Never run user callbacks or parse CSS inside a native message.
            self.m_AppearanceDirty = true;
            if (const auto wake = self.GetHost().m_WakeNativeLoop)
                wake();
        }
        return DefSubclassProc(window, message, wParam, lParam);
    }

    SystemAppearance ReadSystemAppearanceCore() const override
    {
        SystemAppearance result;
        result.m_Support = PlatformSupport::Native;
        const auto color = [](COLORREF value)
        {
            return Color{GetRValue(value) / 255.0f, GetGValue(value) / 255.0f, GetBValue(value) / 255.0f, 1};
        };
        result.m_Background = color(GetSysColor(COLOR_WINDOW));
        result.m_Foreground = color(GetSysColor(COLOR_WINDOWTEXT));
        HIGHCONTRASTW contrast{};
        contrast.cbSize = sizeof(contrast);
        if (SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0))
            result.m_HighContrast = (contrast.dwFlags & HCF_HIGHCONTRASTON) != 0;
        BOOL animation = TRUE;
        if (SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animation, 0))
            result.m_ReducedMotion = !animation;
        DWORD accent = 0;
        BOOL opaque = FALSE;
        if (SUCCEEDED(DwmGetColorizationColor(&accent, &opaque)))
        {
            result.m_Accent = {static_cast<float>((accent >> 16) & 255) / 255,
                               static_cast<float>((accent >> 8) & 255) / 255, static_cast<float>(accent & 255) / 255, 1};
            result.m_AccentKnown = true;
        }
#ifdef ESTUDIOUI_WINRT_APPEARANCE
        const auto initialized = RoInitialize(RO_INIT_SINGLETHREADED);
        struct Apartment
        {
            bool m_Owned;
            ~Apartment() { if (m_Owned) RoUninitialize(); }
        } apartment{SUCCEEDED(initialized)};
        if (SUCCEEDED(initialized) || initialized == RPC_E_CHANGED_MODE)
        {
            try
            {
                using namespace winrt::Windows::UI::ViewManagement;
                const UISettings settings;
                const auto foreground = settings.GetColorValue(UIColorType::Foreground);
                const auto background = settings.GetColorValue(UIColorType::Background);
                const auto systemAccent = settings.GetColorValue(UIColorType::Accent);
                const auto convert = [](const auto& c) { return Color{c.R / 255.0f, c.G / 255.0f, c.B / 255.0f, 1}; };
                result.m_Foreground = convert(foreground);
                result.m_Background = convert(background);
                result.m_Accent = convert(systemAccent);
                result.m_AccentKnown = result.m_ModeKnown = true;
                result.m_Dark = 5 * foreground.G + 2 * foreground.R + foreground.B > 8 * 128;
            }
            catch (const winrt::hresult_error&)
            {
                // Older systems still expose classic colors and contrast preferences.
            }
        }
#endif
        if (result.m_HighContrast)
        {
            result.m_Background = color(GetSysColor(COLOR_WINDOW));
            result.m_Foreground = color(GetSysColor(COLOR_WINDOWTEXT));
            result.m_Accent = color(GetSysColor(COLOR_HIGHLIGHT));
            result.m_AccentKnown = result.m_ModeKnown = true;
            result.m_Dark = result.m_Background.m_R + result.m_Background.m_G + result.m_Background.m_B < 1.5f;
        }
        return result;
    }

    std::optional<SystemAppearance> PollSystemAppearanceCore() override
    {
        if (!std::exchange(m_AppearanceDirty, false))
            return {};
        return ReadSystemAppearanceCore();
    }

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
