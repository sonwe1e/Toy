#include "dvs/persistence/FingerprintService.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace dvs::persistence {
namespace {

constexpr std::size_t kOneMiB = 1024U * 1024U;
constexpr std::size_t kWholeFileLimit = 2U * kOneMiB;
std::atomic<std::uint64_t> nextDirectoryNumber{0};

class FingerprintServiceTests : public ::testing::Test {
protected:
    void SetUp() override {
        const auto timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
        root_ = std::filesystem::temp_directory_path() /
                ("dvs-fingerprint-" + std::to_string(timestamp) + "-" +
                 std::to_string(nextDirectoryNumber.fetch_add(1U)));
        std::error_code errorCode;
        std::filesystem::create_directories(root_, errorCode);
        ASSERT_FALSE(errorCode);
    }

    void TearDown() override {
        std::error_code errorCode;
        std::filesystem::remove_all(root_, errorCode);
    }

    [[nodiscard]] std::filesystem::path filePath(const std::string& name) const {
        return root_ / name;
    }

    void writeFile(const std::filesystem::path& path, const std::vector<char>& contents) const {
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(stream);
        stream.write(contents.data(), static_cast<std::streamsize>(contents.size()));
        ASSERT_TRUE(stream.good());
    }

private:
    std::filesystem::path root_;
};

TEST_F(FingerprintServiceTests, FingerprintsSmallFilesWithStandardSha256) {
    const std::filesystem::path source = filePath("small.bin");
    writeFile(source, {'a', 'b', 'c'});

    const auto identity = FingerprintService::fingerprint(source, 0);

    ASSERT_TRUE(identity);
    EXPECT_EQ(identity.value().byteSize, 3U);
    EXPECT_EQ(identity.value().fingerprintSha256,
              "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

TEST_F(FingerprintServiceTests, HashesEveryByteAtTheTwoMiBBoundary) {
    std::vector<char> original(kWholeFileLimit, 'a');
    std::vector<char> changed = original;
    changed[kOneMiB] = 'b';

    const std::filesystem::path originalPath = filePath("whole-original.bin");
    const std::filesystem::path changedPath = filePath("whole-changed.bin");
    writeFile(originalPath, original);
    writeFile(changedPath, changed);

    const auto originalIdentity =
        FingerprintService::fingerprint(originalPath, 0);
    const auto changedIdentity =
        FingerprintService::fingerprint(changedPath, 0);

    ASSERT_TRUE(originalIdentity);
    ASSERT_TRUE(changedIdentity);
    EXPECT_NE(originalIdentity.value().fingerprintSha256,
              changedIdentity.value().fingerprintSha256);
}

TEST_F(FingerprintServiceTests, HashesOnlyFirstAndLastMiBAboveTheBoundary) {
    std::vector<char> original(kWholeFileLimit + 1U, 'a');
    std::vector<char> middleChanged = original;
    std::vector<char> firstChanged = original;
    std::vector<char> lastChanged = original;
    middleChanged[kOneMiB] = 'b';
    firstChanged.front() = 'b';
    lastChanged.back() = 'b';

    const std::filesystem::path originalPath = filePath("sample-original.bin");
    const std::filesystem::path middlePath = filePath("sample-middle.bin");
    const std::filesystem::path firstPath = filePath("sample-first.bin");
    const std::filesystem::path lastPath = filePath("sample-last.bin");
    writeFile(originalPath, original);
    writeFile(middlePath, middleChanged);
    writeFile(firstPath, firstChanged);
    writeFile(lastPath, lastChanged);

    const auto originalIdentity =
        FingerprintService::fingerprint(originalPath, 0);
    const auto middleIdentity = FingerprintService::fingerprint(middlePath, 0);
    const auto firstIdentity = FingerprintService::fingerprint(firstPath, 0);
    const auto lastIdentity = FingerprintService::fingerprint(lastPath, 0);

    ASSERT_TRUE(originalIdentity);
    ASSERT_TRUE(middleIdentity);
    ASSERT_TRUE(firstIdentity);
    ASSERT_TRUE(lastIdentity);
    EXPECT_EQ(originalIdentity.value().fingerprintSha256, middleIdentity.value().fingerprintSha256);
    EXPECT_NE(originalIdentity.value().fingerprintSha256, firstIdentity.value().fingerprintSha256);
    EXPECT_NE(originalIdentity.value().fingerprintSha256, lastIdentity.value().fingerprintSha256);
}

TEST_F(FingerprintServiceTests, ReportsStableErrorsForMissingAndChangedSources) {
    const std::filesystem::path missing = filePath("missing.bin");
    const auto missingIdentity = FingerprintService::fingerprint(missing, 1);
    ASSERT_FALSE(missingIdentity);
    EXPECT_EQ(missingIdentity.error().code, domain::MediaErrorCode::kSourceMissing);
    EXPECT_EQ(missingIdentity.error().operation, domain::MediaOperation::kProjectPersistence);
    EXPECT_TRUE(missingIdentity.error().recoverable);

    const std::filesystem::path source = filePath("changed.bin");
    writeFile(source, {'a', 'b', 'c'});
    const auto expected = FingerprintService::fingerprint(source, 1);
    ASSERT_TRUE(expected);

    writeFile(source, {'x', 'y', 'z'});
    const auto status =
        FingerprintService::verify(source, expected.value(), 1);

    ASSERT_FALSE(status);
    EXPECT_EQ(status.error().code, domain::MediaErrorCode::kSourceFingerprintMismatch);
    EXPECT_EQ(status.error().operation, domain::MediaOperation::kProjectPersistence);
    EXPECT_TRUE(status.error().recoverable);
}

} // namespace
} // namespace dvs::persistence
