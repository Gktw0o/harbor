// The AppBar worker thread with a fake port: the state machine itself is tested in appbar_state_tests.cpp.

#include <windows.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <gtest/gtest.h>
#include <memory>
#include <mutex>
#include <optional>
#include <wil/resource.h>

#include "app/appbar_worker.h"

namespace harbor::dock
{
namespace
{

using namespace std::chrono_literals;
using geometry::Rect;

// Grants what it's asked; counts calls. Remove can be made to hang, like a call into a hung Explorer.
struct Calls
{
    std::atomic<int> registers{0};
    std::atomic<int> removes{0};
    std::atomic<int> queries{0};
    std::atomic<int> sets{0};
    wil::unique_event hangRemove{wil::EventOptions::ManualReset};
    wil::unique_event releaseRemove{wil::EventOptions::ManualReset};
};

class FakeAppBar final : public appbar::IAppBar
{
  public:
    explicit FakeAppBar(std::shared_ptr<Calls> calls) : m_calls(std::move(calls))
    {
    }
    bool Register() override
    {
        ++m_calls->registers;
        return true;
    }
    void Remove() override
    {
        ++m_calls->removes;
        if (m_calls->hangRemove.is_signaled())
        {
            m_calls->releaseRemove.wait();
        }
    }
    Rect Query(const Rect& proposed) override
    {
        ++m_calls->queries;
        return proposed;
    }
    Rect Set(const Rect& proposed) override
    {
        ++m_calls->sets;
        return proposed;
    }

  private:
    std::shared_ptr<Calls> m_calls;
};

// The granted strips, as the worker reports them on its thread.
class Granted
{
  public:
    void Add(const Rect& strip)
    {
        std::lock_guard lock(m_mutex);
        m_last = strip;
        ++m_count;
    }
    std::optional<Rect> Last()
    {
        std::lock_guard lock(m_mutex);
        return m_last;
    }
    int Count()
    {
        std::lock_guard lock(m_mutex);
        return m_count;
    }

  private:
    std::mutex m_mutex;
    std::optional<Rect> m_last;
    int m_count = 0;
};

bool WaitFor(const std::function<bool()>& condition, std::chrono::milliseconds timeout = 5s)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!condition())
    {
        if (std::chrono::steady_clock::now() > deadline)
        {
            return false;
        }
        Sleep(10);
    }
    return true;
}

constexpr Rect kMonitor{0, 0, 1920, 1080};

TEST(AppBarWorker, ReservesTheStripOnItsThreadAndReportsIt)
{
    auto calls = std::make_shared<Calls>();
    auto granted = std::make_shared<Granted>();
    AppBarWorker worker(std::make_unique<FakeAppBar>(calls),
                        [granted](const Rect& strip) { granted->Add(strip); });

    worker.SetWant({kMonitor, 80});

    ASSERT_TRUE(WaitFor([&] { return granted->Last().has_value(); }));
    EXPECT_EQ(granted->Last(), (Rect{0, 1000, 1920, 1080}));
    EXPECT_EQ(calls->registers, 1);
    EXPECT_EQ(calls->sets, 1);
}

// Explorer's bursts of ABN_POSCHANGED cost a query or two and no new reservation or report.
TEST(AppBarWorker, ABurstOfNotificationsCostsLittle)
{
    auto calls = std::make_shared<Calls>();
    auto granted = std::make_shared<Granted>();
    AppBarWorker worker(std::make_unique<FakeAppBar>(calls),
                        [granted](const Rect& strip) { granted->Add(strip); }, {.minInterval = 100ms});
    worker.SetWant({kMonitor, 80});
    ASSERT_TRUE(WaitFor([&] { return granted->Count() == 1; }));

    for (int i = 0; i < 20; ++i)
    {
        worker.PositionMayHaveChanged();
    }
    // The rate limit's timer brings the last of them in.
    EXPECT_TRUE(WaitFor([&] { return calls->queries >= 2; }));
    Sleep(300);
    EXPECT_LE(calls->queries, 3);
    EXPECT_EQ(calls->sets, 1);
    EXPECT_EQ(granted->Count(), 1);
}

TEST(AppBarWorker, StoppingRemovesTheStrip)
{
    auto calls = std::make_shared<Calls>();
    auto granted = std::make_shared<Granted>();
    AppBarWorker worker(std::make_unique<FakeAppBar>(calls),
                        [granted](const Rect& strip) { granted->Add(strip); });
    worker.SetWant({kMonitor, 80});
    ASSERT_TRUE(WaitFor([&] { return granted->Count() == 1; }));

    worker.Stop();
    EXPECT_EQ(calls->removes, 1);
    worker.Stop();
    EXPECT_EQ(calls->removes, 1);
}

// A hung Explorer must not keep the dock from exiting: the worker is left behind.
TEST(AppBarWorker, StoppingGivesUpOnAHungExplorer)
{
    auto calls = std::make_shared<Calls>();
    auto granted = std::make_shared<Granted>();
    {
        AppBarWorker worker(std::make_unique<FakeAppBar>(calls),
                            [granted](const Rect& strip) { granted->Add(strip); });
        worker.SetWant({kMonitor, 80});
        ASSERT_TRUE(WaitFor([&] { return granted->Count() == 1; }));
        calls->hangRemove.SetEvent();

        const auto started = std::chrono::steady_clock::now();
        worker.Stop(300ms);
        const auto waited = std::chrono::steady_clock::now() - started;
        EXPECT_GE(waited, 250ms);
        EXPECT_LT(waited, 3s);
        EXPECT_EQ(calls->removes, 1);
    }
    // The abandoned thread still owns its port: let it finish.
    calls->releaseRemove.SetEvent();
}

} // namespace
} // namespace harbor::dock
