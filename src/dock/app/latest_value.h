#pragma once

#include <mutex>
#include <optional>
#include <utility>

namespace harbor::dock
{

// A one-slot mailbox between threads: Put replaces a value nobody has taken yet, so a reader that falls
// behind only ever sees the newest one. Thread-safe.
template <typename T> class LatestValue
{
  public:
    void Put(T value)
    {
        std::lock_guard lock(m_mutex);
        m_value = std::move(value);
    }

    // The newest value, or nullopt if there's been none since the last Take.
    std::optional<T> Take()
    {
        std::lock_guard lock(m_mutex);
        return std::exchange(m_value, std::nullopt);
    }

  private:
    std::mutex m_mutex;
    std::optional<T> m_value;
};

} // namespace harbor::dock
