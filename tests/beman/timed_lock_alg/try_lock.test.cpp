// SPDX-License-Identifier: MIT

#include <beman/timed_lock_alg/mutex.hpp>
#include "mock_timed_mutex.hpp"

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <mutex>
#include <thread>
#include <tuple>

using namespace std::chrono_literals;
namespace tla   = beman::timed_lock_alg;
using MockMutex = beman::timed_lock_alg::test::MockTimedMutex;

namespace {
// joining thread for implementations missing std::jthread
class JThread : public std::thread {
  public:
    template <class... Args>
    JThread(Args&&... args) : std::thread(std::forward<Args>(args)...) {}
    ~JThread() {
        if (joinable()) {
            join();
        }
    }
};
const auto now         = std::chrono::steady_clock::now();
const auto no_duration = 0ms;
const auto extra_grace = 100ms;

template <class MutexType, std::size_t N>
void unlocker(std::array<MutexType, N>& mtxs) {
    std::apply([](auto&... mts) { return std::scoped_lock(std::adopt_lock, mts...); }, mtxs);
}
} // namespace

// ============================================================================
// Basic Tests with Mock Mutexes (fast, deterministic)
// ============================================================================

TEST(TryLock, ZeroMutexes) {
    EXPECT_EQ(-1, tla::try_lock_until(now));
    EXPECT_EQ(-1, tla::try_lock_for(no_duration));
}

TEST(TryLock, OneMutexUnlocked) {
    MockMutex mtx;
    EXPECT_EQ(-1, tla::try_lock_until(now, mtx));
    mtx.unlock();

    EXPECT_EQ(-1, tla::try_lock_for(no_duration, mtx));
    mtx.unlock();
}

TEST(TryLock, ManyMutexesUnlocked) {
    std::array<MockMutex, 30> mtxs;

    EXPECT_EQ(-1, std::apply([](auto&... mts) { return tla::try_lock_until(now, mts...); }, mtxs));
    unlocker(mtxs);

    EXPECT_EQ(-1, std::apply([](auto&... mts) { return tla::try_lock_for(no_duration, mts...); }, mtxs));
    unlocker(mtxs);
}

TEST(TryLock, OneMutexLocked) {
    MockMutex mtx;
    mtx.should_fail = true;
    EXPECT_EQ(0, tla::try_lock_until(now, mtx));
    EXPECT_EQ(0, tla::try_lock_for(no_duration, mtx));
}

TEST(TryLock, ManyMutexesOneLockedFirst) {
    std::array<MockMutex, 3> mtxs;
    mtxs[0].should_fail = true;
    int result          = std::apply([](auto&... mts) { return tla::try_lock_for(no_duration, mts...); }, mtxs);
    EXPECT_EQ(0, result);
}

TEST(TryLock, ManyMutexesOneLockedMiddle) {
    std::array<MockMutex, 3> mtxs;
    mtxs[1].should_fail = true;
    int result          = std::apply([](auto&... mts) { return tla::try_lock_for(no_duration, mts...); }, mtxs);
    EXPECT_EQ(1, result);
}

TEST(TryLock, ManyMutexesOneLockedLast) {
    std::array<MockMutex, 3> mtxs;
    mtxs[2].should_fail = true;
    int result          = std::apply([](auto&... mts) { return tla::try_lock_for(no_duration, mts...); }, mtxs);
    EXPECT_EQ(2, result);
}

// ============================================================================
// Integration Tests with Real Mutexes (verify actual threading behavior)
// ============================================================================

TEST(TryLockIntegration, RealMutexBasic) {
    std::timed_mutex mtx;
    EXPECT_EQ(-1, tla::try_lock_until(now, mtx));
    mtx.unlock();

    EXPECT_EQ(-1, tla::try_lock_for(no_duration, mtx));
    mtx.unlock();
}

TEST(TryLockIntegration, ManyRealMutexesUnlocked) {
    std::array<std::timed_mutex, 30> mtxs;

    EXPECT_EQ(-1, std::apply([](auto&... mts) { return tla::try_lock_until(now, mts...); }, mtxs));
    unlocker(mtxs);

    EXPECT_EQ(-1, std::apply([](auto&... mts) { return tla::try_lock_for(no_duration, mts...); }, mtxs));
    unlocker(mtxs);
}

TEST(TryLockIntegration, ManyMutexesOneLockedWithTimeout) {
    std::array<std::timed_mutex, 30> mtxs;
    auto                             th = JThread([&] {
        std::lock_guard lg(mtxs.back());
        std::this_thread::sleep_for(15ms);
    });

    std::this_thread::sleep_for(5ms); // approx 10ms left on lock after this
    EXPECT_EQ(-1, std::apply([](auto&... mts) { return tla::try_lock_for(20ms + extra_grace, mts...); }, mtxs));

    unlocker(mtxs);
}

TEST(TryLockIntegration, ReturnLastFailed) {
    std::array<std::timed_mutex, 2> mtxs;
    auto                            th = JThread([&] {
        std::lock(mtxs[0], mtxs[1]);
        std::this_thread::sleep_for(100ms);
        mtxs[0].unlock(); // 50ms after try_lock_for started, 200ms left

        // try_lock_for here hangs on mtxs[1] and should return 1:
        std::this_thread::sleep_for(300ms + extra_grace);
        mtxs[1].unlock();
    });

    std::this_thread::sleep_for(50ms);
    EXPECT_EQ(1, std::apply([](auto&... mts) { return tla::try_lock_for(200ms, mts...); }, mtxs));
}

TEST(TryLockIntegration, SucceedWithThreeInTrickySequence) {
    // The comments in this test are on implementation details.
    // A different implementation may behave differently but should
    // still succeed in locking all three in time.
    std::array<std::timed_mutex, 3> mtxs;
    auto                            th = JThread([&] {
        std::lock(mtxs[0], mtxs[1], mtxs[2]);
        std::this_thread::sleep_for(55ms);
        mtxs[0].unlock(); // 5ms after try_lock_for started, 95ms left
                          // try_lock_for gets this and jumps to mtxs[1]
        std::this_thread::sleep_for(5ms);
        mtxs[2].unlock(); // try_lock_for still hangs on mtxs[1]
        mtxs[0].lock();
        mtxs[1].unlock(); // try_lock_for gets this and jumps to mtxs[0]
                          // 10ms after try_lock_for started, 90ms left
        std::this_thread::sleep_for(10ms);
        mtxs[0].unlock(); // try_lock_for should have 80ms left here
    });

    std::this_thread::sleep_for(50ms);
    EXPECT_EQ(-1, std::apply([](auto&... mts) { return tla::try_lock_for(100ms + extra_grace, mts...); }, mtxs));
}
