#include "dvs/platform/SourceIdentityService.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <string>
#include <string_view>
#include <system_error>

namespace dvs::platform {
namespace {

std::atomic<std::uint64_t> nextDirectoryNumber{0};

class SourceIdentityServiceTests : public ::testing::Test {
protected:
    void SetUp() override {
        const auto timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
        root_ = std::filesystem::temp_directory_path() /
                ("dvs-source-identity-" + std::to_string(timestamp) + "-" +
                 std::to_string(nextDirectoryNumber.fetch_add(1U)));
        std::error_code errorCode;
        std::filesystem::create_directories(root_, errorCode);
        ASSERT_FALSE(errorCode);
    }

    void TearDown() override {
        std::error_code errorCode;
        std::filesystem::remove_all(root_, errorCode);
    }

    [[nodiscard]] std::filesystem::path filePath(const std::string_view name) const {
        return root_ / std::string{name};
    }

    void writeFile(const std::filesystem::path& path, const std::string_view contents) const {
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(stream.is_open());
        stream.write(contents.data(), static_cast<std::streamsize>(contents.size()));
        ASSERT_TRUE(stream.good());
    }

private:
    std::filesystem::path root_;
};

TEST_F(SourceIdentityServiceTests, ReportsMissingSourcesAsRecoverableMediaProbeErrors) {
    const auto result = SourceIdentityService::fingerprint(
        filePath("missing.mp4"), domain::SourceId{0}, domain::MediaOperation::kMediaProbe);

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, domain::MediaErrorCode::kSourceMissing);
    EXPECT_EQ(result.error().operation, domain::MediaOperation::kMediaProbe);
    EXPECT_EQ(result.error().source, domain::SourceId{0});
    EXPECT_TRUE(result.error().recoverable);
}

TEST_F(SourceIdentityServiceTests, ReportsMismatchesAsRecoverableMediaProbeErrors) {
    const std::filesystem::path source = filePath("changed.mp4");
    writeFile(source, "abc");
    const auto expected = SourceIdentityService::fingerprint(
        source, domain::SourceId{1}, domain::MediaOperation::kMediaProbe);
    ASSERT_TRUE(expected);

    writeFile(source, "xyz");
    const auto status = SourceIdentityService::verify(
        source, expected.value(), domain::SourceId{1}, domain::MediaOperation::kMediaProbe);

    ASSERT_FALSE(status);
    EXPECT_EQ(status.error().code, domain::MediaErrorCode::kSourceFingerprintMismatch);
    EXPECT_EQ(status.error().operation, domain::MediaOperation::kMediaProbe);
    EXPECT_EQ(status.error().source, domain::SourceId{1});
    EXPECT_TRUE(status.error().recoverable);
}

} // namespace
} // namespace dvs::platform
