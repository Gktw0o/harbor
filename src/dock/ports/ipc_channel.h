#pragma once

#include <functional>
#include <string_view>

#include "core/ipc/messages.h"

namespace harbor::ipc
{

// Port: a message channel to another Harbor process. Callbacks run on the channel's own thread; callers
// marshal to their UI thread as needed.
class IIpcChannel
{
  public:
    struct Callbacks
    {
        std::function<void(const Message&)> onMessage;
        std::function<void(bool connected)> onConnectionChanged;
        // Dropped input and transport errors, for logging.
        std::function<void(std::string_view)> onDiagnostic;
    };

    virtual ~IIpcChannel() = default;

    // Starts connecting in the background and keeps reconnecting until Stop(). Call once.
    virtual void Start(Callbacks callbacks) = 0;

    // Stops the background work. No callback runs after it returns. Safe to call more than once.
    virtual void Stop() noexcept = 0;

    // Thread-safe. Returns false if not connected or if the write failed; the channel then reconnects.
    virtual bool Send(const Message& message) noexcept = 0;
};

} // namespace harbor::ipc
