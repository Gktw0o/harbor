#include "adapters/named_pipe_client.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <wil/result_macros.h>
#include <wil/token_helpers.h>

#include "core/ipc/codec.h"
#include "core/ipc/line_framer.h"

namespace harbor::adapters
{
namespace
{

constexpr DWORD kInitialRetryDelayMs = 100;
constexpr DWORD kMaxRetryDelayMs = 2000;
// A peer that stops reading must not freeze the caller (the dock's UI thread) for long.
constexpr DWORD kWriteTimeoutMs = 1000;

// Refuses servers running as another user, so a squatted pipe name can't feed the dock.
HRESULT VerifyServerIsCurrentUser(HANDLE pipe) noexcept
{
    ULONG serverProcessId = 0;
    RETURN_IF_WIN32_BOOL_FALSE(GetNamedPipeServerProcessId(pipe, &serverProcessId));

    wil::unique_handle serverProcess(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, serverProcessId));
    RETURN_LAST_ERROR_IF(!serverProcess);
    wil::unique_handle serverToken;
    RETURN_IF_WIN32_BOOL_FALSE(OpenProcessToken(serverProcess.get(), TOKEN_QUERY, serverToken.put()));

    wil::unique_tokeninfo_ptr<TOKEN_USER> serverUser;
    RETURN_IF_FAILED(wil::get_token_information_nothrow(serverUser, serverToken.get()));
    wil::unique_tokeninfo_ptr<TOKEN_USER> ownUser;
    RETURN_IF_FAILED(wil::get_token_information_nothrow(ownUser, GetCurrentProcessToken()));

    RETURN_HR_IF(E_ACCESSDENIED, !EqualSid(serverUser->User.Sid, ownUser->User.Sid));
    return S_OK;
}

} // namespace

std::wstring PipePath(std::string_view pipeName)
{
    // Valid pipe names are ASCII, so widening byte by byte is exact.
    std::wstring path = L"\\\\.\\pipe\\";
    path.append(pipeName.begin(), pipeName.end());
    return path;
}

NamedPipeClient::NamedPipeClient(std::wstring pipePath) : m_pipePath(std::move(pipePath))
{
}

NamedPipeClient::~NamedPipeClient()
{
    Stop();
}

void NamedPipeClient::Start(Callbacks callbacks)
{
    m_callbacks = std::move(callbacks);
    m_thread = std::thread([this] { Run(); });
}

void NamedPipeClient::Stop() noexcept
{
    m_stop.SetEvent();
    if (m_thread.joinable())
    {
        m_thread.join();
    }
}

bool NamedPipeClient::Send(const ipc::Message& message) noexcept
try
{
    std::string line = ipc::Encode(message);
    line.push_back('\n');

    std::lock_guard lock(m_pipeMutex);
    if (!m_pipe)
    {
        return false;
    }

    wil::unique_event_nothrow written;
    if (FAILED(written.create(wil::EventOptions::ManualReset)))
    {
        return false;
    }
    OVERLAPPED overlapped{};
    overlapped.hEvent = written.get();
    if (!WriteFile(m_pipe.get(), line.data(), static_cast<DWORD>(line.size()), nullptr, &overlapped) &&
        GetLastError() != ERROR_IO_PENDING)
    {
        // Abort the pending read too, so the worker drops this connection and reconnects.
        CancelIoEx(m_pipe.get(), nullptr);
        return false;
    }

    DWORD bytesWritten = 0;
    if (WaitForSingleObject(written.get(), kWriteTimeoutMs) != WAIT_OBJECT_0)
    {
        CancelIoEx(m_pipe.get(), nullptr);
        GetOverlappedResult(m_pipe.get(), &overlapped, &bytesWritten, TRUE);
        return false;
    }
    return GetOverlappedResult(m_pipe.get(), &overlapped, &bytesWritten, FALSE) &&
           bytesWritten == line.size();
}
catch (...)
{
    return false;
}

void NamedPipeClient::Run() noexcept
{
    DWORD retryDelayMs = kInitialRetryDelayMs;
    while (!m_stop.is_signaled())
    {
        const HRESULT hr = Connect();
        if (SUCCEEDED(hr))
        {
            retryDelayMs = kInitialRetryDelayMs;
            ConnectionChanged(true);
            ReadUntilDisconnected(m_pipe.get());
            {
                std::lock_guard lock(m_pipeMutex);
                m_pipe.reset();
            }
            ConnectionChanged(false);
        }
        else if (hr != HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) && hr != HRESULT_FROM_WIN32(ERROR_PIPE_BUSY))
        {
            // "No server yet" is the normal state while Harbor.Shell isn't running; anything else is worth a
            // log.
            char text[48];
            std::snprintf(text, sizeof(text), "connect failed: 0x%08lX", static_cast<unsigned long>(hr));
            Diagnostic(text);
        }

        if (m_stop.wait(retryDelayMs))
        {
            break;
        }
        retryDelayMs = std::min(retryDelayMs * 2, kMaxRetryDelayMs);
    }
}

HRESULT NamedPipeClient::Connect() noexcept
{
    wil::unique_hfile pipe(
        CreateFileW(m_pipePath.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                    FILE_FLAG_OVERLAPPED | SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, nullptr));
    if (!pipe)
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    RETURN_IF_FAILED(VerifyServerIsCurrentUser(pipe.get()));

    std::lock_guard lock(m_pipeMutex);
    m_pipe = std::move(pipe);
    return S_OK;
}

void NamedPipeClient::ReadUntilDisconnected(HANDLE pipe) noexcept
{
    wil::unique_event_nothrow readDone;
    if (FAILED(readDone.create(wil::EventOptions::ManualReset)))
    {
        return;
    }

    ipc::LineFramer framer;
    std::array<char, 4096> buffer{};
    for (;;)
    {
        OVERLAPPED overlapped{};
        overlapped.hEvent = readDone.get();
        if (!ReadFile(pipe, buffer.data(), static_cast<DWORD>(buffer.size()), nullptr, &overlapped))
        {
            if (GetLastError() != ERROR_IO_PENDING)
            {
                return;
            }
            const HANDLE waits[] = {readDone.get(), m_stop.get()};
            if (WaitForMultipleObjects(2, waits, FALSE, INFINITE) != WAIT_OBJECT_0)
            {
                // Stopping: the read must finish before `overlapped` goes out of scope.
                DWORD ignored = 0;
                CancelIoEx(pipe, &overlapped);
                GetOverlappedResult(pipe, &overlapped, &ignored, TRUE);
                return;
            }
        }

        DWORD bytesRead = 0;
        if (!GetOverlappedResult(pipe, &overlapped, &bytesRead, FALSE))
        {
            // ERROR_BROKEN_PIPE: the server went away. ERROR_OPERATION_ABORTED: Send() gave up on a write.
            return;
        }

        try
        {
            const auto droppedBefore = framer.DroppedLineCount();
            for (const auto& line : framer.Push(std::string_view(buffer.data(), bytesRead)))
            {
                Deliver(line);
            }
            if (framer.DroppedLineCount() != droppedBefore)
            {
                Diagnostic("dropped an oversized line");
            }
        }
        catch (...)
        {
            Diagnostic("out of memory while reading; reconnecting");
            return;
        }
    }
}

void NamedPipeClient::Deliver(std::string_view line) noexcept
{
    const auto decoded = ipc::Decode(line);
    if (const auto* error = std::get_if<ipc::DecodeError>(&decoded))
    {
        Diagnostic(ipc::ToString(*error));
        return;
    }
    try
    {
        if (m_callbacks.onMessage)
        {
            m_callbacks.onMessage(std::get<ipc::Message>(decoded));
        }
    }
    catch (...)
    {
        Diagnostic("message handler threw");
    }
}

void NamedPipeClient::Diagnostic(std::string_view text) noexcept
try
{
    if (m_callbacks.onDiagnostic)
    {
        m_callbacks.onDiagnostic(text);
    }
}
catch (...)
{
}

void NamedPipeClient::ConnectionChanged(bool connected) noexcept
try
{
    if (m_callbacks.onConnectionChanged)
    {
        m_callbacks.onConnectionChanged(connected);
    }
}
catch (...)
{
}

} // namespace harbor::adapters
