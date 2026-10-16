#pragma once

#include <cstddef>
#include <mutex>
#include <utility>
#include <vector>

namespace harbor::dock
{

// A queue between threads that keeps every value, in order (unlike LatestValue): dockItems and appInfo must
// be applied as they came (docs/adr/0010). Push from any thread, TakeAll on the reader's. Bounded: a reader
// that stopped doesn't let it grow without end. Thread-safe.
template <typename T> class MessageQueue
{
  public:
    enum class Pushed
    {
        // The queue was empty: wake the reader.
        First,
        // The reader has been woken already.
        Queued,
        // Full: the value was dropped.
        Dropped,
    };

    explicit MessageQueue(std::size_t capacity) : m_capacity(capacity)
    {
    }

    Pushed Push(T value)
    {
        std::lock_guard lock(m_mutex);
        if (m_values.size() >= m_capacity)
        {
            return Pushed::Dropped;
        }
        m_values.push_back(std::move(value));
        return m_values.size() == 1 ? Pushed::First : Pushed::Queued;
    }

    // Everything queued, oldest first.
    std::vector<T> TakeAll()
    {
        std::lock_guard lock(m_mutex);
        return std::exchange(m_values, {});
    }

  private:
    const std::size_t m_capacity;
    std::mutex m_mutex;
    std::vector<T> m_values;
};

} // namespace harbor::dock
