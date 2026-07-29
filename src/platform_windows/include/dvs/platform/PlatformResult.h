#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace dvs::platform {

// Stable, adapter-neutral failure categories. Windows error values and handles remain private to
// platform implementation files; adapters translate these categories to application errors.
enum class PlatformErrorCode {
    kInvalidArgument,
    kInvalidPath,
    kKnownFolderUnavailable,
    kDirectoryUnavailable,
    kDirectoryCreateFailed,
    kInvalidTransactionIdentity,
    kTemporaryFileCreateFailed,
    kWriteFailed,
    kFlushFailed,
    kCloseFailed,
    kTargetAlreadyExists,
    kTargetMissing,
    kReplaceFailed,
    kRecoveryRequired,
    kPublishFailed,
    kCleanupFailed,
    kInvalidState,
};

[[nodiscard]] constexpr std::string_view stableId(const PlatformErrorCode code) noexcept {
    switch (code) {
    case PlatformErrorCode::kInvalidArgument:
        return "invalid_argument";
    case PlatformErrorCode::kInvalidPath:
        return "invalid_path";
    case PlatformErrorCode::kKnownFolderUnavailable:
        return "known_folder_unavailable";
    case PlatformErrorCode::kDirectoryUnavailable:
        return "directory_unavailable";
    case PlatformErrorCode::kDirectoryCreateFailed:
        return "directory_create_failed";
    case PlatformErrorCode::kInvalidTransactionIdentity:
        return "invalid_transaction_identity";
    case PlatformErrorCode::kTemporaryFileCreateFailed:
        return "temporary_file_create_failed";
    case PlatformErrorCode::kWriteFailed:
        return "write_failed";
    case PlatformErrorCode::kFlushFailed:
        return "flush_failed";
    case PlatformErrorCode::kCloseFailed:
        return "close_failed";
    case PlatformErrorCode::kTargetAlreadyExists:
        return "target_already_exists";
    case PlatformErrorCode::kTargetMissing:
        return "target_missing";
    case PlatformErrorCode::kReplaceFailed:
        return "replace_failed";
    case PlatformErrorCode::kRecoveryRequired:
        return "recovery_required";
    case PlatformErrorCode::kPublishFailed:
        return "publish_failed";
    case PlatformErrorCode::kCleanupFailed:
        return "cleanup_failed";
    case PlatformErrorCode::kInvalidState:
        return "invalid_state";
    }

    return "unknown";
}

struct PlatformError final {
    PlatformErrorCode code = PlatformErrorCode::kInvalidArgument;
    std::filesystem::path path;
    std::string technicalDetail;
};

template <typename TValue> class [[nodiscard]] PlatformResult final {
public:
    [[nodiscard]] static PlatformResult success(TValue value) {
        return PlatformResult{std::in_place_index<0>, std::move(value)};
    }

    [[nodiscard]] static PlatformResult failure(PlatformError error) {
        return PlatformResult{std::in_place_index<1>, std::move(error)};
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

    [[nodiscard]] PlatformError& error() & {
        return std::get<ErrorState>(storage_).value;
    }

    [[nodiscard]] const PlatformError& error() const& {
        return std::get<ErrorState>(storage_).value;
    }

private:
    // Keep the failure alternative distinct even when TValue itself is PlatformError.
    struct ErrorState final {
        PlatformError value;
    };

    template <typename TValueOrError>
    PlatformResult(const std::in_place_index_t<0> index, TValueOrError&& value)
        : storage_(index, std::forward<TValueOrError>(value)) {}

    PlatformResult(const std::in_place_index_t<1> index, PlatformError error)
        : storage_(index, ErrorState{.value = std::move(error)}) {}

    std::variant<TValue, ErrorState> storage_;
};

template <> class [[nodiscard]] PlatformResult<void> final {
public:
    [[nodiscard]] static PlatformResult success() {
        return PlatformResult{std::in_place_index<0>};
    }

    [[nodiscard]] static PlatformResult failure(PlatformError error) {
        return PlatformResult{std::in_place_index<1>, std::move(error)};
    }

    [[nodiscard]] bool hasValue() const noexcept {
        return std::holds_alternative<std::monostate>(storage_);
    }

    [[nodiscard]] explicit operator bool() const noexcept {
        return hasValue();
    }

    [[nodiscard]] PlatformError& error() & {
        return std::get<PlatformError>(storage_);
    }

    [[nodiscard]] const PlatformError& error() const& {
        return std::get<PlatformError>(storage_);
    }

private:
    explicit PlatformResult(const std::in_place_index_t<0> index) : storage_(index) {}

    template <typename TValueOrError>
    PlatformResult(const std::in_place_index_t<1> index, TValueOrError&& value)
        : storage_(index, std::forward<TValueOrError>(value)) {}

    std::variant<std::monostate, PlatformError> storage_;
};

using PlatformStatus = PlatformResult<void>;

} // namespace dvs::platform
