#include "RepositorySupport.h"

#include <gtest/gtest.h>

namespace dvs::persistence::internal {
namespace {

[[nodiscard]] application::RequestContext requestContext() {
    return application::RequestContext{
        .sessionId = domain::SessionId{1U},
        .sessionEpoch = domain::SessionEpoch{2U},
        .requestId = domain::RequestId{3U},
    };
}

TEST(OperationStateCommitSemanticsTests, CancellationBeforePublicationClaimWins) {
    OperationState operation{requestContext()};

    operation.requestCancellation();

    EXPECT_TRUE(operation.isCanceled());
    EXPECT_FALSE(operation.tryBeginCommit());
}

TEST(OperationStateCommitSemanticsTests, PublicationClaimPreventsLaterCancellation) {
    OperationState operation{requestContext()};

    ASSERT_TRUE(operation.tryBeginCommit());
    operation.requestCancellation();

    EXPECT_FALSE(operation.isCanceled());
    EXPECT_FALSE(operation.tryBeginCommit());
}

} // namespace
} // namespace dvs::persistence::internal
