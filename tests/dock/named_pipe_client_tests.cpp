// Integration tests: NamedPipeClient against a real pipe server in this process. Every wait has a timeout,
// so a broken client fails the test instead of hanging it.

#include <windows.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <gtest/gtest.h>
#include <mutex>
#include <string>
#include <vector>
#include <wil/resource.h>

#include "adapters/named_pipe_client.h"
#include "core/ipc/codec.h"
#include "core/ipc/line_framer.h"

namespace harbor::adapters
{
namespace
{

using namespace std::chrono_literals;

constexpr DWORD kTimeoutMs = 5000;

std::string UniquePipeName()
{
    static std::atomic<int> counter = 0;
    return "harbor.test." + std::to_string(GetCurrentProcessId()) + "." + std::to_string(counter++);
}

// One-instance overlapped pipe server standing in for Harbor.Shell.
class TestServer
{
  public:
    explicit TestServer(const std::string& name)
        : m_pipe(
              CreateNamedPipeW(PipePath(name).c_str(),
                               PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE | FILE_FLAG_OVERLAPPED,
                               PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
                               1, 4096, 4096, 0, nullptr))
    {
    }

    bool IsValid() const
    {
        return m_pipe.is_valid();
    }

    bool WaitForClient()
    {
        OVERLAPPED overlapped{};
        overlapped.hEvent = m_event.get();
        if (ConnectNamedPipe(m_pipe.get(), &overlapped))
        {
            return true;
        }
        switch (GetLastError())
        {
        case ERROR_PIPE_CONNECTED:
            return true;
        case ERROR_IO_PENDING:
            return Complete(overlapped);
        default:
            return false;
        }
    }

    bool Write(std::string_view bytes)
    {
        OVERLAPPED overlapped{};
        overlapped.hEvent = m_event.get();
        if (!WriteFile(m_pipe.get(), bytes.data(), static_cast<DWORD>(bytes.size()), nullptr, &overlapped) &&
            GetLastError() != ERROR_IO_PENDING)
        {
            return false;
        }
        return Complete(overlapped);
    }

    // Next line written by the client, or empty after kTimeoutMs.
    std::string ReadLine()
    {
        const auto deadline = GetTickCount64() + kTimeoutMs;
        while (m_lines.empty() && GetTickCount64() < deadline)
        {
            DWORD available = 0;
            if (!PeekNamedPipe(m_pipe.get(), nullptr, 0, nullptr, &available, nullptr) || available == 0)
            {
                Sleep(10);
                continue;
            }
            std::string chunk(available, '\0');
            OVERLAPPED overlapped{};
            overlapped.hEvent = m_event.get();
            if (!ReadFile(m_pipe.get(), chunk.data(), available, nullptr, &overlapped) &&
                GetLastError() != ERROR_IO_PENDING)
            {
                break;
            }
            if (!Complete(overlapped))
            {
                break;
            }
            for (auto& line : m_framer.Push(chunk))
            {
                m_lines.push_back(std::move(line));
            }
        }
        if (m_lines.empty())
        {
            return {};
        }
        auto line = std::move(m_lines.front());
        m_lines.erase(m_lines.begin());
        return line;
    }

    void Disconnect()
    {
        DisconnectNamedPipe(m_pipe.get());
    }

  private:
    bool Complete(OVERLAPPED& overlapped)
    {
        DWORD bytes = 0;
        if (WaitForSingleObject(overlapped.hEvent, kTimeoutMs) != WAIT_OBJECT_0)
        {
            CancelIoEx(m_pipe.get(), &overlapped);
            GetOverlappedResult(m_pipe.get(), &overlapped, &bytes, TRUE);
            return false;
        }
        return GetOverlappedResult(m_pipe.get(), &overlapped, &bytes, FALSE) != FALSE;
    }

    wil::unique_hfile m_pipe;
    wil::unique_event m_event{wil::EventOptions::ManualReset};
    ipc::LineFramer m_framer;
    std::vector<std::string> m_lines;
};

// Records callbacks from the client's thread and lets the test wait for them.
class Recorder
{
  public:
    ipc::IIpcChannel::Callbacks Callbacks()
    {
        return {
            .onMessage =
                [this](const ipc::Message& message) { Record([&] { m_messages.push_back(message); }); },
            .onConnectionChanged =
                [this](bool connected) { Record([&] { m_connections.push_back(connected); }); },
            .onDiagnostic =
                [this](std::string_view text) { Record([&] { m_diagnostics.emplace_back(text); }); },
        };
    }

    bool WaitForConnections(std::size_t count)
    {
        return WaitFor([&] { return m_connections.size() >= count; });
    }

    bool WaitForMessages(std::size_t count)
    {
        return WaitFor([&] { return m_messages.size() >= count; });
    }

    bool WaitForDiagnostics(std::size_t count)
    {
        return WaitFor([&] { return m_diagnostics.size() >= count; });
    }

    std::vector<ipc::Message> Messages()
    {
        std::lock_guard lock(m_mutex);
        return m_messages;
    }

    std::vector<bool> Connections()
    {
        std::lock_guard lock(m_mutex);
        return m_connections;
    }

    std::size_t DiagnosticCount()
    {
        std::lock_guard lock(m_mutex);
        return m_diagnostics.size();
    }

  private:
    template <typename F> void Record(F update)
    {
        {
            std::lock_guard lock(m_mutex);
            update();
        }
        m_changed.notify_all();
    }

    template <typename P> bool WaitFor(P predicate)
    {
        std::unique_lock lock(m_mutex);
        return m_changed.wait_for(lock, std::chrono::milliseconds(kTimeoutMs), predicate);
    }

    std::mutex m_mutex;
    std::condition_variable m_changed;
    std::vector<ipc::Message> m_messages;
    std::vector<bool> m_connections;
    std::vector<std::string> m_diagnostics;
};

std::string Line(const ipc::Message& message)
{
    return ipc::Encode(message) + "\n";
}

TEST(NamedPipeClient, ConnectsReceivesAndSends)
{
    const auto name = UniquePipeName();
    TestServer server(name);
    ASSERT_TRUE(server.IsValid());
    Recorder recorder;
    NamedPipeClient client(PipePath(name));
    client.Start(recorder.Callbacks());

    ASSERT_TRUE(server.WaitForClient());
    ASSERT_TRUE(recorder.WaitForConnections(1));
    EXPECT_EQ(recorder.Connections(), std::vector<bool>{true});

    ASSERT_TRUE(server.Write(Line(ipc::Ping{7})));
    ASSERT_TRUE(recorder.WaitForMessages(1));
    EXPECT_EQ(recorder.Messages().front(), ipc::Message{ipc::Ping{7}});

    EXPECT_TRUE(client.Send(ipc::Pong{7}));
    EXPECT_EQ(server.ReadLine(), ipc::Encode(ipc::Pong{7}));
}

TEST(NamedPipeClient, DropsInvalidInputAndKeepsReading)
{
    const auto name = UniquePipeName();
    TestServer server(name);
    ASSERT_TRUE(server.IsValid());
    Recorder recorder;
    NamedPipeClient client(PipePath(name));
    client.Start(recorder.Callbacks());
    ASSERT_TRUE(server.WaitForClient());

    ASSERT_TRUE(server.Write("not json\n"));
    ASSERT_TRUE(server.Write(std::string(ipc::LineFramer::kMaxLineBytes + 10, 'x') + "\n"));
    ASSERT_TRUE(server.Write(R"({"v":2,"type":"ping","payload":{"seq":1}})"
                             "\n"));
    ASSERT_TRUE(server.Write(Line(ipc::Ping{8})));

    // An oversized line is reported after the rest of its read chunk is delivered, so the ping can be seen
    // before the third diagnostic: wait for both instead of assuming an order.
    ASSERT_TRUE(recorder.WaitForMessages(1));
    ASSERT_TRUE(recorder.WaitForDiagnostics(3));
    EXPECT_EQ(recorder.Messages(), std::vector<ipc::Message>{ipc::Ping{8}});
    EXPECT_EQ(recorder.DiagnosticCount(), 3u);
}

TEST(NamedPipeClient, ReconnectsAfterServerDisconnects)
{
    const auto name = UniquePipeName();
    TestServer server(name);
    ASSERT_TRUE(server.IsValid());
    Recorder recorder;
    NamedPipeClient client(PipePath(name));
    client.Start(recorder.Callbacks());
    ASSERT_TRUE(server.WaitForClient());
    ASSERT_TRUE(recorder.WaitForConnections(1));

    server.Disconnect();
    ASSERT_TRUE(recorder.WaitForConnections(2));
    ASSERT_TRUE(server.WaitForClient());
    ASSERT_TRUE(recorder.WaitForConnections(3));
    EXPECT_EQ(recorder.Connections(), (std::vector<bool>{true, false, true}));

    ASSERT_TRUE(server.Write(Line(ipc::Ping{9})));
    ASSERT_TRUE(recorder.WaitForMessages(1));
}

TEST(NamedPipeClient, SendFailsWhileNotConnected)
{
    NamedPipeClient client(PipePath(UniquePipeName()));
    EXPECT_FALSE(client.Send(ipc::Pong{1}));
}

TEST(NamedPipeClient, StopsPromptlyWhileWaitingForServer)
{
    Recorder recorder;
    NamedPipeClient client(PipePath(UniquePipeName()));
    client.Start(recorder.Callbacks());
    Sleep(300); // let it enter the retry backoff

    const auto started = std::chrono::steady_clock::now();
    client.Stop();
    EXPECT_LT(std::chrono::steady_clock::now() - started, 500ms);
    EXPECT_TRUE(recorder.Connections().empty());
}

TEST(NamedPipeClient, StopsPromptlyWhileConnected)
{
    const auto name = UniquePipeName();
    TestServer server(name);
    ASSERT_TRUE(server.IsValid());
    Recorder recorder;
    NamedPipeClient client(PipePath(name));
    client.Start(recorder.Callbacks());
    ASSERT_TRUE(server.WaitForClient());
    ASSERT_TRUE(recorder.WaitForConnections(1));

    const auto started = std::chrono::steady_clock::now();
    client.Stop();
    EXPECT_LT(std::chrono::steady_clock::now() - started, 500ms);
}

} // namespace
} // namespace harbor::adapters
