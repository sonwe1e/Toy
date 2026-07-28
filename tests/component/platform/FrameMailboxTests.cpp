#include "dvs/platform/FrameBudget.h"
#include "dvs/platform/FrameMailbox.h"

#include "GpuFrameResource.h"
#include "GpuFrameSet.h"

#include <cstddef>
#include <cstdint>
#include <gtest/gtest.h>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

namespace dvs::platform {
namespace {

static_assert(!std::is_copy_constructible_v<GpuFrameAllocation>);
static_assert(!std::is_copy_assignable_v<GpuFrameAllocation>);
static_assert(std::is_move_constructible_v<GpuFrameAllocation>);
static_assert(!std::is_move_assignable_v<GpuFrameAllocation>);
static_assert(std::is_same_v<decltype(std::declval<const GpuFrameResource&>().backing()),
                             const IGpuFrameBacking&>);

class TestGpuBacking final : public IGpuFrameBacking {
public:
    explicit TestGpuBacking(const std::uint64_t token) noexcept : token_(token) {}

    [[nodiscard]] std::uint64_t token() const noexcept {
        return token_;
    }

private:
    std::uint64_t token_ = 0;
};

class ObservingGpuBacking final : public IGpuFrameBacking {
public:
    ObservingGpuBacking(FrameBudget& budget, std::size_t& reservedBytesAtDestruction) noexcept
        : budget_(budget), reservedBytesAtDestruction_(reservedBytesAtDestruction) {}

    ~ObservingGpuBacking() override {
        reservedBytesAtDestruction_ = budget_.reservedBytes();
    }

private:
    FrameBudget& budget_;
    std::size_t& reservedBytesAtDestruction_;
};

[[nodiscard]] application::FrameRequestContext makeContext(const std::uint64_t requestId,
                                                           const std::uint64_t generation,
                                                           const std::uint64_t playbackGeneration) {
    return application::FrameRequestContext{
        .playback =
            application::PlaybackRequestContext{
                .request =
                    application::RequestContext{
                        .sessionId = domain::SessionId{11},
                        .sessionEpoch = domain::SessionEpoch{3},
                        .requestId = domain::RequestId{requestId},
                    },
                .playbackGeneration = domain::PlaybackGeneration{playbackGeneration},
            },
        .deviceGeneration = domain::DeviceGeneration{generation},
    };
}

[[nodiscard]] std::shared_ptr<const GpuFrameResource>
makeResource(FrameBudget& budget,
             const std::size_t bytes,
             const std::uint64_t token,
             const application::FrameRequestContext& context,
             const domain::FrameId frameId,
             const domain::SourceId sourceId) {
    std::optional<FrameBudget::Reservation> reservation = budget.tryReserve(bytes);
    if (!reservation) {
        return {};
    }

    std::unique_ptr<const IGpuFrameBacking> backing = std::make_unique<TestGpuBacking>(token);
    GpuFrameAllocation allocation{std::move(*reservation), std::move(backing)};

    return GpuFrameResource::create(
        GpuFrameIdentity{
            .context = context,
            .frameId = frameId,
            .sourceId = sourceId,
        },
        application::FrameGeometry{
            .width = 4,
            .height = 2,
            .textureRegion = application::TextureRegion{},
        },
        domain::ColorMetadata{
            .matrix = domain::ColorMatrix::kBt709,
            .range = domain::ColorRange::kLimited,
            .matrixInferred = false,
        },
        std::move(allocation));
}

[[nodiscard]] std::shared_ptr<const GpuFrameSet>
makeSet(FrameBudget& budget,
        const application::FrameRequestContext& context,
        const std::int64_t frameId,
        const std::uint64_t firstToken) {
    const domain::FrameId identity{frameId};
    const std::shared_ptr<const GpuFrameResource> frameA =
        makeResource(budget, 10U, firstToken, context, identity, domain::SourceId{0});
    const std::shared_ptr<const GpuFrameResource> frameB =
        makeResource(budget, 10U, firstToken + 1U, context, identity, domain::SourceId{1});
    std::vector<GpuFrameSlot> slots{
        GpuFrameSlot{.sourceId = 0U, .frame = frameA},
        GpuFrameSlot{.sourceId = 1U, .frame = frameB},
    };
    return GpuFrameSet::create(context, identity, std::move(slots));
}

[[nodiscard]] std::uint64_t tokenOf(const std::shared_ptr<const GpuFrameResource>& resource) {
    const auto* const backing = dynamic_cast<const TestGpuBacking*>(&resource->backing());
    return backing != nullptr ? backing->token() : 0U;
}

TEST(FrameMailboxTests, ReplacesOnlyWithACompleteSetAndReleasesTheDisplacedBudget) {
    FrameBudget budget{40U};
    FrameMailbox mailbox{domain::DeviceGeneration{5}};

    std::shared_ptr<const GpuFrameSet> first = makeSet(budget, makeContext(1U, 5U, 7U), 0, 100U);
    ASSERT_NE(first, nullptr);
    ASSERT_EQ(mailbox.publish(first), FrameMailboxPublishResult::Published);
    std::uint64_t firstSerial = 0U;
    {
        const FrameMailboxReadResult firstRead = mailbox.tryLatest(domain::DeviceGeneration{5});
        ASSERT_EQ(firstRead.status, FrameMailboxReadStatus::Available);
        ASSERT_TRUE(firstRead.publication.has_value());
        firstSerial = firstRead.publication->publicationSerial;
    }
    first.reset();
    EXPECT_EQ(budget.reservedBytes(), 20U);

    std::shared_ptr<const GpuFrameSet> second = makeSet(budget, makeContext(2U, 5U, 8U), 1, 200U);
    ASSERT_NE(second, nullptr);
    ASSERT_EQ(budget.reservedBytes(), 40U);
    ASSERT_EQ(mailbox.publish(second), FrameMailboxPublishResult::Published);
    second.reset();
    EXPECT_EQ(budget.reservedBytes(), 20U);

    const FrameMailboxReadResult latestRead = mailbox.tryLatest(domain::DeviceGeneration{5});
    ASSERT_EQ(latestRead.status, FrameMailboxReadStatus::Available);
    ASSERT_TRUE(latestRead.publication.has_value());
    EXPECT_GT(latestRead.publication->publicationSerial, firstSerial);
    const std::shared_ptr<const GpuFrameSet> latest = latestRead.publication->set;
    ASSERT_NE(latest, nullptr);
    EXPECT_EQ(latest->frameId(), domain::FrameId{1});
    EXPECT_EQ(latest->frameCount(), 2U);
    const GpuFrameSlot* const slotA = latest->find(0U);
    const GpuFrameSlot* const slotB = latest->find(1U);
    ASSERT_NE(slotA, nullptr);
    ASSERT_NE(slotB, nullptr);
    EXPECT_EQ(tokenOf(slotA->frame), 200U);
    EXPECT_EQ(tokenOf(slotB->frame), 201U);
    EXPECT_EQ(latest->accountedBytes(), 20U);

    // Incomplete set (missing slot) should fail creation.
    std::vector<GpuFrameSlot> incompleteSlots{GpuFrameSlot{.sourceId = 0U, .frame = slotA->frame}};
    const auto incomplete = GpuFrameSet::create(latest->context(), latest->frameId(), std::move(incompleteSlots));
    EXPECT_EQ(incomplete, nullptr);
}

TEST(FrameMailboxTests, RejectsStaleGenerationsAndAdvancingGenerationClearsTheSet) {
    FrameBudget budget{40U};
    FrameMailbox mailbox{domain::DeviceGeneration{8}};

    std::shared_ptr<const GpuFrameSet> stale = makeSet(budget, makeContext(1U, 7U, 7U), 0, 10U);
    ASSERT_NE(stale, nullptr);
    EXPECT_EQ(mailbox.publish(stale), FrameMailboxPublishResult::DeviceGenerationMismatch);
    stale.reset();
    EXPECT_EQ(budget.reservedBytes(), 0U);

    std::shared_ptr<const GpuFrameSet> current = makeSet(budget, makeContext(2U, 8U, 8U), 1, 20U);
    ASSERT_NE(current, nullptr);
    ASSERT_EQ(mailbox.publish(current), FrameMailboxPublishResult::Published);
    current.reset();
    EXPECT_EQ(budget.reservedBytes(), 20U);

    EXPECT_TRUE(mailbox.advanceDeviceGeneration(domain::DeviceGeneration{9}));
    EXPECT_EQ(budget.reservedBytes(), 0U);
    EXPECT_EQ(mailbox.tryLatest(domain::DeviceGeneration{8}).status,
              FrameMailboxReadStatus::DeviceGenerationMismatch);
    EXPECT_EQ(mailbox.tryLatest(domain::DeviceGeneration{9}).status, FrameMailboxReadStatus::Empty);
    EXPECT_FALSE(mailbox.advanceDeviceGeneration(domain::DeviceGeneration{8}));

    std::shared_ptr<const GpuFrameSet> oldGeneration =
        makeSet(budget, makeContext(3U, 8U, 9U), 2, 30U);
    ASSERT_NE(oldGeneration, nullptr);
    EXPECT_EQ(mailbox.publish(oldGeneration), FrameMailboxPublishResult::DeviceGenerationMismatch);
}

TEST(FrameMailboxTests, SupportsScopedClearAndRevalidationImmediatelyBeforeDraw) {
    FrameBudget budget{80U};
    FrameMailbox mailbox{domain::DeviceGeneration{4}};
    const application::FrameRequestContext firstContext = makeContext(1U, 4U, 7U);
    const application::FrameRequestContext secondContext = makeContext(2U, 4U, 8U);

    const std::shared_ptr<const GpuFrameSet> first = makeSet(budget, firstContext, 0, 10U);
    const std::shared_ptr<const GpuFrameSet> second = makeSet(budget, secondContext, 1, 20U);
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    ASSERT_EQ(mailbox.publish(first), FrameMailboxPublishResult::Published);

    const FrameMailboxReadResult inFlightRead = mailbox.tryLatest(domain::DeviceGeneration{4});
    ASSERT_EQ(inFlightRead.status, FrameMailboxReadStatus::Available);
    ASSERT_TRUE(inFlightRead.publication.has_value());
    const FrameMailboxPublication inFlight = *inFlightRead.publication;
    ASSERT_EQ(inFlight.set, first);
    EXPECT_EQ(mailbox.validateForDraw(inFlight, domain::DeviceGeneration{4}),
              FrameMailboxDrawStatus::Current);

    ASSERT_EQ(mailbox.publish(second), FrameMailboxPublishResult::Published);
    EXPECT_EQ(mailbox.validateForDraw(inFlight, domain::DeviceGeneration{4}),
              FrameMailboxDrawStatus::Superseded);
    EXPECT_FALSE(mailbox.clear(firstContext.playback));

    const FrameMailboxReadResult secondRead = mailbox.tryLatest(domain::DeviceGeneration{4});
    ASSERT_EQ(secondRead.status, FrameMailboxReadStatus::Available);
    ASSERT_TRUE(secondRead.publication.has_value());
    EXPECT_EQ(secondRead.publication->set, second);

    application::PlaybackRequestContext requestAgnosticClear = secondContext.playback;
    requestAgnosticClear.request.requestId = domain::RequestId{0};
    EXPECT_TRUE(mailbox.clear(requestAgnosticClear));
    EXPECT_EQ(mailbox.tryLatest(domain::DeviceGeneration{4}).status, FrameMailboxReadStatus::Empty);
    EXPECT_EQ(mailbox.validateForDraw(*secondRead.publication, domain::DeviceGeneration{4}),
              FrameMailboxDrawStatus::Superseded);
    EXPECT_EQ(mailbox.publish(second), FrameMailboxPublishResult::PlaybackScopeRejected);

    application::FrameRequestContext foreignContext = makeContext(3U, 4U, 99U);
    foreignContext.playback.request.sessionId = domain::SessionId{12};
    const std::shared_ptr<const GpuFrameSet> foreign = makeSet(budget, foreignContext, 2, 30U);
    ASSERT_NE(foreign, nullptr);
    EXPECT_EQ(mailbox.publish(foreign), FrameMailboxPublishResult::PlaybackScopeRejected);
}

TEST(FrameMailboxTests, ShutdownDropsOwnershipAndRejectsFurtherPublication) {
    FrameBudget budget{40U};
    FrameMailbox mailbox{domain::DeviceGeneration{6}};

    std::shared_ptr<const GpuFrameSet> set = makeSet(budget, makeContext(1U, 6U, 7U), 0, 10U);
    ASSERT_NE(set, nullptr);
    ASSERT_EQ(mailbox.publish(set), FrameMailboxPublishResult::Published);
    set.reset();
    ASSERT_EQ(budget.reservedBytes(), 20U);

    mailbox.shutdown();
    EXPECT_TRUE(mailbox.isClosed());
    EXPECT_EQ(budget.reservedBytes(), 0U);
    EXPECT_EQ(mailbox.tryLatest(domain::DeviceGeneration{6}).status,
              FrameMailboxReadStatus::Closed);

    set = makeSet(budget, makeContext(2U, 6U, 8U), 1, 20U);
    ASSERT_NE(set, nullptr);
    EXPECT_EQ(mailbox.publish(set), FrameMailboxPublishResult::Closed);
    EXPECT_FALSE(mailbox.clear(set->context().playback));
    EXPECT_FALSE(mailbox.advanceDeviceGeneration(domain::DeviceGeneration{7}));
    set.reset();
    EXPECT_EQ(budget.reservedBytes(), 0U);
}

TEST(FrameMailboxTests, InvalidGpuResourceCreationDestroysBackingBeforeReturningItsReservation) {
    FrameBudget budget{16U};
    std::size_t reservedBytesAtBackingDestruction = 0U;
    std::optional<FrameBudget::Reservation> reservation = budget.tryReserve(16U);
    ASSERT_TRUE(reservation.has_value());
    std::unique_ptr<const IGpuFrameBacking> backing =
        std::make_unique<ObservingGpuBacking>(budget, reservedBytesAtBackingDestruction);
    GpuFrameAllocation allocation{std::move(*reservation), std::move(backing)};

    const std::shared_ptr<const GpuFrameResource> resource = GpuFrameResource::create(
        GpuFrameIdentity{
            .context = makeContext(1U, 1U, 1U),
            .frameId = domain::FrameId{0},
            .sourceId = domain::SourceId{0},
        },
        application::FrameGeometry{},
        domain::ColorMetadata{},
        std::move(allocation));
    EXPECT_EQ(resource, nullptr);
    EXPECT_EQ(reservedBytesAtBackingDestruction, 16U);
    EXPECT_EQ(budget.reservedBytes(), 0U);
}

TEST(FrameMailboxTests, SuccessfulGpuResourceDestroysBackingBeforeReturningItsReservation) {
    FrameBudget budget{16U};
    std::size_t reservedBytesAtBackingDestruction = 0U;
    std::optional<FrameBudget::Reservation> reservation = budget.tryReserve(16U);
    ASSERT_TRUE(reservation.has_value());
    std::unique_ptr<const IGpuFrameBacking> backing =
        std::make_unique<ObservingGpuBacking>(budget, reservedBytesAtBackingDestruction);
    GpuFrameAllocation allocation{std::move(*reservation), std::move(backing)};
    std::shared_ptr<const GpuFrameResource> ordered = GpuFrameResource::create(
        GpuFrameIdentity{
            .context = makeContext(1U, 3U, 1U),
            .frameId = domain::FrameId{0},
            .sourceId = domain::SourceId{0},
        },
        application::FrameGeometry{
            .width = 4U,
            .height = 2U,
            .textureRegion = application::TextureRegion{},
        },
        domain::ColorMetadata{},
        std::move(allocation));
    ASSERT_NE(ordered, nullptr);
    ordered.reset();
    EXPECT_EQ(reservedBytesAtBackingDestruction, 16U);
    EXPECT_EQ(budget.reservedBytes(), 0U);
}

TEST(FrameMailboxTests, SetDerivesIdentityAndRejectsContextFrameSourceAndAliasingErrors) {
    FrameBudget budget{80U};
    const application::FrameRequestContext context = makeContext(1U, 2U, 4U);
    const application::FrameRequestContext otherContext = makeContext(2U, 2U, 4U);
    const application::FrameRequestContext otherDevice = makeContext(1U, 3U, 4U);

    const std::shared_ptr<const GpuFrameResource> frameA =
        makeResource(budget, 8U, 1U, context, domain::FrameId{7}, domain::SourceId{0});
    const std::shared_ptr<const GpuFrameResource> frameB =
        makeResource(budget, 8U, 2U, context, domain::FrameId{7}, domain::SourceId{1});
    const std::shared_ptr<const GpuFrameResource> wrongContextB =
        makeResource(budget, 8U, 3U, otherContext, domain::FrameId{7}, domain::SourceId{1});
    const std::shared_ptr<const GpuFrameResource> wrongDeviceB =
        makeResource(budget, 8U, 4U, otherDevice, domain::FrameId{7}, domain::SourceId{1});
    const std::shared_ptr<const GpuFrameResource> wrongFrameB =
        makeResource(budget, 8U, 5U, context, domain::FrameId{8}, domain::SourceId{1});
    ASSERT_NE(frameA, nullptr);
    ASSERT_NE(frameB, nullptr);
    ASSERT_NE(wrongContextB, nullptr);
    ASSERT_NE(wrongDeviceB, nullptr);
    ASSERT_NE(wrongFrameB, nullptr);

    std::vector<GpuFrameSlot> validSlots{
        GpuFrameSlot{.sourceId = 0U, .frame = frameA},
        GpuFrameSlot{.sourceId = 1U, .frame = frameB},
    };
    const std::shared_ptr<const GpuFrameSet> set =
        GpuFrameSet::create(context, domain::FrameId{7}, std::move(validSlots));
    ASSERT_NE(set, nullptr);
    EXPECT_EQ(set->context(), context);
    EXPECT_EQ(set->frameId(), domain::FrameId{7});

    // Duplicate sourceId should fail.
    std::vector<GpuFrameSlot> duplicateSlots{
        GpuFrameSlot{.sourceId = 0U, .frame = frameA},
        GpuFrameSlot{.sourceId = 0U, .frame = frameB},
    };
    EXPECT_EQ(GpuFrameSet::create(context, domain::FrameId{7}, std::move(duplicateSlots)), nullptr);

    // Wrong context should fail.
    std::vector<GpuFrameSlot> wrongContextSlots{
        GpuFrameSlot{.sourceId = 0U, .frame = frameA},
        GpuFrameSlot{.sourceId = 1U, .frame = wrongContextB},
    };
    EXPECT_EQ(GpuFrameSet::create(context, domain::FrameId{7}, std::move(wrongContextSlots)), nullptr);

    // Wrong device should fail.
    std::vector<GpuFrameSlot> wrongDeviceSlots{
        GpuFrameSlot{.sourceId = 0U, .frame = frameA},
        GpuFrameSlot{.sourceId = 1U, .frame = wrongDeviceB},
    };
    EXPECT_EQ(GpuFrameSet::create(context, domain::FrameId{7}, std::move(wrongDeviceSlots)), nullptr);

    // Wrong frameId should fail.
    std::vector<GpuFrameSlot> wrongFrameSlots{
        GpuFrameSlot{.sourceId = 0U, .frame = frameA},
        GpuFrameSlot{.sourceId = 1U, .frame = wrongFrameB},
    };
    EXPECT_EQ(GpuFrameSet::create(context, domain::FrameId{7}, std::move(wrongFrameSlots)), nullptr);

    // Invalid frameId should fail.
    std::vector<GpuFrameSlot> invalidFrameSlots{
        GpuFrameSlot{.sourceId = 0U, .frame = frameA},
        GpuFrameSlot{.sourceId = 1U, .frame = frameB},
    };
    EXPECT_EQ(GpuFrameSet::create(context, domain::FrameId{-1}, std::move(invalidFrameSlots)), nullptr);
}

} // namespace
} // namespace dvs::platform
