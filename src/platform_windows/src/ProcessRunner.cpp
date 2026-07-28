#include "dvs/platform/ProcessRunner.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <exception>
#include <limits>
#include <string>
#include <utility>
#include <vector>
#include <windows.h>

namespace dvs::platform {

struct CancellationToken::State final {
    std::atomic_bool cancellationRequested = false;
};

CancellationToken::CancellationToken() : state_(std::make_shared<State>()) {}

void CancellationToken::requestCancellation() const noexcept {
    if (state_) {
        state_->cancellationRequested.store(true, std::memory_order_release);
    }
}

bool CancellationToken::isCancellationRequested() const noexcept {
    return state_ && state_->cancellationRequested.load(std::memory_order_acquire);
}

namespace {

constexpr DWORD kPipeBufferBytes = 16U * 1024U;
constexpr DWORD kPipeReadChunkBytes = 4U * 1024U;
// Read at most one pipe buffer from each stream before returning to cancellation and process
// state checks. Both bytes and read operations are capped so a chatty child cannot monopolize the
// supervisor loop with either large or unusually small writes.
constexpr DWORD kDrainBytesPerPipePass = kPipeBufferBytes;
constexpr DWORD kDrainReadOperationsPerPipePass = kDrainBytesPerPipePass / kPipeReadChunkBytes;
constexpr DWORD kPollMilliseconds = 20U;
constexpr auto kGracefulCancellationDelay = std::chrono::seconds{2};
constexpr auto kForcedTerminationWait = std::chrono::seconds{1};
constexpr auto kOutputDrainWait = std::chrono::seconds{1};
constexpr DWORD kForcedTerminationWaitMilliseconds =
    static_cast<DWORD>(kForcedTerminationWait.count() * 1000);

class UniqueHandle final {
public:
    UniqueHandle() noexcept = default;
    explicit UniqueHandle(const HANDLE handle) noexcept : handle_(handle) {}
    ~UniqueHandle() {
        reset();
    }

    UniqueHandle(const UniqueHandle&) = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;

    UniqueHandle(UniqueHandle&& other) noexcept : handle_(std::exchange(other.handle_, nullptr)) {}

    UniqueHandle& operator=(UniqueHandle&& other) noexcept {
        if (this != &other) {
            reset(std::exchange(other.handle_, nullptr));
        }
        return *this;
    }

    [[nodiscard]] HANDLE get() const noexcept {
        return handle_;
    }

    [[nodiscard]] bool isValid() const noexcept {
        return handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE;
    }

    void reset(HANDLE replacement = nullptr) noexcept {
        if (isValid()) {
            CloseHandle(handle_);
        }
        handle_ = replacement;
    }

private:
    HANDLE handle_ = nullptr;
};

class ProcThreadAttributeList final {
public:
    ProcThreadAttributeList() noexcept = default;

    ~ProcThreadAttributeList() {
        reset();
    }

    ProcThreadAttributeList(const ProcThreadAttributeList&) = delete;
    ProcThreadAttributeList& operator=(const ProcThreadAttributeList&) = delete;

    [[nodiscard]] bool initialize(std::array<HANDLE, 3>& inheritedHandles,
                                  DWORD& nativeError) noexcept {
        SIZE_T byteCount = 0;
        if (InitializeProcThreadAttributeList(nullptr, 1, 0, &byteCount) != FALSE) {
            nativeError = ERROR_INVALID_DATA;
            return false;
        }
        nativeError = GetLastError();
        if (nativeError != ERROR_INSUFFICIENT_BUFFER) {
            return false;
        }

        list_ =
            static_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(HeapAlloc(GetProcessHeap(), 0, byteCount));
        if (list_ == nullptr) {
            nativeError = ERROR_NOT_ENOUGH_MEMORY;
            return false;
        }

        if (InitializeProcThreadAttributeList(list_, 1, 0, &byteCount) == FALSE) {
            nativeError = GetLastError();
            reset();
            return false;
        }
        initialized_ = true;

        if (UpdateProcThreadAttribute(list_,
                                      0,
                                      PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                                      inheritedHandles.data(),
                                      sizeof(inheritedHandles),
                                      nullptr,
                                      nullptr) == FALSE) {
            nativeError = GetLastError();
            reset();
            return false;
        }
        return true;
    }

    [[nodiscard]] LPPROC_THREAD_ATTRIBUTE_LIST get() const noexcept {
        return list_;
    }

private:
    void reset() noexcept {
        if (list_ == nullptr) {
            return;
        }
        if (initialized_) {
            DeleteProcThreadAttributeList(list_);
        }
        HeapFree(GetProcessHeap(), 0, list_);
        list_ = nullptr;
        initialized_ = false;
    }

    LPPROC_THREAD_ATTRIBUTE_LIST list_ = nullptr;
    bool initialized_ = false;
};

struct CapturedStream final {
    std::string bytes;
    bool truncated = false;
    bool allocationFailed = false;
    std::optional<DWORD> readError;
};

struct ChildPipes final {
    UniqueHandle childStandardInput;
    UniqueHandle parentStandardInput;
    UniqueHandle parentStandardOutput;
    UniqueHandle childStandardOutput;
    UniqueHandle parentStandardError;
    UniqueHandle childStandardError;
};

enum class PipeDrainState {
    kOpen,
    kClosed,
    kFailed,
};

enum class PipeSetupState {
    kSucceeded,
    kCreationFailed,
    kConfigurationFailed,
};

[[nodiscard]] ProcessRunResult
makeFailure(const ProcessRunErrorCode code, const DWORD nativeError, std::string technicalDetail) {
    ProcessRunResult result;
    result.status = ProcessRunStatus::kFailed;
    result.error = ProcessRunError{
        .code = code,
        .nativeError = static_cast<std::uint32_t>(nativeError),
        .technicalDetail = std::move(technicalDetail),
    };
    return result;
}

[[nodiscard]] bool containsEmbeddedNull(const std::wstring_view value) noexcept {
    return value.find(L'\0') != std::wstring_view::npos;
}

[[nodiscard]] bool requiresWindowsQuoting(const std::wstring_view value) noexcept {
    if (value.empty()) {
        return true;
    }

    return std::any_of(value.begin(), value.end(), [](const wchar_t character) {
        return character == L' ' || character == L'\t' || character == L'\n' ||
               character == L'\r' || character == L'\v' || character == L'"';
    });
}

[[nodiscard]] std::wstring buildCommandLine(const std::vector<std::wstring>& arguments) {
    std::wstring commandLine;
    for (std::size_t index = 0; index < arguments.size(); ++index) {
        if (index != 0) {
            commandLine.push_back(L' ');
        }
        commandLine.append(ProcessRunner::quoteWindowsArgument(arguments[index]));
    }
    return commandLine;
}

[[nodiscard]] bool configureParentHandle(const HANDLE handle, DWORD& nativeError) noexcept {
    if (SetHandleInformation(handle, HANDLE_FLAG_INHERIT, 0) != FALSE) {
        return true;
    }
    nativeError = GetLastError();
    return false;
}

[[nodiscard]] PipeSetupState createChildPipes(ChildPipes& pipes, DWORD& nativeError) noexcept {
    SECURITY_ATTRIBUTES attributes{};
    attributes.nLength = sizeof(attributes);
    attributes.bInheritHandle = TRUE;

    HANDLE childStandardInput = nullptr;
    HANDLE parentStandardInput = nullptr;
    if (CreatePipe(&childStandardInput, &parentStandardInput, &attributes, kPipeBufferBytes) ==
        FALSE) {
        nativeError = GetLastError();
        return PipeSetupState::kCreationFailed;
    }
    pipes.childStandardInput.reset(childStandardInput);
    pipes.parentStandardInput.reset(parentStandardInput);
    if (!configureParentHandle(pipes.parentStandardInput.get(), nativeError)) {
        return PipeSetupState::kConfigurationFailed;
    }

    HANDLE parentStandardOutput = nullptr;
    HANDLE childStandardOutput = nullptr;
    if (CreatePipe(&parentStandardOutput, &childStandardOutput, &attributes, kPipeBufferBytes) ==
        FALSE) {
        nativeError = GetLastError();
        return PipeSetupState::kCreationFailed;
    }
    pipes.parentStandardOutput.reset(parentStandardOutput);
    pipes.childStandardOutput.reset(childStandardOutput);
    if (!configureParentHandle(pipes.parentStandardOutput.get(), nativeError)) {
        return PipeSetupState::kConfigurationFailed;
    }

    HANDLE parentStandardError = nullptr;
    HANDLE childStandardError = nullptr;
    if (CreatePipe(&parentStandardError, &childStandardError, &attributes, kPipeBufferBytes) ==
        FALSE) {
        nativeError = GetLastError();
        return PipeSetupState::kCreationFailed;
    }
    pipes.parentStandardError.reset(parentStandardError);
    pipes.childStandardError.reset(childStandardError);
    return configureParentHandle(pipes.parentStandardError.get(), nativeError)
               ? PipeSetupState::kSucceeded
               : PipeSetupState::kConfigurationFailed;
}

void appendCapturedBytes(CapturedStream& captured,
                         const char* bytes,
                         const std::size_t byteCount,
                         const std::size_t maximumBytes) noexcept {
    if (byteCount == 0) {
        return;
    }

    if (captured.allocationFailed || captured.bytes.size() >= maximumBytes) {
        captured.truncated = true;
        return;
    }

    const std::size_t retainedBytes = std::min(byteCount, maximumBytes - captured.bytes.size());
    try {
        captured.bytes.append(bytes, retainedBytes);
    } catch (const std::exception&) {
        captured.allocationFailed = true;
        captured.truncated = true;
        return;
    }

    if (retainedBytes != byteCount) {
        captured.truncated = true;
    }
}

[[nodiscard]] PipeDrainState drainAvailable(UniqueHandle& pipe,
                                            CapturedStream& captured,
                                            const std::size_t maximumBytes) noexcept {
    std::array<char, kPipeReadChunkBytes> buffer{};
    DWORD remainingBudget = kDrainBytesPerPipePass;
    DWORD remainingReadOperations = kDrainReadOperationsPerPipePass;

    while (pipe.isValid() && remainingBudget != 0U && remainingReadOperations != 0U) {
        DWORD availableBytes = 0;
        if (PeekNamedPipe(pipe.get(), nullptr, 0, nullptr, &availableBytes, nullptr) == FALSE) {
            const DWORD nativeError = GetLastError();
            if (nativeError == ERROR_BROKEN_PIPE) {
                pipe.reset();
                return PipeDrainState::kClosed;
            }
            captured.readError = nativeError;
            return PipeDrainState::kFailed;
        }
        if (availableBytes == 0) {
            return PipeDrainState::kOpen;
        }

        const DWORD requestedBytes = std::min<DWORD>(
            std::min<DWORD>(availableBytes, static_cast<DWORD>(buffer.size())), remainingBudget);
        DWORD readBytes = 0;
        if (ReadFile(pipe.get(), buffer.data(), requestedBytes, &readBytes, nullptr) == FALSE) {
            const DWORD nativeError = GetLastError();
            if (nativeError == ERROR_BROKEN_PIPE) {
                pipe.reset();
                return PipeDrainState::kClosed;
            }
            captured.readError = nativeError;
            return PipeDrainState::kFailed;
        }
        if (readBytes == 0) {
            return PipeDrainState::kOpen;
        }

        appendCapturedBytes(captured, buffer.data(), readBytes, maximumBytes);
        remainingBudget -= readBytes;
        --remainingReadOperations;
    }

    return pipe.isValid() ? PipeDrainState::kOpen : PipeDrainState::kClosed;
}

[[nodiscard]] bool drainBothAvailable(ChildPipes& pipes,
                                      CapturedStream& standardOutput,
                                      CapturedStream& standardError,
                                      const ProcessRunOptions& options,
                                      DWORD& nativeError) noexcept {
    const PipeDrainState outputState =
        drainAvailable(pipes.parentStandardOutput, standardOutput, options.maxStandardOutputBytes);
    if (outputState == PipeDrainState::kFailed) {
        nativeError = *standardOutput.readError;
        return false;
    }

    const PipeDrainState errorState =
        drainAvailable(pipes.parentStandardError, standardError, options.maxStandardErrorBytes);
    if (errorState == PipeDrainState::kFailed) {
        nativeError = *standardError.readError;
        return false;
    }
    return true;
}

[[nodiscard]] bool drainUntilClosed(ChildPipes& pipes,
                                    CapturedStream& standardOutput,
                                    CapturedStream& standardError,
                                    const ProcessRunOptions& options,
                                    DWORD& nativeError) noexcept {
    const auto deadline = std::chrono::steady_clock::now() + kOutputDrainWait;
    while (pipes.parentStandardOutput.isValid() || pipes.parentStandardError.isValid()) {
        if (!drainBothAvailable(pipes, standardOutput, standardError, options, nativeError)) {
            return false;
        }
        if (!pipes.parentStandardOutput.isValid() && !pipes.parentStandardError.isValid()) {
            return true;
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            nativeError = WAIT_TIMEOUT;
            return false;
        }
        Sleep(kPollMilliseconds);
    }
    return true;
}

[[nodiscard]] bool sendGracefulCancellation(UniqueHandle& parentStandardInput) noexcept {
    constexpr std::array<char, 2> kCancelInput{'q', '\n'};
    if (!parentStandardInput.isValid()) {
        return false;
    }

    DWORD writtenBytes = 0;
    const BOOL writeSucceeded = WriteFile(parentStandardInput.get(),
                                          kCancelInput.data(),
                                          static_cast<DWORD>(kCancelInput.size()),
                                          &writtenBytes,
                                          nullptr);
    parentStandardInput.reset();
    return writeSucceeded != FALSE && writtenBytes == kCancelInput.size();
}

void transferCapture(ProcessRunResult& result,
                     CapturedStream&& standardOutput,
                     CapturedStream&& standardError) noexcept {
    result.standardOutput = std::move(standardOutput.bytes);
    result.standardError = std::move(standardError.bytes);
    result.standardOutputTruncated = standardOutput.truncated;
    result.standardErrorTruncated = standardError.truncated;
}

void setFailure(ProcessRunResult& result,
                const ProcessRunErrorCode code,
                const DWORD nativeError,
                std::string technicalDetail) {
    result.status = ProcessRunStatus::kFailed;
    result.error = ProcessRunError{
        .code = code,
        .nativeError = static_cast<std::uint32_t>(nativeError),
        .technicalDetail = std::move(technicalDetail),
    };
}

[[nodiscard]] ProcessRunResult runImpl(const std::vector<std::wstring>& arguments,
                                       const ProcessRunOptions& options) {
    if (arguments.empty() || arguments.front().empty()) {
        return makeFailure(ProcessRunErrorCode::kInvalidArguments,
                           ERROR_INVALID_PARAMETER,
                           "An executable and argument vector are required.");
    }
    for (const std::wstring& argument : arguments) {
        if (containsEmbeddedNull(argument)) {
            return makeFailure(ProcessRunErrorCode::kInvalidArguments,
                               ERROR_INVALID_PARAMETER,
                               "Arguments cannot contain embedded null characters.");
        }
    }
    if (options.cancellationToken.isCancellationRequested()) {
        ProcessRunResult result;
        result.status = ProcessRunStatus::kCanceled;
        return result;
    }

    std::wstring commandLine = buildCommandLine(arguments);
    std::vector<wchar_t> mutableCommandLine(commandLine.begin(), commandLine.end());
    mutableCommandLine.push_back(L'\0');

    UniqueHandle job(CreateJobObjectW(nullptr, nullptr));
    if (!job.isValid()) {
        return makeFailure(
            ProcessRunErrorCode::kJobCreationFailed, GetLastError(), "CreateJobObjectW failed.");
    }

    JOBOBJECT_EXTENDED_LIMIT_INFORMATION jobLimits{};
    jobLimits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (SetInformationJobObject(
            job.get(), JobObjectExtendedLimitInformation, &jobLimits, sizeof(jobLimits)) == FALSE) {
        return makeFailure(ProcessRunErrorCode::kJobConfigurationFailed,
                           GetLastError(),
                           "SetInformationJobObject(JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE) failed.");
    }

    ChildPipes pipes;
    DWORD nativeError = ERROR_SUCCESS;
    const PipeSetupState pipeSetup = createChildPipes(pipes, nativeError);
    if (pipeSetup != PipeSetupState::kSucceeded) {
        const ProcessRunErrorCode errorCode = pipeSetup == PipeSetupState::kConfigurationFailed
                                                  ? ProcessRunErrorCode::kPipeConfigurationFailed
                                                  : ProcessRunErrorCode::kPipeCreationFailed;
        return makeFailure(errorCode, nativeError, "Failed to create or configure child pipes.");
    }

    std::array<HANDLE, 3> inheritedHandles{
        pipes.childStandardInput.get(),
        pipes.childStandardOutput.get(),
        pipes.childStandardError.get(),
    };
    ProcThreadAttributeList attributeList;
    if (!attributeList.initialize(inheritedHandles, nativeError)) {
        return makeFailure(ProcessRunErrorCode::kHandleInheritanceConfigurationFailed,
                           nativeError,
                           "Failed to restrict child handle inheritance to stdio endpoints.");
    }

    STARTUPINFOEXW startupInfo{};
    startupInfo.StartupInfo.cb = sizeof(startupInfo);
    startupInfo.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startupInfo.StartupInfo.hStdInput = pipes.childStandardInput.get();
    startupInfo.StartupInfo.hStdOutput = pipes.childStandardOutput.get();
    startupInfo.StartupInfo.hStdError = pipes.childStandardError.get();
    startupInfo.lpAttributeList = attributeList.get();

    PROCESS_INFORMATION processInformation{};
    if (CreateProcessW(arguments.front().c_str(),
                       mutableCommandLine.data(),
                       nullptr,
                       nullptr,
                       TRUE,
                       EXTENDED_STARTUPINFO_PRESENT | CREATE_SUSPENDED | CREATE_NO_WINDOW,
                       nullptr,
                       nullptr,
                       &startupInfo.StartupInfo,
                       &processInformation) == FALSE) {
        return makeFailure(
            ProcessRunErrorCode::kProcessLaunchFailed, GetLastError(), "CreateProcessW failed.");
    }

    UniqueHandle process(processInformation.hProcess);
    UniqueHandle primaryThread(processInformation.hThread);
    pipes.childStandardInput.reset();
    pipes.childStandardOutput.reset();
    pipes.childStandardError.reset();

    if (AssignProcessToJobObject(job.get(), process.get()) == FALSE) {
        const DWORD assignmentError = GetLastError();
        TerminateProcess(process.get(), ERROR_CANCELLED);
        WaitForSingleObject(process.get(), kForcedTerminationWaitMilliseconds);
        return makeFailure(ProcessRunErrorCode::kJobAssignmentFailed,
                           assignmentError,
                           "AssignProcessToJobObject failed before process resume.");
    }

    if (ResumeThread(primaryThread.get()) == static_cast<DWORD>(-1)) {
        const DWORD resumeError = GetLastError();
        TerminateJobObject(job.get(), ERROR_CANCELLED);
        WaitForSingleObject(process.get(), kForcedTerminationWaitMilliseconds);
        return makeFailure(ProcessRunErrorCode::kResumeFailed, resumeError, "ResumeThread failed.");
    }
    primaryThread.reset();

    ProcessRunResult result;
    CapturedStream standardOutput;
    CapturedStream standardError;
    bool cancellationStarted = false;
    std::optional<std::chrono::steady_clock::time_point> gracefulCancellationDeadline;
    std::optional<std::chrono::steady_clock::time_point> forceTerminationDeadline;

    while (true) {
        const auto now = std::chrono::steady_clock::now();
        if (!cancellationStarted && options.cancellationToken.isCancellationRequested()) {
            cancellationStarted = true;
            result.gracefulCancellationSignaled =
                sendGracefulCancellation(pipes.parentStandardInput);
            gracefulCancellationDeadline = now + kGracefulCancellationDelay;
        }

        if (gracefulCancellationDeadline && now >= *gracefulCancellationDeadline &&
            !forceTerminationDeadline) {
            if (TerminateJobObject(job.get(), ERROR_CANCELLED) == FALSE) {
                setFailure(result,
                           ProcessRunErrorCode::kForcedTerminationFailed,
                           GetLastError(),
                           "TerminateJobObject failed after graceful cancellation timeout.");
                break;
            }
            result.forceTerminated = true;
            forceTerminationDeadline = now + kForcedTerminationWait;
            continue;
        }

        if (forceTerminationDeadline && now >= *forceTerminationDeadline) {
            setFailure(result,
                       ProcessRunErrorCode::kForcedTerminationTimedOut,
                       WAIT_TIMEOUT,
                       "The process did not exit within one second of Job Object termination.");
            break;
        }

        if (!drainBothAvailable(pipes, standardOutput, standardError, options, nativeError)) {
            TerminateJobObject(job.get(), ERROR_CANCELLED);
            WaitForSingleObject(process.get(), kForcedTerminationWaitMilliseconds);
            setFailure(result,
                       ProcessRunErrorCode::kOutputCaptureFailed,
                       nativeError,
                       "Failed while draining child output.");
            break;
        }

        const DWORD waitResult = WaitForSingleObject(process.get(), kPollMilliseconds);
        if (waitResult == WAIT_OBJECT_0) {
            DWORD exitCode = 0;
            if (GetExitCodeProcess(process.get(), &exitCode) == FALSE) {
                setFailure(result,
                           ProcessRunErrorCode::kExitCodeReadFailed,
                           GetLastError(),
                           "GetExitCodeProcess failed.");
            } else {
                result.exitCode = static_cast<std::uint32_t>(exitCode);
                result.status =
                    cancellationStarted ? ProcessRunStatus::kCanceled : ProcessRunStatus::kExited;
            }
            break;
        }
        if (waitResult == WAIT_FAILED) {
            TerminateJobObject(job.get(), ERROR_CANCELLED);
            WaitForSingleObject(process.get(), kForcedTerminationWaitMilliseconds);
            setFailure(result,
                       ProcessRunErrorCode::kWaitFailed,
                       GetLastError(),
                       "WaitForSingleObject failed.");
            break;
        }
    }

    pipes.parentStandardInput.reset();
    // Closing the job after the direct child reaches a terminal state kills any remaining child
    // tree members and makes the final pipe drain bounded.
    job.reset();

    DWORD drainError = ERROR_SUCCESS;
    const bool drainSucceeded =
        drainUntilClosed(pipes, standardOutput, standardError, options, drainError);
    const bool outputAllocationFailed =
        standardOutput.allocationFailed || standardError.allocationFailed;
    transferCapture(result, std::move(standardOutput), std::move(standardError));

    if (result.status != ProcessRunStatus::kFailed && !drainSucceeded) {
        setFailure(result,
                   drainError == WAIT_TIMEOUT ? ProcessRunErrorCode::kOutputDrainTimedOut
                                              : ProcessRunErrorCode::kOutputCaptureFailed,
                   drainError,
                   "Timed out or failed while draining final child output.");
    }
    if (result.status != ProcessRunStatus::kFailed && outputAllocationFailed) {
        setFailure(result,
                   ProcessRunErrorCode::kOutputCaptureFailed,
                   ERROR_NOT_ENOUGH_MEMORY,
                   "Output capture allocation failed.");
    }

    return result;
}

} // namespace

ProcessRunResult ProcessRunner::run(const std::vector<std::wstring>& arguments,
                                    const ProcessRunOptions& options) {
    try {
        return runImpl(arguments, options);
    } catch (const std::exception& exception) {
        return makeFailure(
            ProcessRunErrorCode::kInternalFailure, ERROR_NOT_ENOUGH_MEMORY, exception.what());
    }
}

std::wstring ProcessRunner::quoteWindowsArgument(const std::wstring_view argument) {
    if (!requiresWindowsQuoting(argument)) {
        return std::wstring{argument};
    }

    std::wstring quoted;
    quoted.push_back(L'"');
    std::size_t pendingBackslashes = 0;
    for (const wchar_t character : argument) {
        if (character == L'\\') {
            ++pendingBackslashes;
            continue;
        }

        if (character == L'"') {
            quoted.append((pendingBackslashes * 2U) + 1U, L'\\');
            quoted.push_back(L'"');
            pendingBackslashes = 0;
            continue;
        }

        quoted.append(pendingBackslashes, L'\\');
        quoted.push_back(character);
        pendingBackslashes = 0;
    }
    quoted.append(pendingBackslashes * 2U, L'\\');
    quoted.push_back(L'"');
    return quoted;
}

} // namespace dvs::platform
