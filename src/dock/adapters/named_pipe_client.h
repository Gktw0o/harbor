#pragma once

#include <windows.h>

#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <wil/resource.h>

#include "ports/ipc_channel.h"

namespace harbor::adapters
{

// "\\.\pipe\<name>" for a validated Harbor pipe name (see ipc::IsValidPipeName).
std::wstring PipePath(std::string_view pipeName);

// IIpcChannel over a local named pipe, as the client. Connects only to a server running as the same user,
// with impersonation limited to identification, and reconnects with backoff until stopped.
class NamedPipeClient final : public ipc::IIpcChannel
{
  public:
    explicit NamedPipeClient(std::wstring pipePath);
    ~NamedPipeClient() override;
    NamedPipeClient(const NamedPipeClient&) = delete;
    NamedPipeClient& operator=(const NamedPipeClient&) = delete;

    void Start(Callbacks callbacks) override;
    void Stop() noexcept override;
    bool Send(const ipc::Message& message) noexcept override;

  private:
    void Run() noexcept;
    HRESULT Connect() noexcept;
    void ReadUntilDisconnected(HANDLE pipe) noexcept;
    void Deliver(std::string_view line) noexcept;
    void Diagnostic(std::string_view text) noexcept;
    void ConnectionChanged(bool connected) noexcept;

    const std::wstring m_pipePath;
    Callbacks m_callbacks;
    wil::unique_event m_stop{wil::EventOptions::ManualReset};
    std::thread m_thread;

    // Guards m_pipe. Only the worker thread opens and closes it; Send() writes under the lock.
    std::mutex m_pipeMutex;
    wil::unique_hfile m_pipe;
};

} // namespace harbor::adapters
