#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dvs::platform {

// Copyable cancellation signal for a blocking process run. The job supervisor owns the token and
// may request cancellation from a worker thread; ProcessRunner itself must never run on the GUI
// or render thread.
class CancellationToken final {
public:
    CancellationToken();

    void requestCancellation() const noexcept;
    [[nodiscard]] bool isCancellationRequested() const noexcept;

private:
    struct State;

    std::shared_ptr<State> state_;
};

enum class ProcessRunStatus {
    kExited,
    kCanceled,
    kFailed,
};

enum class ProcessRunErrorCode {
    kInvalidArguments,
    kPipeCreationFailed,
    kPipeConfigurationFailed,
    kJobCreationFailed,
    kJobConfigurationFailed,
    kHandleInheritanceConfigurationFailed,
    kProcessLaunchFailed,
    kJobAssignmentFailed,
    kResumeFailed,
    kWaitFailed,
    kOutputCaptureFailed,
    kOutputDrainTimedOut,
    kForcedTerminationFailed,
    kForcedTerminationTimedOut,
    kExitCodeReadFailed,
    kInternalFailure,
};

struct ProcessRunError final {
    ProcessRunErrorCode code = ProcessRunErrorCode::kInternalFailure;
    std::uint32_t nativeError = 0;
    std::string technicalDetail;
};

struct ProcessRunOptions final {
    // Output is retained as raw bytes; excess data is drained and discarded so a noisy child
    // cannot consume unbounded memory or block on a full pipe.
    std::size_t maxStandardOutputBytes = 1024U * 1024U;
    std::size_t maxStandardErrorBytes = 1024U * 1024U;
    CancellationToken cancellationToken;
};

struct ProcessRunResult final {
    ProcessRunStatus status = ProcessRunStatus::kFailed;
    std::optional<std::uint32_t> exitCode;
    bool gracefulCancellationSignaled = false;
    bool forceTerminated = false;
    std::string standardOutput;
    std::string standardError;
    bool standardOutputTruncated = false;
    bool standardErrorTruncated = false;
    std::optional<ProcessRunError> error;
};

// The caller supplies an executable followed by its individual arguments. ProcessRunner never
// invokes a shell; the only command line it creates is the Windows-required quoted form passed to
// CreateProcessW. The process inherits only its three stdio endpoints, is created suspended,
// assigned to a kill-on-close Job Object, then resumed. Cancellation writes "q\\n", waits exactly
// two seconds, then terminates the Job Object.
class ProcessRunner final {
public:
    [[nodiscard]] static ProcessRunResult run(const std::vector<std::wstring>& arguments,
                                              const ProcessRunOptions& options = {});

    // Exposed for deterministic component tests. It implements the CommandLineToArgvW-compatible
    // escaping rule used exclusively to convert the argument vector for CreateProcessW.
    [[nodiscard]] static std::wstring quoteWindowsArgument(std::wstring_view argument);
};

} // namespace dvs::platform
