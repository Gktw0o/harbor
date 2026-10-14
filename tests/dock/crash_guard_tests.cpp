// The dock's crash handlers, in a real crashing process (harbor_dock_crash_helper). The helper hides
// stand-in taskbar windows owned by this test process, then crashes; the handlers must show them again.
// Failures no handler sees (fail-fast, TerminateProcess) are Harbor Shell's watchdog's job, tested in
// Harbor.Core.Tests.

#include <cstdio>
#include <gtest/gtest.h>
#include <string>
#include <wil/resource.h>

#include "fake_taskbar.h"
#include "temp_dir.h"

namespace harbor::dock
{
namespace
{

using namespace std::chrono_literals;
using test::FakeTaskbar;

struct CrashedHelper
{
    wil::unique_process_information process;
    bool hidBeforeCrashing = false;
};

// Starts the helper, waits until it has hidden the windows, then lets it crash.
CrashedHelper RunHelper(const test::TempDir& dir, const FakeTaskbar& fake, std::wstring_view kind,
                        bool handlers = true)
{
    const std::wstring prefix = L"Local\\Harbor.Test." + fake.Id();
    wil::unique_event hidden;
    hidden.create(wil::EventOptions::ManualReset, (prefix + L".hidden").c_str());
    wil::unique_event crash;
    crash.create(wil::EventOptions::ManualReset, (prefix + L".crash").c_str());

    std::wstring commandLine = L"\"" HARBOR_CRASH_HELPER L"\" \"" + dir.Path().wstring() + L"\" " +
                               fake.Id() + L" " + std::wstring(kind) + (handlers ? L"" : L" --no-handlers");
    STARTUPINFOW startup{.cb = sizeof(STARTUPINFOW)};
    CrashedHelper helper;
    if (!CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup,
                        &helper.process))
    {
        ADD_FAILURE() << "can't start the crash helper: error " << GetLastError();
        return helper;
    }
    const HANDLE waits[] = {hidden.get(), helper.process.hProcess};
    helper.hidBeforeCrashing = WaitForMultipleObjects(2, waits, FALSE, 10'000) == WAIT_OBJECT_0 &&
                               !IsWindowVisible(fake.Primary()) && !IsWindowVisible(fake.Secondaries()[0]);
    crash.SetEvent();
    return helper;
}

// How the helper ended so far, for failure messages.
std::string ExitStatus(const CrashedHelper& helper)
{
    DWORD code = 0;
    if (helper.process.hProcess == nullptr || !GetExitCodeProcess(helper.process.hProcess, &code))
    {
        return "unknown";
    }
    char text[32]{};
    std::snprintf(text, sizeof(text), code == STILL_ACTIVE ? "still running" : "exit 0x%08lX",
                  static_cast<unsigned long>(code));
    return text;
}

// Windows Error Reporting may hold a crashed process for a while; the test doesn't wait for that.
void EndHelper(CrashedHelper& helper)
{
    if (helper.process.hProcess != nullptr)
    {
        TerminateProcess(helper.process.hProcess, 1);
        WaitForSingleObject(helper.process.hProcess, 10'000);
    }
}

// Measured on Windows 11 26H1, ARM64: an x64 process built with the Debug CRT (/MDd) and Control Flow Guard,
// running under x64 emulation, fails fast (0xC0000409) on an exception on its main thread before any
// unhandled-exception filter runs. Release-CRT builds, other threads, and native arm64 are unaffected.
// Harbor ships Release builds, and the Shell's watchdog covers what no handler sees anyway.
bool MainThreadFilterSkippedHere()
{
#if defined(_M_X64) && defined(_DEBUG)
    USHORT processMachine = 0;
    USHORT nativeMachine = 0;
    return IsWow64Process2(GetCurrentProcess(), &processMachine, &nativeMachine) &&
           nativeMachine == IMAGE_FILE_MACHINE_ARM64;
#else
    return false;
#endif
}

class CrashGuardTest : public ::testing::TestWithParam<const wchar_t*>
{
};

TEST_P(CrashGuardTest, ACrashShowsTheHiddenTaskbarAgain)
{
    if (std::wstring_view(GetParam()) == L"access-violation" && MainThreadFilterSkippedHere())
    {
        GTEST_SKIP() << "x64 Debug CRT + CFG under emulation fails fast before the filter (see "
                        "MainThreadFilterSkippedHere)";
    }
    test::TempDir dir;
    FakeTaskbar fake(dir.Path());
    auto helper = RunHelper(dir, fake, GetParam());
    ASSERT_TRUE(helper.hidBeforeCrashing);

    EXPECT_TRUE(FakeTaskbar::WaitUntilVisible(fake.Primary(), true, 10s)) << "helper: " << ExitStatus(helper);
    EXPECT_TRUE(FakeTaskbar::WaitUntilVisible(fake.Secondaries()[0], true, 10s));
    EndHelper(helper);
}

INSTANTIATE_TEST_SUITE_P(Kinds, CrashGuardTest,
                         ::testing::Values(L"access-violation", L"access-violation-on-thread", L"abort",
                                           L"terminate", L"invalid-parameter", L"purecall"),
                         [](const auto& info) {
                             std::string name;
                             for (const wchar_t* c = info.param; *c != L'\0'; ++c)
                             {
                                 if (*c != L'-')
                                 {
                                     name.push_back(static_cast<char>(*c));
                                 }
                             }
                             return name;
                         });

// The control case: without the handlers the same crash leaves the windows hidden, so the tests above pass
// because of the handlers and nothing else.
TEST(CrashGuard, WithoutTheHandlersACrashLeavesTheTaskbarHidden)
{
    test::TempDir dir;
    FakeTaskbar fake(dir.Path());
    auto helper = RunHelper(dir, fake, L"access-violation", /*handlers=*/false);
    ASSERT_TRUE(helper.hidBeforeCrashing);

    WaitForSingleObject(helper.process.hProcess, 3'000);
    EXPECT_FALSE(IsWindowVisible(fake.Primary()));
    EndHelper(helper);
}

} // namespace
} // namespace harbor::dock
