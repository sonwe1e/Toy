#include "dvs/persistence/ProjectRelinkService.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <string>
#include <system_error>

namespace dvs::persistence {
namespace {

std::atomic<std::uint64_t> nextDirectoryNumber{0};

class ProjectRelinkServiceTests : public ::testing::Test {
protected:
    void SetUp() override {
        const auto timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
        root_ = std::filesystem::temp_directory_path() /
                ("dvs-relink-" + std::to_string(timestamp) + "-" +
                 std::to_string(nextDirectoryNumber.fetch_add(1U)));
        std::filesystem::create_directories(root_);
    }

    void TearDown() override {
        std::error_code errorCode;
        std::filesystem::remove_all(root_, errorCode);
    }

    std::filesystem::path root_;
};

TEST_F(ProjectRelinkServiceTests, PreparesAValidatedCandidateWithoutMutatingAnyProject) {
    const std::filesystem::path relocatedPath = root_ / "relocated-a.mov";
    std::ofstream stream(relocatedPath, std::ios::binary);
    stream << "relocated source";
    stream.close();
    ASSERT_TRUE(stream.good());

    const auto prepared = ProjectRelinkService::prepare(domain::SourceRole::kA, relocatedPath);

    ASSERT_TRUE(prepared);
    EXPECT_EQ(prepared.value().sourceRole(), domain::SourceRole::kA);
    EXPECT_TRUE(prepared.value().normalizedPath().is_absolute());
    EXPECT_EQ(prepared.value().normalizedPath().filename(), relocatedPath.filename());
    EXPECT_TRUE(prepared.value().sourceIdentity().isComplete());
}

TEST_F(ProjectRelinkServiceTests, ReturnsStableErrorsForMissingFileAndInvalidRole) {
    const auto missing =
        ProjectRelinkService::prepare(domain::SourceRole::kB, root_ / "missing.mov");
    ASSERT_FALSE(missing);
    EXPECT_EQ(missing.error().code, domain::MediaErrorCode::kSourceMissing);
    EXPECT_EQ(missing.error().sourceRole, domain::SourceRole::kB);

    const auto invalid =
        ProjectRelinkService::prepare(domain::SourceRole::kProject, root_ / "ignored.mov");
    ASSERT_FALSE(invalid);
    EXPECT_EQ(invalid.error().code, domain::MediaErrorCode::kInvalidArgument);
    EXPECT_EQ(invalid.error().sourceRole, domain::SourceRole::kProject);
}

} // namespace
} // namespace dvs::persistence
