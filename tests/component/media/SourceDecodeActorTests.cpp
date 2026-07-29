#include "dvs/media/MediaProbe.h"
#include "dvs/platform/FrameBudget.h"

#include "SourceDecodeActor.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <future>
#include <gtest/gtest.h>
#include <thread>
#include <vector>

namespace dvs::media::internal {
namespace {

[[nodiscard]] std::filesystem::path fixture(const char* const name) {
    return std::filesystem::path{DVS_MEDIA_FIXTURE_DIR} / name;
}

[[nodiscard]] domain::MediaDescriptor descriptor(const char* const name) {
    const auto probed = MediaProbe::inspect(fixture(name), 0U);
    EXPECT_TRUE(probed);
    return probed.value();
}

TEST(SourceDecodeActorTests, ReusesOneWorkerAcrossRapidExactSeeks) {
    platform::FrameBudget budget{16U * 1024U * 1024U};
    std::atomic<bool> interrupted = false;
    std::atomic<bool> canceled = false;
    SourceDecodeActor actor{
        0U,
        descriptor("h265_a_320x180_30fps_12.mp4"),
        budget,
        &interrupted,
    };
    ASSERT_TRUE(actor.open(canceled));
    const std::thread::id worker = actor.workerThreadId();
    ASSERT_NE(worker, std::thread::id{});

    constexpr std::array<std::int64_t, 12U> kSeekOrder{
        0,
        11,
        1,
        10,
        2,
        9,
        3,
        8,
        4,
        7,
        5,
        6,
    };
    for (const std::int64_t frame : kSeekOrder) {
        SourceDecodeSubmission submitted = actor.submit(SourceDecodeRequest{
            .frameId = domain::FrameId{frame},
            .priority = SourceDecodePriority::Exact,
            .cancellationRequested = &canceled,
        });
        ASSERT_EQ(submitted.status, application::PortSubmitResult::Accepted);
        ASSERT_TRUE(submitted.completion.valid());
        const domain::Result<DecodedFrame> decoded = submitted.completion.get();
        ASSERT_TRUE(decoded) << decoded.error().technicalDetail;
        EXPECT_EQ(actor.workerThreadId(), worker);
        EXPECT_EQ(actor.lastDecodeThreadId(), worker);
    }
    EXPECT_EQ(actor.completedDecodeCount(), kSeekOrder.size());
}

TEST(SourceDecodeActorTests, ExactWorkDisplacesQueuedPrefetchWithoutCreatingWorkers) {
    platform::FrameBudget budget{16U * 1024U * 1024U};
    std::atomic<bool> interrupted = false;
    std::atomic<bool> canceled = false;
    SourceDecodeActor actor{
        0U,
        descriptor("h264_a_320x180_30fps_12.mp4"),
        budget,
        &interrupted,
    };
    ASSERT_TRUE(actor.open(canceled));
    const std::thread::id worker = actor.workerThreadId();

    std::vector<std::future<domain::Result<DecodedFrame>>> prefetch;
    for (std::int64_t frame = 1; frame < 12; ++frame) {
        SourceDecodeSubmission submitted = actor.submit(SourceDecodeRequest{
            .frameId = domain::FrameId{frame},
            .priority = SourceDecodePriority::Prefetch,
            .cancellationRequested = &canceled,
        });
        ASSERT_EQ(submitted.status, application::PortSubmitResult::Accepted);
        prefetch.push_back(std::move(submitted.completion));
    }

    SourceDecodeSubmission exact = actor.submit(SourceDecodeRequest{
        .frameId = domain::FrameId{0},
        .priority = SourceDecodePriority::Exact,
        .cancellationRequested = &canceled,
    });
    ASSERT_EQ(exact.status, application::PortSubmitResult::Accepted);
    const domain::Result<DecodedFrame> exactResult = exact.completion.get();
    ASSERT_TRUE(exactResult) << exactResult.error().technicalDetail;

    std::size_t displaced = 0U;
    for (auto& completion : prefetch) {
        if (!completion.get()) {
            ++displaced;
        }
    }
    EXPECT_GT(displaced, 0U);
    EXPECT_EQ(actor.workerThreadId(), worker);
    EXPECT_EQ(actor.lastDecodeThreadId(), worker);
}

} // namespace
} // namespace dvs::media::internal
