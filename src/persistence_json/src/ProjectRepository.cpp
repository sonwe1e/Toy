#include "dvs/persistence/ProjectRepository.h"

#include "dvs/persistence/DerivedAlignmentCache.h"
#include "dvs/persistence/FingerprintService.h"
#include "dvs/persistence/ProjectJson.h"
#include "dvs/persistence/ProjectRelinkService.h"
#include "dvs/platform/AtomicFilePublisher.h"
#include "dvs/platform/WindowsPaths.h"

#include "RepositorySupport.h"
#include "SerialIoActor.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace dvs::persistence {
namespace {

constexpr std::uintmax_t kMaximumProjectBytes = 16U * 1024U * 1024U;

[[nodiscard]] domain::Result<std::filesystem::path>
absoluteProjectPath(const std::filesystem::path& projectPath) {
    const auto absolutePath = platform::WindowsPaths::absolutePath(projectPath);
    if (!absolutePath) {
        return domain::Result<std::filesystem::path>::failure(
            internal::platformError(absolutePath.error(), std::nullopt));
    }
    return domain::Result<std::filesystem::path>::success(absolutePath.value());
}

[[nodiscard]] domain::Result<std::string>
readProjectText(const std::filesystem::path& projectPath) {
    std::error_code errorCode;
    const std::uintmax_t byteCount = std::filesystem::file_size(projectPath, errorCode);
    if (errorCode || byteCount > kMaximumProjectBytes ||
        byteCount > static_cast<std::uintmax_t>(std::numeric_limits<std::size_t>::max())) {
        return domain::Result<std::string>::failure(internal::persistenceError(
            domain::MediaErrorCode::kProjectFileIo,
            std::nullopt,
            "Project file is unavailable or exceeds the 16 MiB document limit."));
    }

    std::ifstream stream(projectPath, std::ios::binary);
    if (!stream) {
        return domain::Result<std::string>::failure(
            internal::persistenceError(domain::MediaErrorCode::kProjectFileIo,
                                       std::nullopt,
                                       "Could not open the project file for reading."));
    }

    std::string document(static_cast<std::size_t>(byteCount), '\0');
    stream.read(document.data(), static_cast<std::streamsize>(document.size()));
    if (stream.gcount() != static_cast<std::streamsize>(document.size())) {
        return domain::Result<std::string>::failure(
            internal::persistenceError(domain::MediaErrorCode::kProjectFileIo,
                                       std::nullopt,
                                       "Project file changed or could not be fully read."));
    }
    return domain::Result<std::string>::success(std::move(document));
}

struct LoadedProject final {
    domain::Project project;
    application::SourceRevalidationDiagnostics sourceDiagnostics;
    std::shared_ptr<const std::vector<application::SequenceAlignmentResult>>
        derivedAlignmentResults;
    std::optional<domain::MediaError> alignmentCacheError;
};

[[nodiscard]] bool isRecoverableRevalidationError(const domain::MediaError& error) noexcept {
    return error.code == domain::MediaErrorCode::kSourceMissing ||
           error.code == domain::MediaErrorCode::kSourceFingerprintMismatch;
}

[[nodiscard]] domain::Result<LoadedProject> loadProject(const std::filesystem::path& projectPath) {
    auto absolutePath = absoluteProjectPath(projectPath);
    if (!absolutePath) {
        return domain::Result<LoadedProject>::failure(absolutePath.error());
    }

    auto document = readProjectText(absolutePath.value());
    if (!document) {
        return domain::Result<LoadedProject>::failure(document.error());
    }

    auto project = ProjectJson::decodeText(document.value(), absolutePath.value());
    if (!project) {
        return domain::Result<LoadedProject>::failure(project.error());
    }

    const auto& sources = project.value().sources().sources();
    application::SourceRevalidationDiagnostics sourceDiagnostics;
    sourceDiagnostics.reserve(sources.size());

    for (const auto& source : sources) {
        if (!source.descriptor.sourceIdentity.has_value()) {
            return domain::Result<LoadedProject>::failure(
                internal::persistenceError(domain::MediaErrorCode::kInvalidProjectSchema,
                                           source.id,
                                           "Project sources require persisted file identities."));
        }

        const auto status = FingerprintService::verify(
            source.descriptor.normalizedPath, *source.descriptor.sourceIdentity, source.id);

        std::optional<domain::MediaError> error;
        if (!status) {
            error = status.error();
        }
        sourceDiagnostics.push_back(application::SourceRevalidationDiagnostic{
            .sourceId = source.id,
            .error = std::move(error),
        });
    }

    for (const application::SourceRevalidationDiagnostic& diagnostic : sourceDiagnostics) {
        if (diagnostic.error.has_value() && !isRecoverableRevalidationError(*diagnostic.error)) {
            return domain::Result<LoadedProject>::failure(*diagnostic.error);
        }
    }
    return domain::Result<LoadedProject>::success(LoadedProject{
        .project = std::move(project).value(),
        .sourceDiagnostics = std::move(sourceDiagnostics),
    });
}

[[nodiscard]] std::filesystem::path
defaultAlignmentCacheDirectory(const std::filesystem::path& requested) {
    if (!requested.empty()) {
        return requested;
    }
    const auto paths = platform::WindowsPaths::applicationDataPaths();
    if (!paths) {
        return {};
    }
    return paths.value().proxyCacheDirectory / L"Alignment";
}

[[nodiscard]] bool isOwnerTokenCharacter(const unsigned char character) noexcept {
    return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
           (character >= '0' && character <= '9') || character == '-' || character == '_';
}

[[nodiscard]] std::string hexadecimalHash(const std::uint64_t value) {
    constexpr std::array<char, 16> kHex{
        '0',
        '1',
        '2',
        '3',
        '4',
        '5',
        '6',
        '7',
        '8',
        '9',
        'a',
        'b',
        'c',
        'd',
        'e',
        'f',
    };
    std::string text(16U, '0');
    for (std::size_t index = 0; index < text.size(); ++index) {
        const std::size_t shift = (text.size() - 1U - index) * 4U;
        text[index] = kHex[(value >> shift) & 0x0FU];
    }
    return text;
}

[[nodiscard]] std::string temporaryOwnerId(const domain::ProjectId& projectId) {
    constexpr std::uint64_t kFnvOffset = 14695981039346656037ULL;
    constexpr std::uint64_t kFnvPrime = 1099511628211ULL;
    constexpr std::size_t kOwnerPrefixLimit = 72U;

    std::uint64_t hash = kFnvOffset;
    std::string token;
    token.reserve(kOwnerPrefixLimit + 17U);
    for (const unsigned char character : projectId.value()) {
        hash ^= character;
        hash *= kFnvPrime;
        if (token.size() < kOwnerPrefixLimit) {
            token.push_back(isOwnerTokenCharacter(character) ? static_cast<char>(character) : '_');
        }
    }
    if (token.empty()) {
        token = "project";
    }
    token.push_back('-');
    token += hexadecimalHash(hash);
    return token;
}

[[nodiscard]] domain::Status writeProject(const application::ProjectSaveRequest& request,
                                          internal::OperationState& operation) {
    auto absolutePath = absoluteProjectPath(request.projectPath);
    if (!absolutePath) {
        return domain::Status::failure(absolutePath.error());
    }

    auto text = ProjectJson::encodeText(request.project, absolutePath.value());
    if (!text) {
        return domain::Status::failure(text.error());
    }

    std::error_code errorCode;
    const bool destinationExists = std::filesystem::exists(absolutePath.value(), errorCode);
    if (errorCode) {
        return domain::Status::failure(internal::persistenceError(
            domain::MediaErrorCode::kProjectFileIo,
            std::nullopt,
            "Could not inspect the project destination before publication."));
    }

    auto publisher = platform::AtomicFilePublisher::begin(
        absolutePath.value(),
        platform::TemporaryFileIdentity{
            .operation = "project-save",
            .ownerId = temporaryOwnerId(request.project.id()),
            .revision = request.context.projectRevision.value(),
        });
    if (!publisher) {
        return domain::Status::failure(internal::platformError(publisher.error(), std::nullopt));
    }

    const std::span<const char> characters{text.value().data(), text.value().size()};
    const auto bytes = std::as_bytes(characters);
    auto status = publisher.value()->write(bytes);
    if (!status) {
        return domain::Status::failure(internal::platformError(status.error(), std::nullopt));
    }
    status = publisher.value()->flush();
    if (!status) {
        return domain::Status::failure(internal::platformError(status.error(), std::nullopt));
    }

    // The successful compare/exchange is the save's linearization point. A cancellation that
    // arrived earlier leaves the flushed temporary unpublished; one that arrives later is too
    // late to change the terminal result after a durable replacement.
    if (!operation.tryBeginCommit()) {
        return domain::Status::failure(
            internal::persistenceError(domain::MediaErrorCode::kProjectFileIo,
                                       std::nullopt,
                                       "Project save was canceled before atomic publication."));
    }
    status = destinationExists ? publisher.value()->publishReplacingExisting()
                               : publisher.value()->publishNew();
    if (!status) {
        return domain::Status::failure(internal::platformError(status.error(), std::nullopt));
    }
    return domain::Status::success();
}

[[nodiscard]] application::PortSubmitResult
mapSubmitResult(const internal::IoSubmitResult result) noexcept {
    switch (result) {
    case internal::IoSubmitResult::kAccepted:
        return application::PortSubmitResult::Accepted;
    case internal::IoSubmitResult::kBusy:
        return application::PortSubmitResult::Busy;
    case internal::IoSubmitResult::kClosed:
        return application::PortSubmitResult::Closed;
    }
    return application::PortSubmitResult::Closed;
}

} // namespace

class ProjectRepository::Impl final {
public:
    Impl(const std::size_t queueCapacity, std::filesystem::path alignmentCacheDirectory)
        : alignmentCache(defaultAlignmentCacheDirectory(std::move(alignmentCacheDirectory))),
          actor(queueCapacity) {}

    ~Impl() {
        operations.cancelAll();
        actor.close();
    }

    [[nodiscard]] application::PortSubmitResult
    submit(const application::ProjectLoadRequest& request,
           std::shared_ptr<application::IApplicationEventSink> events) {
        if (!events) {
            return application::PortSubmitResult::Closed;
        }
        const std::weak_ptr<application::IApplicationEventSink> weakEvents{events};
        return submitTask(request.context, [this, request, weakEvents](const auto& operation) {
            executeLoad(request, weakEvents, operation);
        });
    }

    [[nodiscard]] application::PortSubmitResult
    submit(const application::ProjectRelinkRequest& request,
           std::shared_ptr<application::IApplicationEventSink> events) {
        if (!events) {
            return application::PortSubmitResult::Closed;
        }
        const std::weak_ptr<application::IApplicationEventSink> weakEvents{events};
        return submitTask(request.context, [this, request, weakEvents](const auto& operation) {
            executeRelink(request, weakEvents, operation);
        });
    }

    [[nodiscard]] application::PortSubmitResult
    submit(const application::ProjectSaveRequest& request,
           std::shared_ptr<application::IApplicationEventSink> events) {
        if (!events) {
            return application::PortSubmitResult::Closed;
        }
        const std::weak_ptr<application::IApplicationEventSink> weakEvents{events};
        return submitTask(request.context.request,
                          [this, request, weakEvents](const auto& operation) {
                              executeSave(request, weakEvents, operation);
                          });
    }

    void cancel(const application::RequestContext& context) noexcept {
        operations.cancel(context);
    }

private:
    template <typename TTask>
    [[nodiscard]] application::PortSubmitResult
    submitTask(const application::RequestContext& context, TTask&& task) {
        std::shared_ptr<internal::OperationState> operation;
        try {
            operation = operations.add(context);
            const auto submitted = actor.submit(
                [operation, task = std::forward<TTask>(task)]() mutable { task(operation); });
            if (submitted != internal::IoSubmitResult::kAccepted) {
                operations.remove(operation);
            }
            return mapSubmitResult(submitted);
        } catch (...) {
            if (operation) {
                operations.remove(operation);
            }
            return application::PortSubmitResult::Busy;
        }
    }

    void executeLoad(const application::ProjectLoadRequest& request,
                     const std::weak_ptr<application::IApplicationEventSink>& events,
                     const std::shared_ptr<internal::OperationState>& operation) noexcept {
        try {
            if (operation->isCanceled()) {
                internal::completeCanceled(operation, events, request.context);
            } else {
                auto loadedProject = loadProject(request.projectPath);
                if (!loadedProject) {
                    internal::completeFailed(
                        operation, events, request.context, loadedProject.error());
                } else if (operation->isCanceled()) {
                    internal::completeCanceled(operation, events, request.context);
                } else {
                    LoadedProject payload = std::move(loadedProject).value();
                    for (application::SourceRevalidationDiagnostic& diagnostic :
                         payload.sourceDiagnostics) {
                        if (diagnostic.error.has_value()) {
                            diagnostic.error = internal::withRequestId(std::move(*diagnostic.error),
                                                                       request.context);
                        }
                    }
                    const bool sourceRevalidationFailed = std::any_of(
                        payload.sourceDiagnostics.begin(),
                        payload.sourceDiagnostics.end(),
                        [](const application::SourceRevalidationDiagnostic& diagnostic) {
                            return diagnostic.error.has_value();
                        });
                    const domain::ProjectAlignmentState& alignment =
                        payload.project.alignmentState();
                    if (alignment.mode == domain::ProjectAlignmentMode::kAutomaticSequence) {
                        if (sourceRevalidationFailed || !alignment.analysisCacheKey.has_value()) {
                            payload.alignmentCacheError = internal::persistenceError(
                                domain::MediaErrorCode::kInvalidProjectSchema,
                                std::nullopt,
                                "Derived alignment cache cannot be reused until all sources "
                                "pass fingerprint revalidation.");
                        } else {
                            auto cached = alignmentCache.load(*alignment.analysisCacheKey,
                                                              payload.project.sources());
                            if (cached) {
                                payload.derivedAlignmentResults = std::make_shared<
                                    const std::vector<application::SequenceAlignmentResult>>(
                                    std::move(cached).value());
                            } else {
                                payload.alignmentCacheError = cached.error();
                            }
                        }
                    }
                    application::ApplicationEvent loaded{application::ProjectLoaded{
                        .context = request.context,
                        .project = std::move(payload.project),
                        .sourceDiagnostics = std::move(payload.sourceDiagnostics),
                        .derivedAlignmentResults = std::move(payload.derivedAlignmentResults),
                        .alignmentCacheError = std::move(payload.alignmentCacheError),
                    }};
                    if (operation->isCanceled()) {
                        internal::completeCanceled(operation, events, request.context);
                    } else if (operation->claimTerminal()) {
                        internal::postCritical(events, std::move(loaded));
                        internal::postSucceeded(events, request.context);
                    }
                }
            }
        } catch (const std::exception& exception) {
            internal::completeFailed(
                operation,
                events,
                request.context,
                internal::persistenceError(domain::MediaErrorCode::kProjectFileIo,
                                           std::nullopt,
                                           "Unexpected project-load exception: " +
                                               std::string{exception.what()}));
        } catch (...) {
            internal::completeFailed(
                operation,
                events,
                request.context,
                internal::persistenceError(domain::MediaErrorCode::kProjectFileIo,
                                           std::nullopt,
                                           "Unexpected project-load failure."));
        }
        operations.remove(operation);
    }

    void executeRelink(const application::ProjectRelinkRequest& request,
                       const std::weak_ptr<application::IApplicationEventSink>& events,
                       const std::shared_ptr<internal::OperationState>& operation) noexcept {
        try {
            if (operation->isCanceled()) {
                internal::completeCanceled(operation, events, request.context);
            } else {
                auto candidate =
                    ProjectRelinkService::prepare(request.sourceId, request.newSourcePath);
                if (!candidate) {
                    internal::completeFailed(operation, events, request.context, candidate.error());
                } else if (operation->isCanceled()) {
                    internal::completeCanceled(operation, events, request.context);
                } else {
                    application::ApplicationEvent prepared{application::SourceRelinkPrepared{
                        .context = request.context,
                        .candidate = std::move(candidate).value(),
                    }};
                    if (operation->isCanceled()) {
                        internal::completeCanceled(operation, events, request.context);
                    } else if (operation->claimTerminal()) {
                        internal::postCritical(events, std::move(prepared));
                        internal::postSucceeded(events, request.context);
                    }
                }
            }
        } catch (const std::exception& exception) {
            internal::completeFailed(
                operation,
                events,
                request.context,
                internal::persistenceError(domain::MediaErrorCode::kProjectFileIo,
                                           request.sourceId,
                                           "Unexpected project-relink exception: " +
                                               std::string{exception.what()}));
        } catch (...) {
            internal::completeFailed(
                operation,
                events,
                request.context,
                internal::persistenceError(domain::MediaErrorCode::kProjectFileIo,
                                           request.sourceId,
                                           "Unexpected project-relink failure."));
        }
        operations.remove(operation);
    }

    void executeSave(const application::ProjectSaveRequest& request,
                     const std::weak_ptr<application::IApplicationEventSink>& events,
                     const std::shared_ptr<internal::OperationState>& operation) noexcept {
        try {
            if (operation->isCanceled()) {
                internal::completeCanceled(operation, events, request.context);
            } else {
                domain::Status status = domain::Status::success();
                const domain::ProjectAlignmentState& alignment = request.project.alignmentState();
                if (alignment.mode == domain::ProjectAlignmentMode::kAutomaticSequence) {
                    if (!alignment.analysisCacheKey.has_value() ||
                        !request.derivedAlignmentResults) {
                        status = domain::Status::failure(internal::persistenceError(
                            domain::MediaErrorCode::kInvalidProjectSchema,
                            std::nullopt,
                            "Automatic alignment save requires a derived cache payload."));
                    } else {
                        status = alignmentCache.store(*alignment.analysisCacheKey,
                                                      request.project.sources(),
                                                      *request.derivedAlignmentResults,
                                                      request.context.projectRevision.value());
                    }
                }
                if (status && operation->isCanceled()) {
                    internal::completeCanceled(operation, events, request.context);
                    operations.remove(operation);
                    return;
                }
                if (status) {
                    status = writeProject(request, *operation);
                }
                if (!status) {
                    internal::completeFailed(operation, events, request.context, status.error());
                } else {
                    application::ApplicationEvent saved{application::ProjectSaved{
                        .context = request.context,
                    }};
                    if (operation->claimTerminal()) {
                        internal::postCritical(events, std::move(saved));
                        internal::postSucceeded(events, request.context);
                    }
                }
            }
        } catch (const std::exception& exception) {
            internal::completeFailed(
                operation,
                events,
                request.context,
                internal::persistenceError(domain::MediaErrorCode::kProjectFileIo,
                                           std::nullopt,
                                           "Unexpected project-save exception: " +
                                               std::string{exception.what()}));
        } catch (...) {
            internal::completeFailed(
                operation,
                events,
                request.context,
                internal::persistenceError(domain::MediaErrorCode::kProjectFileIo,
                                           std::nullopt,
                                           "Unexpected project-save failure."));
        }
        operations.remove(operation);
    }

    DerivedAlignmentCache alignmentCache;
    internal::SerialIoActor actor;
    internal::OperationRegistry operations;
};

ProjectRepository::ProjectRepository(const std::size_t queueCapacity,
                                     std::filesystem::path alignmentCacheDirectory)
    : impl_(std::make_unique<Impl>(queueCapacity, std::move(alignmentCacheDirectory))) {}

ProjectRepository::~ProjectRepository() = default;

application::PortSubmitResult
ProjectRepository::submit(const application::ProjectLoadRequest& request,
                          std::shared_ptr<application::IApplicationEventSink> events) {
    return impl_->submit(request, std::move(events));
}

application::PortSubmitResult
ProjectRepository::submit(const application::ProjectRelinkRequest& request,
                          std::shared_ptr<application::IApplicationEventSink> events) {
    return impl_->submit(request, std::move(events));
}

application::PortSubmitResult
ProjectRepository::submit(const application::ProjectSaveRequest& request,
                          std::shared_ptr<application::IApplicationEventSink> events) {
    return impl_->submit(request, std::move(events));
}

void ProjectRepository::cancel(const application::RequestContext& context) noexcept {
    impl_->cancel(context);
}

} // namespace dvs::persistence
