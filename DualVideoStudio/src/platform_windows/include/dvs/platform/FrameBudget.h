#pragma once

#include <cstddef>
#include <memory>
#include <optional>

namespace dvs::platform {

// Thread-safe byte accounting shared by CPU and GPU frame resources. A reservation releases its
// bytes exactly once when reset, moved-from, or destroyed.
class FrameBudget final {
private:
    struct State;

public:
    class Reservation final {
    public:
        Reservation() noexcept = default;
        ~Reservation();

        Reservation(const Reservation&) = delete;
        Reservation& operator=(const Reservation&) = delete;

        Reservation(Reservation&& other) noexcept;
        Reservation& operator=(Reservation&& other) noexcept;

        [[nodiscard]] std::size_t bytes() const noexcept;
        [[nodiscard]] explicit operator bool() const noexcept;

        void reset() noexcept;

    private:
        friend class FrameBudget;

        Reservation(std::shared_ptr<State> state, std::size_t bytes) noexcept;

        std::shared_ptr<State> state_;
        std::size_t bytes_ = 0;
    };

    explicit FrameBudget(std::size_t capacityBytes);

    FrameBudget(const FrameBudget&) = delete;
    FrameBudget& operator=(const FrameBudget&) = delete;
    FrameBudget(FrameBudget&&) = delete;
    FrameBudget& operator=(FrameBudget&&) = delete;

    [[nodiscard]] std::optional<Reservation> tryReserve(std::size_t bytes) noexcept;
    [[nodiscard]] std::size_t capacityBytes() const noexcept;
    [[nodiscard]] std::size_t reservedBytes() const noexcept;
    [[nodiscard]] std::size_t availableBytes() const noexcept;

private:
    std::shared_ptr<State> state_;
};

} // namespace dvs::platform
