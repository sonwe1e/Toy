#pragma once

#include "dvs/domain/MediaError.h"

#include <utility>
#include <variant>

namespace dvs::domain {

// C++20 replacement for std::expected. All construction is explicit through success/failure.
// The error alternative is wrapped so Result<MediaError> remains a valid, unambiguous result type.
template <typename TValue> class [[nodiscard]] Result final {
public:
    [[nodiscard]] static Result success(TValue value) {
        return Result{std::in_place_index<0>, std::move(value)};
    }

    [[nodiscard]] static Result failure(MediaError error) {
        return Result{std::in_place_index<1>, std::move(error)};
    }

    [[nodiscard]] bool hasValue() const noexcept {
        return std::holds_alternative<TValue>(storage_);
    }

    [[nodiscard]] explicit operator bool() const noexcept {
        return hasValue();
    }

    [[nodiscard]] TValue& value() & {
        return std::get<TValue>(storage_);
    }

    [[nodiscard]] const TValue& value() const& {
        return std::get<TValue>(storage_);
    }

    [[nodiscard]] TValue&& value() && {
        return std::get<TValue>(std::move(storage_));
    }

    [[nodiscard]] MediaError& error() & {
        return std::get<ErrorState>(storage_).value;
    }

    [[nodiscard]] const MediaError& error() const& {
        return std::get<ErrorState>(storage_).value;
    }

private:
    struct ErrorState final {
        MediaError value;
    };

    template <typename TValueOrError>
    Result(const std::in_place_index_t<0> index, TValueOrError&& value)
        : storage_(index, std::forward<TValueOrError>(value)) {}

    Result(const std::in_place_index_t<1> index, MediaError error)
        : storage_(index, ErrorState{.value = std::move(error)}) {}

    std::variant<TValue, ErrorState> storage_;
};

template <> class [[nodiscard]] Result<void> final {
public:
    [[nodiscard]] static Result success() {
        return Result{std::in_place_index<0>};
    }

    [[nodiscard]] static Result failure(MediaError error) {
        return Result{std::in_place_index<1>, std::move(error)};
    }

    [[nodiscard]] bool hasValue() const noexcept {
        return std::holds_alternative<std::monostate>(storage_);
    }

    [[nodiscard]] explicit operator bool() const noexcept {
        return hasValue();
    }

    [[nodiscard]] MediaError& error() & {
        return std::get<MediaError>(storage_);
    }

    [[nodiscard]] const MediaError& error() const& {
        return std::get<MediaError>(storage_);
    }

private:
    explicit Result(const std::in_place_index_t<0> index) : storage_(index) {}

    template <typename TValueOrError>
    Result(const std::in_place_index_t<1> index, TValueOrError&& value)
        : storage_(index, std::forward<TValueOrError>(value)) {}

    std::variant<std::monostate, MediaError> storage_;
};

using Status = Result<void>;

} // namespace dvs::domain
