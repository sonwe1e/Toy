#include "GpuFrameResource.h"

#include <cassert>
#include <new>
#include <utility>

namespace dvs::platform {

static_assert(std::atomic<GpuFrameResource*>::is_always_lock_free,
              "Deferred GPU retirement must not block the render thread.");

GpuFrameRetirementDomain::~GpuFrameRetirementDomain() {
    assert(retiredHead_.load(std::memory_order_relaxed) == nullptr);
    assert(liveResourceCount_.load(std::memory_order_relaxed) == 0U);
}

void GpuFrameRetirementDomain::registerResource() noexcept {
    static_cast<void>(liveResourceCount_.fetch_add(1U, std::memory_order_relaxed));
}

void GpuFrameRetirementDomain::retire(const GpuFrameResource* const resource) noexcept {
    auto* const mutableResource = const_cast<GpuFrameResource*>(resource);
    GpuFrameResource* head = retiredHead_.load(std::memory_order_relaxed);
    do {
        mutableResource->retirementNext_.store(head, std::memory_order_relaxed);
    } while (!retiredHead_.compare_exchange_weak(
        head, mutableResource, std::memory_order_release, std::memory_order_relaxed));
    activity_.notify_one();
}

std::size_t GpuFrameRetirementDomain::drainRetired() noexcept {
    GpuFrameResource* resource = retiredHead_.exchange(nullptr, std::memory_order_acquire);
    std::size_t drained = 0U;
    while (resource != nullptr) {
        GpuFrameResource* const next =
            resource->retirementNext_.exchange(nullptr, std::memory_order_relaxed);
        delete resource;
        ++drained;
        static_cast<void>(liveResourceCount_.fetch_sub(1U, std::memory_order_acq_rel));
        resource = next;
    }
    return drained;
}

std::size_t GpuFrameRetirementDomain::liveResourceCount() const noexcept {
    return liveResourceCount_.load(std::memory_order_acquire);
}

bool GpuFrameRetirementDomain::hasRetiredResources() const noexcept {
    return retiredHead_.load(std::memory_order_acquire) != nullptr;
}

void GpuFrameRetirementDomain::notifyActivity() noexcept {
    activity_.notify_all();
}

std::condition_variable& GpuFrameRetirementDomain::activityCondition() noexcept {
    return activity_;
}

IGpuFrameBacking::~IGpuFrameBacking() = default;

GpuFrameAllocation::GpuFrameAllocation(FrameBudget::Reservation reservation,
                                       std::unique_ptr<const IGpuFrameBacking> backing) noexcept
    : reservation_(std::move(reservation)), backing_(std::move(backing)) {}

bool GpuFrameAllocation::isValid() const noexcept {
    return reservation_ && reservation_.bytes() != 0U && backing_ != nullptr;
}

std::size_t GpuFrameAllocation::accountedBytes() const noexcept {
    return reservation_.bytes();
}

const IGpuFrameBacking& GpuFrameAllocation::backing() const noexcept {
    return *backing_;
}

std::shared_ptr<const GpuFrameResource>
GpuFrameResource::create(GpuFrameIdentity identity,
                         const application::FrameGeometry geometry,
                         const domain::ColorMetadata colorMetadata,
                         GpuFrameAllocation allocation) noexcept {
    if (!identity.frameId.isValid() || !geometry.isValid() || !colorMetadata.isValid() ||
        !allocation.isValid()) {
        return {};
    }

    try {
        return std::shared_ptr<const GpuFrameResource>{new GpuFrameResource(
            std::move(identity), geometry, colorMetadata, std::move(allocation))};
    } catch (...) {
        return {};
    }
}

std::shared_ptr<const GpuFrameResource> GpuFrameResource::createDeferred(
    GpuFrameIdentity identity,
    const application::FrameGeometry geometry,
    const domain::ColorMetadata colorMetadata,
    GpuFrameAllocation allocation,
    std::shared_ptr<GpuFrameRetirementDomain> retirementDomain) noexcept {
    if (!identity.frameId.isValid() || !geometry.isValid() || !colorMetadata.isValid() ||
        !allocation.isValid() || !retirementDomain) {
        return {};
    }

    auto* const resource = new (std::nothrow)
        GpuFrameResource(std::move(identity), geometry, colorMetadata, std::move(allocation));
    if (resource == nullptr) {
        return {};
    }

    retirementDomain->registerResource();
    try {
        return std::shared_ptr<const GpuFrameResource>{
            resource,
            [domain = std::move(retirementDomain)](const GpuFrameResource* const retired) noexcept {
                domain->retire(retired);
            },
        };
    } catch (...) {
        // shared_ptr invokes the supplied deleter if control-block allocation fails. The resource
        // is therefore already queued for the actor and must not be deleted here.
        return {};
    }
}

GpuFrameResource::GpuFrameResource(GpuFrameIdentity identity,
                                   const application::FrameGeometry geometry,
                                   const domain::ColorMetadata colorMetadata,
                                   GpuFrameAllocation&& allocation) noexcept
    : identity_(std::move(identity)), geometry_(geometry), colorMetadata_(colorMetadata),
      allocation_(std::move(allocation)) {}

const GpuFrameIdentity& GpuFrameResource::identity() const noexcept {
    return identity_;
}

const application::FrameRequestContext& GpuFrameResource::context() const noexcept {
    return identity_.context;
}

const domain::FrameId& GpuFrameResource::frameId() const noexcept {
    return identity_.frameId;
}

domain::SourceId GpuFrameResource::sourceId() const noexcept {
    return identity_.sourceId;
}

const application::FrameGeometry& GpuFrameResource::geometry() const noexcept {
    return geometry_;
}

const domain::ColorMetadata& GpuFrameResource::colorMetadata() const noexcept {
    return colorMetadata_;
}

domain::DeviceGeneration GpuFrameResource::deviceGeneration() const noexcept {
    return identity_.context.deviceGeneration;
}

const IGpuFrameBacking& GpuFrameResource::backing() const noexcept {
    return allocation_.backing();
}

std::size_t GpuFrameResource::accountedBytes() const noexcept {
    return allocation_.accountedBytes();
}

} // namespace dvs::platform
