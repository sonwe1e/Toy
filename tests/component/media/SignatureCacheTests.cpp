#include "dvs/domain/MediaDescriptor.h"

#include "SignatureCache.h"

#include <cstdint>
#include <filesystem>
#include <gtest/gtest.h>
#include <string>

namespace dvs::media::internal {
namespace {

[[nodiscard]] domain::MediaDescriptor descriptor(const std::string& fingerprint) {
    const std::string sha256 = std::string(63U, '0') + fingerprint;
    return domain::MediaDescriptor{
        .normalizedPath = std::filesystem::path{"source.mp4"},
        .extent = domain::MediaExtent{.width = 1'920, .height = 1'080},
        .duration = domain::MediaTime{1'000'000},
        .sourceIdentity =
            domain::SourceFileIdentity{
                .byteSize = 1U,
                .modifiedUtcMilliseconds = 1,
                .fingerprintSha256 = sha256,
            },
    };
}

[[nodiscard]] application::FrameLumaSignature signature(const std::int64_t frame) {
    application::FrameLumaSignature value{.frameId = domain::FrameId{frame}};
    value.luma.fill(static_cast<std::uint8_t>(frame));
    return value;
}

TEST(SignatureCacheTests, EvictsOldestEntriesAtTheConfiguredCapacity) {
    SignatureCache cache{3U};
    const domain::MediaDescriptor sourceA = descriptor("a");
    const domain::MediaDescriptor sourceB = descriptor("b");

    cache.store(sourceA, signature(0));
    cache.store(sourceA, signature(1));
    cache.store(sourceB, signature(0));
    ASSERT_EQ(cache.entryCountForTesting(), 3U);

    cache.store(sourceA, signature(2));

    EXPECT_EQ(cache.entryCountForTesting(), 3U);
    EXPECT_FALSE(cache.find(sourceA, domain::FrameId{0}).has_value());
    EXPECT_TRUE(cache.find(sourceA, domain::FrameId{1}).has_value());
    EXPECT_TRUE(cache.find(sourceA, domain::FrameId{2}).has_value());
    EXPECT_TRUE(cache.find(sourceB, domain::FrameId{0}).has_value());
}

TEST(SignatureCacheTests, ReplacingAnEntryDoesNotConsumeCapacityOrChangeEvictionOrder) {
    SignatureCache cache{2U};
    const domain::MediaDescriptor source = descriptor("c");

    cache.store(source, signature(0));
    cache.store(source, signature(1));
    cache.store(source, signature(0));
    ASSERT_EQ(cache.entryCountForTesting(), 2U);

    cache.store(source, signature(2));

    EXPECT_FALSE(cache.find(source, domain::FrameId{0}).has_value());
    EXPECT_TRUE(cache.find(source, domain::FrameId{1}).has_value());
    EXPECT_TRUE(cache.find(source, domain::FrameId{2}).has_value());
}

} // namespace
} // namespace dvs::media::internal
