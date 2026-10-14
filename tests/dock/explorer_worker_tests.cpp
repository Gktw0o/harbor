#include <atomic>
#include <gtest/gtest.h>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "app/explorer_worker.h"

namespace harbor::dock
{
namespace
{

using namespace std::chrono_literals;

TEST(ExplorerWorker, RunsAPostedJob)
{
    ExplorerWorker worker;
    std::atomic<bool> ran{false};
    EXPECT_TRUE(worker.PostAndWait([&] { ran = true; }, 5s));
    EXPECT_TRUE(ran);
}

// A restore posted while a hide still waits replaces it.
TEST(ExplorerWorker, OnlyTheNewestPendingJobRuns)
{
    ExplorerWorker worker;
    std::atomic<bool> busy{false};
    std::atomic<bool> release{false};
    std::atomic<bool> newestDone{false};
    std::mutex mutex;
    std::vector<std::string> ran;
    const auto record = [&](std::string name) {
        std::lock_guard lock(mutex);
        ran.push_back(std::move(name));
    };

    worker.Post([&] {
        busy = true;
        while (!release)
        {
            std::this_thread::yield();
        }
        record("first");
    });
    while (!busy)
    {
        std::this_thread::yield();
    }
    // Both are posted while the first job runs, so the second replaces the first of them.
    worker.Post([&] { record("replaced"); });
    worker.Post([&] {
        record("newest");
        newestDone = true;
    });
    release = true;

    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (!newestDone && std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::yield();
    }
    std::lock_guard lock(mutex);
    EXPECT_EQ(ran, (std::vector<std::string>{"first", "newest"}));
}

TEST(ExplorerWorker, PostAndWaitGivesUpOnAStuckJob)
{
    ExplorerWorker worker;
    std::atomic<bool> busy{false};
    std::atomic<bool> release{false};
    worker.Post([&] {
        busy = true;
        while (!release)
        {
            std::this_thread::yield();
        }
    });
    // Until the stuck job has started, a new post would simply replace it.
    while (!busy)
    {
        std::this_thread::yield();
    }

    EXPECT_FALSE(worker.PostAndWait([] {}, 100ms));
    release = true;
}

TEST(ExplorerWorker, StoppingRunsThePendingJob)
{
    std::atomic<bool> ran{false};
    {
        ExplorerWorker worker;
        worker.Post([&] { ran = true; });
    }
    EXPECT_TRUE(ran);
}

TEST(ExplorerWorker, KeepsGoingAfterAJobThrows)
{
    ExplorerWorker worker;
    worker.Post([] { throw std::runtime_error("boom"); });
    std::atomic<bool> ran{false};
    EXPECT_TRUE(worker.PostAndWait([&] { ran = true; }, 5s));
    EXPECT_TRUE(ran);
}

} // namespace
} // namespace harbor::dock
