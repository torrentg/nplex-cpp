#pragma once

#include <mutex>
#include <cstddef>
#include <stdexcept>
#include <condition_variable>
#include "cqueue.hpp"
#include "nplex-cpp/exception.hpp"

namespace nplex {

/**
 * Basic thread-safe message queue.
 * 
 * It is a mpmc (multiple producers, multiple consumers) queue.
 * Queue used to comunicate between threads.
 * 
 * @note This class is thread-safe.
 * @tparam T Items type.
 */
template <typename T>
class mqueue
{
  private:

    std::condition_variable m_cond_not_empty;
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
     * @exception nplex_mqueue_exceeded Queue is full.
     */
    void push(const T &item)
    {
        std::unique_lock<std::mutex> lock(m_mutex);

        try {
            m_queue.push(item);
        } catch (const std::length_error &) {
            throw nplex_mqueue_exceeded("mqueue is full");
        }

        m_cond_not_empty.notify_one();
    }

    /**
     * Try to push an item to the queue.
     * 
     * @param[in] item Item to push.
     * 
     * @return true if the item was pushed,
     *         false otherwise.
     */
    bool try_push(const T &item)
    {
        std::unique_lock<std::mutex> lock(m_mutex);

        if (m_queue.full())
            return false;

        m_queue.push(item);
        m_cond_not_empty.notify_one();

        return true;
    }

    /**
     * Pop an item from the queue.
     * Waits until an item is available.
     * 
     * @return Popped item.
     */
    T pop()
    {
        std::unique_lock<std::mutex> lock(m_mutex);

        m_cond_not_empty.wait(lock, [this]() { return !m_queue.empty(); });

        return m_queue.pop();
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
