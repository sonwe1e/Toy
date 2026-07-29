#include "dvs/application/PrefetchScheduler.h"

#include <gtest/gtest.h>
#include <vector>

namespace dvs::application {
namespace {

TEST(PrefetchSchedulerTests, ExactNavigationPrefetchesInTheObservedDirection) {
    PrefetchScheduler scheduler;

    EXPECT_EQ(scheduler.afterExact(domain::FrameId{5}, 20U),
              (std::vector<domain::FrameId>{
                  domain::FrameId{6},
                  domain::FrameId{7},
                  domain::FrameId{8},
                  domain::FrameId{4},
              }));
    EXPECT_EQ(scheduler.afterExact(domain::FrameId{4}, 20U),
              (std::vector<domain::FrameId>{
                  domain::FrameId{3},
                  domain::FrameId{2},
                  domain::FrameId{1},
                  domain::FrameId{5},
              }));
}

TEST(PrefetchSchedulerTests, TargetsStayInsideTheCanonicalTimeline) {
    PrefetchScheduler scheduler;

    EXPECT_EQ(scheduler.afterExact(domain::FrameId{0}, 3U),
              (std::vector<domain::FrameId>{domain::FrameId{1}, domain::FrameId{2}}));
    EXPECT_EQ(PrefetchScheduler::duringForwardPlayback(domain::FrameId{1}, 3U),
              (std::vector<domain::FrameId>{domain::FrameId{2}}));
}

} // namespace
} // namespace dvs::application
