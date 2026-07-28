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

    const auto prepared = ProjectRelinkService::prepare(0, relocatedPath);

    ASSERT_TRUE(prepared);
    EXPECT_EQ(prepared.value().sourceId(), 0U);
    EXPECT_TRUE(prepared.value().normalizedPath().is_absolute());
    EXPECT_EQ(prepared.value().normalizedPath().filename(), relocatedPath.filename());
    EXPECT_TRUE(prepared.value().sourceIdentity().isComplete());
}

TEST_F(ProjectRelinkServiceTests, ReturnsStableErrorsForMissingFile) {
    const auto missing =
        ProjectRelinkService::prepare(1, root_ / "missing.mov");
    ASSERT_FALSE(missing);
    EXPECT_EQ(missing.error().code, domain::MediaErrorCode::kSourceMissing);
    ASSERT_TRUE(missing.error().source.has_value());
    EXPECT_EQ(*missing.error().source, 1U);
}

} // namespace
} // namespace dvs::persistence
