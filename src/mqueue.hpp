#pragma once

#include <mutex>
#include <cstddef>
#include "cqueue.hpp"
#include "nplex-cpp/exception.hpp"

namespace nplex {

/**
 * Message queue used to store commands pending to be processed by the event loop.
 * 
 * Intended usage:
 * 
 *   thread 1: push(item)
 *             queue has only 1 item -> wake up thread X (via async)
 *   thread 2: push(item)
 *             queue has more than 1 item -> does nothing
 *   thread X: repeatedly calls try_pop(item) until the queue is empty
 *             sets thread to waiting
 *   thread 1: push(item)
 *             queue has only 1 item -> wake up thread X (via async)
 * 
 * @note This class is thread-safe.
 * @tparam T Type of items in the queue.
 */
template <typename T>
class mqueue
{
  private:

    gto::cqueue<T> m_queue;
    std::mutex m_mutex;

  public:

    /**
     * Constructor.
     * 
     * @param[in] capacity Maximum queue capacity (0 means unlimited).
     */
    mqueue(std::size_t capacity = 0) : m_queue(capacity) {}

    /**
     * Remove all queue items.
     */
    void clear()
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_queue.clear();
    }

    /**
     * Push an item to the queue.
     * 
     * @param[in] item Item to push.
     * 
     * @return The queue size after the push.
     * 
     * @exception nplex_exception Queue is full.
     */
    std::size_t push(const T &item)
    {
        std::unique_lock<std::mutex> lock(m_mutex);

        if (m_queue.full())
            throw nplex_mqueue_exceeded("mqueue exceeded capacity");

        m_queue.push(item);
        return m_queue.size();
    }

    std::size_t push(T &&item)
    {
        std::unique_lock<std::mutex> lock(m_mutex);

        if (m_queue.full())
            throw nplex_mqueue_exceeded("mqueue exceeded capacity");

        m_queue.push(std::move(item));
        return m_queue.size();
    }

    /**
     * Try to pop an item from the queue.
     * 
     * @param[out] item Popped item.
     * 
     * @return true if the item was popped,
     *         false otherwise.
     */
    bool try_pop(T &item)
    {
        std::unique_lock<std::mutex> lock(m_mutex);

        if (m_queue.empty())
            return false;

        item = m_queue.pop();
        return true;
    }
};

}; // namespace nplex
