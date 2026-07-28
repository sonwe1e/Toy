#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>

namespace dvs::application {

// Normalized texture coordinates allow a proxy-backed handle to reference a region of a shared
// resource. They intentionally do not expose a graphics API type.
struct TextureRegion final {
    float left = 0.0F;
    float top = 0.0F;
    float right = 1.0F;
    float bottom = 1.0F;

    [[nodiscard]] constexpr bool isValid() const noexcept {
        return left >= 0.0F && top >= 0.0F && right <= 1.0F && bottom <= 1.0F && left < right &&
               top < bottom;
    }

    [[nodiscard]] constexpr bool operator==(const TextureRegion&) const = default;
};

struct FrameGeometry final {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    TextureRegion textureRegion{};

    [[nodiscard]] constexpr bool isValid() const noexcept {
        return width != 0 && height != 0 && textureRegion.isValid();
    }

    [[nodiscard]] constexpr bool operator==(const FrameGeometry&) const = default;
};

// Concrete frame resources are owned by platform adapters. This deliberately empty polymorphic
// base is the only resource type visible to application code; FFmpeg, Qt, and D3D types stay in
// adapter implementation files.
class IFrameResource {
public:
    virtual ~IFrameResource() = default;

protected:
    IFrameResource() = default;
};

class FrameHandle final {
public:
    [[nodiscard]] static std::optional<FrameHandle>
    create(std::shared_ptr<const IFrameResource> resource,
           FrameGeometry geometry,
           std::size_t accountedBytes) noexcept;

    [[nodiscard]] const std::shared_ptr<const IFrameResource>& resource() const noexcept;
    [[nodiscard]] const FrameGeometry& geometry() const noexcept;
    [[nodiscard]] std::size_t accountedBytes() const noexcept;
    [[nodiscard]] bool isValid() const noexcept;

private:
    FrameHandle(std::shared_ptr<const IFrameResource> resource,
                FrameGeometry geometry,
                std::size_t accountedBytes) noexcept;

    std::shared_ptr<const IFrameResource> resource_;
    FrameGeometry geometry_;
    std::size_t accountedBytes_ = 0;
};

inline std::optional<FrameHandle>
FrameHandle::create(std::shared_ptr<const IFrameResource> resource,
                    const FrameGeometry geometry,
                    const std::size_t accountedBytes) noexcept {
    if (!resource || !geometry.isValid()) {
        return std::nullopt;
    }

    return FrameHandle{std::move(resource), geometry, accountedBytes};
}

inline FrameHandle::FrameHandle(std::shared_ptr<const IFrameResource> resource,
                                const FrameGeometry geometry,
                                const std::size_t accountedBytes) noexcept
    : resource_(std::move(resource)), geometry_(geometry), accountedBytes_(accountedBytes) {}

inline const std::shared_ptr<const IFrameResource>& FrameHandle::resource() const noexcept {
    return resource_;
}

inline const FrameGeometry& FrameHandle::geometry() const noexcept {
    return geometry_;
}

inline std::size_t FrameHandle::accountedBytes() const noexcept {
    return accountedBytes_;
}

inline bool FrameHandle::isValid() const noexcept {
    return resource_ != nullptr && geometry_.isValid();
}

} // namespace dvs::application
