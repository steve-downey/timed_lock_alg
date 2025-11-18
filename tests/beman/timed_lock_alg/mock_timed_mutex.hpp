// SPDX-License-Identifier: MIT

#ifndef BEMAN_TIMED_LOCK_ALG_TESTS_MOCK_TIMED_MUTEX_HPP
#define BEMAN_TIMED_LOCK_ALG_TESTS_MOCK_TIMED_MUTEX_HPP

#include <atomic>
#include <chrono>
#include <thread>

namespace beman::timed_lock_alg::test {

struct MockTimedMutex {
    std::atomic<bool> locked{false};
    std::atomic<bool> should_fail{false};
    std::atomic<int>  lock_count{0};
    std::atomic<int>  unlock_count{0};
    std::atomic<int>  try_lock_count{0};

    void lock() {
        while (should_fail)
            std::this_thread::yield();
        ++lock_count;
        locked = true;
    }

    bool try_lock() {
        ++try_lock_count;
        if (should_fail)
            return false;
        ++lock_count;
        locked = true;
        return true;
    }

    template <class Rep, class Period>
    bool try_lock_for(const std::chrono::duration<Rep, Period>&) {
        return try_lock();
    }

    template <class Clock, class Duration>
    bool try_lock_until(const std::chrono::time_point<Clock, Duration>&) {
        return try_lock();
    }

    void unlock() {
        ++unlock_count;
        locked = false;
    }
};

} // namespace beman::timed_lock_alg::test

#endif // BEMAN_TIMED_LOCK_ALG_TESTS_MOCK_TIMED_MUTEX_HPP
