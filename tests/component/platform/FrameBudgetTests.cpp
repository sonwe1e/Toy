#include "dvs/platform/FrameBudget.h"

#include <atomic>
#include <cstddef>
#include <gtest/gtest.h>
#include <thread>
#include <utility>
#include <vector>

namespace dvs::platform {

TEST(FrameBudgetTests, TracksReservationsAcrossMovesAndDestruction) {
    FrameBudget budget{10};
    EXPECT_EQ(budget.capacityBytes(), 10U);
    EXPECT_EQ(budget.availableBytes(), 10U);

    auto first = budget.tryReserve(4);
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(first->bytes(), 4U);
    EXPECT_EQ(budget.reservedBytes(), 4U);
    EXPECT_EQ(budget.availableBytes(), 6U);
    EXPECT_FALSE(budget.tryReserve(7).has_value());

    FrameBudget::Reservation moved = std::move(*first);
    EXPECT_FALSE(static_cast<bool>(*first));
    EXPECT_TRUE(static_cast<bool>(moved));
    moved.reset();
    EXPECT_EQ(budget.reservedBytes(), 0U);
    EXPECT_EQ(budget.availableBytes(), 10U);
}

TEST(FrameBudgetTests, AllowsExactlyCapacityAndReleasesOnScopeExit) {
    FrameBudget budget{8};
    {
        auto reservation = budget.tryReserve(8);
        ASSERT_TRUE(reservation.has_value());
        EXPECT_EQ(budget.availableBytes(), 0U);
    }
    EXPECT_EQ(budget.reservedBytes(), 0U);
}

TEST(FrameBudgetTests, EnforcesTheShared256MiBLimitAcrossTheDualSourcePipeline) {
    constexpr std::size_t kCapacityBytes = 256U * 1024U * 1024U;
    constexpr std::size_t kWidth = 3840U;
    constexpr std::size_t kHeight = 2160U;
    constexpr std::size_t kYPlaneBytes = kWidth * kHeight;
    constexpr std::size_t kUvPlaneBytes = kWidth * ((kHeight + 1U) / 2U);
    constexpr std::size_t kSourceCount = 2U;

    // One decoded pair, one upload-staging pair, and retained front/back GPU pairs are all live
    // during replacement. Reserving each plane separately proves that no A/B or UV bytes escape
    // the one shared cap.
    constexpr std::size_t kCompletePairCopies = 4U;
    constexpr std::size_t kExpectedBytes =
        kCompletePairCopies * kSourceCount * (kYPlaneBytes + kUvPlaneBytes);
    static_assert(kExpectedBytes < kCapacityBytes);

    FrameBudget budget{kCapacityBytes};
    std::vector<FrameBudget::Reservation> reservations;
    reservations.reserve(kCompletePairCopies * kSourceCount * 2U);

    const auto reserve = [&](const std::size_t bytes) {
        auto reservation = budget.tryReserve(bytes);
        if (!reservation) {
            return false;
        }
        reservations.push_back(std::move(*reservation));
        return true;
    };
    for (std::size_t pairCopy = 0; pairCopy < kCompletePairCopies; ++pairCopy) {
        for (std::size_t source = 0; source < kSourceCount; ++source) {
            ASSERT_TRUE(reserve(kYPlaneBytes));
            ASSERT_TRUE(reserve(kUvPlaneBytes));
        }
    }

    EXPECT_EQ(budget.reservedBytes(), kExpectedBytes);
    auto capacityRemainder = budget.tryReserve(kCapacityBytes - kExpectedBytes);
    ASSERT_TRUE(capacityRemainder.has_value());
    EXPECT_EQ(budget.availableBytes(), 0U);
    EXPECT_FALSE(budget.tryReserve(1U).has_value());

    capacityRemainder->reset();
    reservations.clear();
    EXPECT_EQ(budget.reservedBytes(), 0U);
}

TEST(FrameBudgetTests, ConcurrentReservationsNeverExceedCapacityAndReleaseExactlyOnce) {
    constexpr std::size_t kCapacity = 64U;
    constexpr std::size_t kWorkerCount = 8U;
    constexpr std::size_t kIterations = 2'000U;
    FrameBudget budget{kCapacity};
    std::atomic<bool> start{false};
    std::atomic<std::size_t> peakReserved{0U};
    std::vector<std::thread> workers;
    workers.reserve(kWorkerCount);

    for (std::size_t worker = 0U; worker < kWorkerCount; ++worker) {
        workers.emplace_back([&, worker] {
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            const std::size_t bytes = worker + 1U;
            for (std::size_t iteration = 0U; iteration < kIterations; ++iteration) {
                auto reservation = budget.tryReserve(bytes);
                if (!reservation) {
                    std::this_thread::yield();
                    continue;
                }
                const std::size_t current = budget.reservedBytes();
                std::size_t observedPeak = peakReserved.load(std::memory_order_relaxed);
                while (observedPeak < current &&
                       !peakReserved.compare_exchange_weak(observedPeak,
                                                           current,
                                                           std::memory_order_relaxed,
                                                           std::memory_order_relaxed)) {
                }
            }
        });
    }

    start.store(true, std::memory_order_release);
    for (std::thread& worker : workers) {
        worker.join();
    }

    EXPECT_LE(peakReserved.load(std::memory_order_relaxed), kCapacity);
    EXPECT_EQ(budget.reservedBytes(), 0U);
    EXPECT_EQ(budget.availableBytes(), kCapacity);
}

} // namespace dvs::platform
