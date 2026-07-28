#include "dvs/persistence/FingerprintService.h"
#include "dvs/persistence/ProjectRepository.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <memory>
#include <mutex>
#include <string>
#include <system_error>
#include <utility>
#include <variant>
#include <vector>

namespace dvs::persistence {
namespace {

std::atomic<std::uint64_t> nextDirectoryNumber{0};

[[nodiscard]] application::RequestContext requestContext(const std::uint64_t requestId) {
    return application::RequestContext{
        .sessionId = domain::SessionId{1U},
        .sessionEpoch = domain::SessionEpoch{2U},
        .requestId = domain::RequestId{requestId},
    };
}

template <typename TContext>
[[nodiscard]] bool isTerminalFor(const application::ApplicationEvent& event,
                                 const TContext& expected) {
    const auto* terminal = std::get_if<application::RequestTerminal>(&event);
    if (terminal == nullptr) {
        return false;
    }
    return std::visit(
        [&expected](const auto& outcome) {
            const auto* context = std::get_if<TContext>(&outcome.context);
            return context != nullptr && *context == expected;
        },
        *terminal);
}

[[nodiscard]] bool isSucceededTerminal(const application::ApplicationEvent& event) {
    const auto* terminal = std::get_if<application::RequestTerminal>(&event);
    return terminal != nullptr && std::get_if<application::RequestSucceeded>(terminal) != nullptr;
}

class RecordingEventSink final : public application::IApplicationEventSink {
public:
    [[nodiscard]] application::EventPostResult
    postCritical(application::ApplicationEvent event) noexcept override {
        {
            std::scoped_lock lock(mutex_);
            if (criticalIngressClosed_) {
                return application::EventPostResult::Closed;
            }
            try {
                criticalEvents_.push_back(std::move(event));
            } catch (...) {
                return application::EventPostResult::Closed;
            }
        }
        condition_.notify_all();
        return application::EventPostResult::Accepted;
    }

    [[nodiscard]] application::EventPostResult
    postRealtime(application::ApplicationEvent event) noexcept override {
        static_cast<void>(event);
        return application::EventPostResult::Accepted;
    }

    void closeRealtimeIngress() noexcept override {}

    void closeCriticalIngress() noexcept override {
        std::scoped_lock lock(mutex_);
        criticalIngressClosed_ = true;
    }

    template <typename TContext>
    [[nodiscard]] bool waitForTerminal(const TContext& context,
                                       const std::chrono::milliseconds timeout) {
        std::unique_lock lock(mutex_);
        return condition_.wait_for(lock, timeout, [this, &context] {
            for (const application::ApplicationEvent& event : criticalEvents_) {
                if (isTerminalFor(event, context)) {
                    return true;
                }
            }
            return false;
        });
    }

    [[nodiscard]] std::vector<application::ApplicationEvent> criticalEvents() const {
        std::scoped_lock lock(mutex_);
        return criticalEvents_;
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable condition_;
    std::vector<application::ApplicationEvent> criticalEvents_;
    bool criticalIngressClosed_ = false;
};

class BlockingEventSink final : public application::IApplicationEventSink {
public:
    [[nodiscard]] application::EventPostResult
    postCritical(application::ApplicationEvent event) noexcept override {
        static_cast<void>(event);
        std::unique_lock lock(mutex_);
        postEntered_ = true;
        condition_.notify_all();
        condition_.wait(lock, [this] { return released_; });
        return application::EventPostResult::Accepted;
    }

    [[nodiscard]] application::EventPostResult
    postRealtime(application::ApplicationEvent event) noexcept override {
        static_cast<void>(event);
        return application::EventPostResult::Accepted;
    }

    void closeRealtimeIngress() noexcept override {}
    void closeCriticalIngress() noexcept override {}

    [[nodiscard]] bool waitForPost(const std::chrono::milliseconds timeout) {
        std::unique_lock lock(mutex_);
        return condition_.wait_for(lock, timeout, [this] { return postEntered_; });
    }

    void release() noexcept {
        {
            std::scoped_lock lock(mutex_);
            released_ = true;
        }
        condition_.notify_all();
    }

private:
    std::mutex mutex_;
    std::condition_variable condition_;
    bool postEntered_ = false;
    bool released_ = false;
};

class ExpiringEventSink final : public application::IApplicationEventSink {
public:
    ExpiringEventSink(std::atomic<std::size_t>& postCalls,
                      std::atomic<std::size_t>& destructions) noexcept
        : postCalls_(postCalls), destructions_(destructions) {}

    ~ExpiringEventSink() override {
        destructions_.fetch_add(1U, std::memory_order_relaxed);
    }

    [[nodiscard]] application::EventPostResult
    postCritical(application::ApplicationEvent event) noexcept override {
        static_cast<void>(event);
        postCalls_.fetch_add(1U, std::memory_order_relaxed);
        return application::EventPostResult::Accepted;
    }

    [[nodiscard]] application::EventPostResult
    postRealtime(application::ApplicationEvent event) noexcept override {
        static_cast<void>(event);
        return application::EventPostResult::Accepted;
    }

    void closeRealtimeIngress() noexcept override {}
    void closeCriticalIngress() noexcept override {}

private:
    std::atomic<std::size_t>& postCalls_;
    std::atomic<std::size_t>& destructions_;
};

class ProjectRepositoryTests : public ::testing::Test {
protected:
    void SetUp() override {
        const auto timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
        root_ = std::filesystem::temp_directory_path() /
                ("dvs-project-repository-" + std::to_string(timestamp) + "-" +
                 std::to_string(nextDirectoryNumber.fetch_add(1U)));
        std::error_code errorCode;
        std::filesystem::create_directories(root_, errorCode);
        ASSERT_FALSE(errorCode);
    }

    void TearDown() override {
        std::error_code errorCode;
        std::filesystem::remove_all(root_, errorCode);
    }

    [[nodiscard]] std::filesystem::path path(const std::string& name) const {
        return root_ / name;
    }

    void writeFile(const std::filesystem::path& path, const std::string& contents) const {
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(stream);
        stream << contents;
        ASSERT_TRUE(stream.good());
    }

private:
    std::filesystem::path root_;
};

[[nodiscard]] domain::Result<domain::Project>
makeProject(const std::filesystem::path& sourceAPath,
            const domain::SourceFileIdentity& identityA,
            const std::filesystem::path& sourceBPath,
            const domain::SourceFileIdentity& identityB) {
    const auto rate = domain::RationalRate::create(30, 1);
    if (!rate) {
        return domain::Result<domain::Project>::failure(rate.error());
    }
    std::vector<domain::ComparisonSource> sources;
    sources.push_back(domain::ComparisonSource{
        .id = 0,
        .role = domain::ComparisonRole::kReference,
        .descriptor = domain::MediaDescriptor{
            .normalizedPath = sourceAPath,
            .extent = domain::MediaExtent{.width = 1'920U, .height = 1'080U},
            .frameRate = rate.value(),
            .frameCount =
                domain::FrameCountInfo{
                    .value = 5,
                    .origin = domain::FrameCountOrigin::kReported,
                },
            .duration = domain::MediaTime{166'666},
            .codecId = "h264",
            .pixelFormatId = "yuv420p",
            .bitDepth = 8U,
            .decodeCapabilities =
                domain::DecodeCapabilities{
                    .softwareDecode = true,
                    .d3d11VaDecode = false,
                },
            .timingConfidence = domain::TimingConfidence::kDeclaredCfr,
            .sourceIdentity = identityA,
        },
        .displayName = "Source A",
    });
    sources.push_back(domain::ComparisonSource{
        .id = 1,
        .role = domain::ComparisonRole::kPrediction,
        .descriptor = domain::MediaDescriptor{
            .normalizedPath = sourceBPath,
            .extent = domain::MediaExtent{.width = 1'280U, .height = 720U},
            .frameRate = rate.value(),
            .frameCount =
                domain::FrameCountInfo{
                    .value = 5,
                    .origin = domain::FrameCountOrigin::kReported,
                },
            .duration = domain::MediaTime{166'666},
            .codecId = "h264",
            .pixelFormatId = "yuv420p",
            .bitDepth = 8U,
            .decodeCapabilities =
                domain::DecodeCapabilities{
                    .softwareDecode = true,
                    .d3d11VaDecode = false,
                },
            .timingConfidence = domain::TimingConfidence::kDeclaredCfr,
            .sourceIdentity = identityB,
        },
        .displayName = "Source B",
    });
    const auto validated = domain::ComparisonValidator::validate(std::move(sources));
    if (!validated) {
        return domain::Result<domain::Project>::failure(validated.error());
    }
    return domain::Project::create(
        domain::ProjectId{"project-1"}, "Round-trip project", validated.value().set);
}

TEST_F(ProjectRepositoryTests, SavesThenLoadsWithPayloadBeforeTerminal) {
    const std::filesystem::path sourceAPath = path("a.mp4");
    const std::filesystem::path sourceBPath = path("b.mp4");
    const std::filesystem::path projectPath = path("project.dvs.json");
    writeFile(sourceAPath, "source-a");
    writeFile(sourceBPath, "source-b");

    const auto identityA = FingerprintService::fingerprint(sourceAPath, 0);
    const auto identityB = FingerprintService::fingerprint(sourceBPath, 1);
    ASSERT_TRUE(identityA);
    ASSERT_TRUE(identityB);
    const auto rate = domain::RationalRate::create(30, 1);
    ASSERT_TRUE(rate);

    std::vector<domain::ComparisonSource> sources;
    sources.push_back(domain::ComparisonSource{
        .id = 0,
        .role = domain::ComparisonRole::kReference,
        .descriptor = domain::MediaDescriptor{
            .normalizedPath = sourceAPath,
            .extent = domain::MediaExtent{.width = 1'920U, .height = 1'080U},
            .frameRate = rate.value(),
            .frameCount =
                domain::FrameCountInfo{
                    .value = 5,
                    .origin = domain::FrameCountOrigin::kReported,
                },
            .duration = domain::MediaTime{166'666},
            .codecId = "h264",
            .pixelFormatId = "yuv420p",
            .bitDepth = 8U,
            .decodeCapabilities =
                domain::DecodeCapabilities{
                    .softwareDecode = true,
                    .d3d11VaDecode = false,
                },
            .timingConfidence = domain::TimingConfidence::kDeclaredCfr,
            .sourceIdentity = identityA.value(),
        },
        .displayName = "Source A",
    });
    sources.push_back(domain::ComparisonSource{
        .id = 1,
        .role = domain::ComparisonRole::kPrediction,
        .descriptor = domain::MediaDescriptor{
            .normalizedPath = sourceBPath,
            .extent = domain::MediaExtent{.width = 1'280U, .height = 720U},
            .frameRate = rate.value(),
            .frameCount =
                domain::FrameCountInfo{
                    .value = 5,
                    .origin = domain::FrameCountOrigin::kReported,
                },
            .duration = domain::MediaTime{166'666},
            .codecId = "h264",
            .pixelFormatId = "yuv420p",
            .bitDepth = 8U,
            .decodeCapabilities =
                domain::DecodeCapabilities{
                    .softwareDecode = true,
                    .d3d11VaDecode = false,
                },
            .timingConfidence = domain::TimingConfidence::kDeclaredCfr,
            .sourceIdentity = identityB.value(),
        },
        .displayName = "Source B",
    });
    const auto validated = domain::ComparisonValidator::validate(std::move(sources));
    ASSERT_TRUE(validated);
    const auto project =
        domain::Project::create(domain::ProjectId{"project-1"}, "Round-trip project", validated.value().set);
    ASSERT_TRUE(project);

    const auto saveEvents = std::make_shared<RecordingEventSink>();
    const auto loadEvents = std::make_shared<RecordingEventSink>();
    ProjectRepository repository;
    const application::ProjectSaveRequest saveRequest{
        .context =
            application::SaveRequestContext{
                .request = requestContext(3U),
                .projectRevision = domain::ProjectRevision{7U},
            },
        .projectPath = projectPath,
        .project = project.value(),
    };
    EXPECT_EQ(repository.submit(saveRequest, saveEvents), application::PortSubmitResult::Accepted);
    ASSERT_TRUE(saveEvents->waitForTerminal(saveRequest.context, std::chrono::seconds{2}));
    const auto saveCriticalEvents = saveEvents->criticalEvents();
    ASSERT_EQ(saveCriticalEvents.size(), 2U);
    const auto* saved = std::get_if<application::ProjectSaved>(&saveCriticalEvents[0]);
    ASSERT_NE(saved, nullptr);
    EXPECT_EQ(saved->context, saveRequest.context);
    const auto* saveTerminal = std::get_if<application::RequestTerminal>(&saveCriticalEvents[1]);
    ASSERT_NE(saveTerminal, nullptr);
    const auto* saveSucceeded = std::get_if<application::RequestSucceeded>(saveTerminal);
    ASSERT_NE(saveSucceeded, nullptr);
    const auto* saveContext = std::get_if<application::SaveRequestContext>(&saveSucceeded->context);
    ASSERT_NE(saveContext, nullptr);
    EXPECT_EQ(*saveContext, saveRequest.context);

    const application::ProjectLoadRequest loadRequest{
        .context = requestContext(4U),
        .projectPath = projectPath,
    };
    EXPECT_EQ(repository.submit(loadRequest, loadEvents), application::PortSubmitResult::Accepted);
    ASSERT_TRUE(loadEvents->waitForTerminal(loadRequest.context, std::chrono::seconds{2}));
    const auto loadCriticalEvents = loadEvents->criticalEvents();
    ASSERT_EQ(loadCriticalEvents.size(), 2U);
    const auto* loaded = std::get_if<application::ProjectLoaded>(&loadCriticalEvents[0]);
    ASSERT_NE(loaded, nullptr);
    EXPECT_EQ(loaded->context, loadRequest.context);
    EXPECT_EQ(loaded->project.id(), project.value().id());
    const auto& loadedSources = loaded->project.sources().sources();
    EXPECT_EQ(loadedSources[0].descriptor.normalizedPath, sourceAPath);
    EXPECT_EQ(loadedSources[1].descriptor.normalizedPath, sourceBPath);
    ASSERT_EQ(loaded->sourceDiagnostics.size(), 2U);
    EXPECT_EQ(loaded->sourceDiagnostics[0].sourceId, 0U);
    EXPECT_FALSE(loaded->sourceDiagnostics[0].error.has_value());
    EXPECT_EQ(loaded->sourceDiagnostics[1].sourceId, 1U);
    EXPECT_FALSE(loaded->sourceDiagnostics[1].error.has_value());
    const auto* loadTerminal = std::get_if<application::RequestTerminal>(&loadCriticalEvents[1]);
    ASSERT_NE(loadTerminal, nullptr);
    const auto* loadSucceeded = std::get_if<application::RequestSucceeded>(loadTerminal);
    ASSERT_NE(loadSucceeded, nullptr);
    const auto* loadContext = std::get_if<application::RequestContext>(&loadSucceeded->context);
    ASSERT_NE(loadContext, nullptr);
    EXPECT_EQ(*loadContext, loadRequest.context);
}

TEST_F(ProjectRepositoryTests, StructuralLoadFailureIncludesItsAsynchronousRequestId) {
    const std::filesystem::path projectPath = path("invalid.dvsproj");
    writeFile(projectPath, "{ not-valid-json");

    const auto events = std::make_shared<RecordingEventSink>();
    ProjectRepository repository;
    const application::ProjectLoadRequest request{
        .context = requestContext(20U),
        .projectPath = projectPath,
    };

    EXPECT_EQ(repository.submit(request, events), application::PortSubmitResult::Accepted);
    ASSERT_TRUE(events->waitForTerminal(request.context, std::chrono::seconds{2}));
    const auto criticalEvents = events->criticalEvents();
    ASSERT_EQ(criticalEvents.size(), 1U);
    const auto* terminal = std::get_if<application::RequestTerminal>(&criticalEvents.front());
    ASSERT_NE(terminal, nullptr);
    const auto* failed = std::get_if<application::RequestFailed>(terminal);
    ASSERT_NE(failed, nullptr);
    const auto* context = std::get_if<application::RequestContext>(&failed->context);
    ASSERT_NE(context, nullptr);
    EXPECT_EQ(*context, request.context);
    EXPECT_EQ(failed->error.code, domain::MediaErrorCode::kInvalidProjectSchema);
    ASSERT_TRUE(failed->error.requestId.has_value());
    EXPECT_EQ(*failed->error.requestId, request.context.requestId);
}

TEST_F(ProjectRepositoryTests, AsyncRelinkFailureIsRecoverableAndRequestScoped) {
    ProjectRepository repository;
    const auto events = std::make_shared<RecordingEventSink>();
    const application::ProjectRelinkRequest request{
        .context = requestContext(25U),
        .sourceId = 0,
        .newSourcePath = path("missing-replacement.mp4"),
    };
    ASSERT_EQ(repository.submit(request, events), application::PortSubmitResult::Accepted);
    ASSERT_TRUE(events->waitForTerminal(request.context, std::chrono::seconds{2}));
    const auto criticalEvents = events->criticalEvents();
    ASSERT_EQ(criticalEvents.size(), 1U);
    const auto* terminal = std::get_if<application::RequestTerminal>(&criticalEvents.front());
    ASSERT_NE(terminal, nullptr);
    const auto* failed = std::get_if<application::RequestFailed>(terminal);
    ASSERT_NE(failed, nullptr);
    EXPECT_EQ(failed->error.code, domain::MediaErrorCode::kSourceMissing);
    EXPECT_TRUE(failed->error.recoverable);
    EXPECT_EQ(failed->error.requestId, request.context.requestId);
}

TEST_F(ProjectRepositoryTests, LoadsEditableDiagnosticsThenPreparesRelinkCandidates) {
    const std::filesystem::path sourceAPath = path("a.mp4");
    const std::filesystem::path sourceBPath = path("b.mp4");
    const std::filesystem::path projectPath = path("project.dvsproj");
    writeFile(sourceAPath, "original source A");
    writeFile(sourceBPath, "original source B");

    const auto identityA = FingerprintService::fingerprint(sourceAPath, 0);
    const auto identityB = FingerprintService::fingerprint(sourceBPath, 1);
    ASSERT_TRUE(identityA);
    ASSERT_TRUE(identityB);
    const auto initialProject =
        makeProject(sourceAPath, identityA.value(), sourceBPath, identityB.value());
    ASSERT_TRUE(initialProject);

    ProjectRepository repository;
    const auto firstSaveEvents = std::make_shared<RecordingEventSink>();
    const application::ProjectSaveRequest firstSave{
        .context =
            application::SaveRequestContext{
                .request = requestContext(30U),
                .projectRevision = domain::ProjectRevision{1U},
            },
        .projectPath = projectPath,
        .project = initialProject.value(),
    };
    ASSERT_EQ(repository.submit(firstSave, firstSaveEvents),
              application::PortSubmitResult::Accepted);
    ASSERT_TRUE(firstSaveEvents->waitForTerminal(firstSave.context, std::chrono::seconds{2}));

    writeFile(sourceAPath, "modified source A with a different identity");
    std::error_code removeError;
    EXPECT_TRUE(std::filesystem::remove(sourceBPath, removeError));
    ASSERT_FALSE(removeError);

    const auto diagnosticLoadEvents = std::make_shared<RecordingEventSink>();
    const application::ProjectLoadRequest diagnosticLoad{
        .context = requestContext(31U),
        .projectPath = projectPath,
    };
    ASSERT_EQ(repository.submit(diagnosticLoad, diagnosticLoadEvents),
              application::PortSubmitResult::Accepted);
    ASSERT_TRUE(
        diagnosticLoadEvents->waitForTerminal(diagnosticLoad.context, std::chrono::seconds{2}));
    const auto diagnosticLoadCriticalEvents = diagnosticLoadEvents->criticalEvents();
    ASSERT_EQ(diagnosticLoadCriticalEvents.size(), 2U);
    const auto* loaded = std::get_if<application::ProjectLoaded>(&diagnosticLoadCriticalEvents[0]);
    ASSERT_NE(loaded, nullptr);
    EXPECT_EQ(loaded->project.id(), initialProject.value().id());
    ASSERT_EQ(loaded->sourceDiagnostics.size(), 2U);
    const auto& sourceADiagnostic = loaded->sourceDiagnostics[0];
    EXPECT_EQ(sourceADiagnostic.sourceId, 0U);
    ASSERT_TRUE(sourceADiagnostic.error.has_value());
    EXPECT_EQ(sourceADiagnostic.error->code, domain::MediaErrorCode::kSourceFingerprintMismatch);
    EXPECT_TRUE(sourceADiagnostic.error->recoverable);
    EXPECT_EQ(sourceADiagnostic.error->requestId, diagnosticLoad.context.requestId);
    const auto& sourceBDiagnostic = loaded->sourceDiagnostics[1];
    EXPECT_EQ(sourceBDiagnostic.sourceId, 1U);
    ASSERT_TRUE(sourceBDiagnostic.error.has_value());
    EXPECT_EQ(sourceBDiagnostic.error->code, domain::MediaErrorCode::kSourceMissing);
    EXPECT_TRUE(sourceBDiagnostic.error->recoverable);
    EXPECT_EQ(sourceBDiagnostic.error->requestId, diagnosticLoad.context.requestId);
    const auto* diagnosticLoadTerminal =
        std::get_if<application::RequestTerminal>(&diagnosticLoadCriticalEvents[1]);
    ASSERT_NE(diagnosticLoadTerminal, nullptr);
    EXPECT_NE(std::get_if<application::RequestSucceeded>(diagnosticLoadTerminal), nullptr);

    const auto relinkAEvents = std::make_shared<RecordingEventSink>();
    const application::ProjectRelinkRequest relinkA{
        .context = requestContext(32U),
        .sourceId = 0,
        .newSourcePath = sourceAPath,
    };
    ASSERT_EQ(repository.submit(relinkA, relinkAEvents), application::PortSubmitResult::Accepted);
    ASSERT_TRUE(relinkAEvents->waitForTerminal(relinkA.context, std::chrono::seconds{2}));
    const auto relinkACriticalEvents = relinkAEvents->criticalEvents();
    ASSERT_EQ(relinkACriticalEvents.size(), 2U);
    const auto* preparedA =
        std::get_if<application::SourceRelinkPrepared>(&relinkACriticalEvents[0]);
    ASSERT_NE(preparedA, nullptr);
    EXPECT_EQ(preparedA->context, relinkA.context);
    EXPECT_EQ(preparedA->candidate.sourceId(), 0U);
    EXPECT_TRUE(preparedA->candidate.normalizedPath().is_absolute());
    EXPECT_TRUE(preparedA->candidate.sourceIdentity().isComplete());
    EXPECT_TRUE(isSucceededTerminal(relinkACriticalEvents[1]));
    // Candidate preparation cannot mutate the editable project or claim the changed file is
    // compatible media. That only happens after a fresh probe produces a comparison set.
    const auto& loadedSources = loaded->project.sources().sources();
    EXPECT_EQ(loadedSources[0].descriptor.normalizedPath, sourceAPath);
    ASSERT_TRUE(loadedSources[0].descriptor.sourceIdentity.has_value());
    EXPECT_EQ(loadedSources[0].descriptor.sourceIdentity->fingerprintSha256,
              identityA.value().fingerprintSha256);

    writeFile(sourceBPath, "replacement source B");
    const auto relinkBEvents = std::make_shared<RecordingEventSink>();
    const application::ProjectRelinkRequest relinkB{
        .context = requestContext(33U),
        .sourceId = 1,
        .newSourcePath = sourceBPath,
    };
    ASSERT_EQ(repository.submit(relinkB, relinkBEvents), application::PortSubmitResult::Accepted);
    ASSERT_TRUE(relinkBEvents->waitForTerminal(relinkB.context, std::chrono::seconds{2}));
    const auto relinkBCriticalEvents = relinkBEvents->criticalEvents();
    ASSERT_EQ(relinkBCriticalEvents.size(), 2U);
    const auto* preparedB =
        std::get_if<application::SourceRelinkPrepared>(&relinkBCriticalEvents[0]);
    ASSERT_NE(preparedB, nullptr);
    EXPECT_EQ(preparedB->context, relinkB.context);
    EXPECT_EQ(preparedB->candidate.sourceId(), 1U);
    EXPECT_TRUE(preparedB->candidate.normalizedPath().is_absolute());
    EXPECT_TRUE(preparedB->candidate.sourceIdentity().isComplete());
    EXPECT_TRUE(isSucceededTerminal(relinkBCriticalEvents[1]));
}

TEST_F(ProjectRepositoryTests, DropsQueuedCompletionWhenEventSinkIsDestroyed) {
    std::atomic<std::size_t> postCalls{0U};
    std::atomic<std::size_t> destructions{0U};

    {
        ProjectRepository repository;
        const auto blockingEvents = std::make_shared<BlockingEventSink>();
        const application::ProjectLoadRequest blockingRequest{
            .context = requestContext(60U),
            .projectPath = path("blocking-missing.dvsproj"),
        };
        ASSERT_EQ(repository.submit(blockingRequest, blockingEvents),
                  application::PortSubmitResult::Accepted);
        if (!blockingEvents->waitForPost(std::chrono::seconds{2})) {
            blockingEvents->release();
            FAIL() << "The blocking repository operation did not reach event publication.";
        }

        auto expiringEvents = std::make_shared<ExpiringEventSink>(postCalls, destructions);
        const std::weak_ptr<application::IApplicationEventSink> weakEvents{expiringEvents};
        const application::ProjectLoadRequest queuedRequest{
            .context = requestContext(61U),
            .projectPath = path("queued-missing.dvsproj"),
        };
        const auto queuedResult = repository.submit(queuedRequest, expiringEvents);
        if (queuedResult != application::PortSubmitResult::Accepted) {
            blockingEvents->release();
            FAIL() << "The queued repository operation was not accepted.";
        }

        expiringEvents.reset();
        EXPECT_TRUE(weakEvents.expired());
        EXPECT_EQ(destructions.load(std::memory_order_relaxed), 1U);
        blockingEvents->release();
    }

    EXPECT_EQ(postCalls.load(std::memory_order_relaxed), 0U);
    EXPECT_EQ(destructions.load(std::memory_order_relaxed), 1U);
}

} // namespace
} // namespace dvs::persistence
