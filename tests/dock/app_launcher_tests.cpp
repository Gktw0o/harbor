// Launching through the real shell: the marker app (marker_app.cpp) leaves a file when it starts.

#include <windows.h>

#include <objbase.h>

#include <chrono>
#include <filesystem>
#include <gtest/gtest.h>
#include <thread>
#include <wil/resource.h>

#include "adapters/app_launcher.h"
#include "temp_dir.h"

namespace harbor::adapters
{
namespace
{

using namespace std::chrono_literals;

TEST(AppLauncher, AnAppOpensThroughTheAppsFolder)
{
    EXPECT_EQ(LaunchTarget({ipc::DockItemKind::App, "Microsoft.WindowsTerminal_8wekyb3d8bbwe!App", "x", {}}),
              L"shell:AppsFolder\\Microsoft.WindowsTerminal_8wekyb3d8bbwe!App");
}

TEST(AppLauncher, APathOpensAsItIs)
{
    EXPECT_EQ(LaunchTarget({ipc::DockItemKind::Path, "C:\\Ünïcødé ğ\\app.exe", "x", {}}),
              L"C:\\\u00dcn\u00efc\u00f8d\u00e9 \u011f\\app.exe");
}

// The route depends on the machine (Explorer refuses on some CI runners); either must start the app.
TEST(AppLauncher, StartsAPinnedFile)
{
    const auto com = wil::CoInitializeEx_failfast(COINIT_APARTMENTTHREADED);
    test::TempDir dir(L" \u00dcn\u00efc\u00f8d\u00e9");
    const auto app = dir.Path() / L"marker app.exe";
    std::filesystem::copy_file(HARBOR_MARKER_APP, app);
    const auto marker = std::filesystem::path(app.wstring() + L".launched");

    const auto utf8 = app.u8string();
    const auto result =
        LaunchApp({ipc::DockItemKind::Path, std::string(utf8.begin(), utf8.end()), "marker", {}});

    ASSERT_TRUE(SUCCEEDED(result.result))
        << std::hex << result.result << " explorer " << result.explorerResult;
    const auto deadline = std::chrono::steady_clock::now() + 15s;
    while (!std::filesystem::exists(marker) && std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::sleep_for(50ms);
    }
    EXPECT_TRUE(std::filesystem::exists(marker));
    // The marker app may still hold its own file for a moment; TempDir's cleanup tolerates that.
    std::this_thread::sleep_for(200ms);
}

} // namespace
} // namespace harbor::adapters
