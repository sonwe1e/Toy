#include "dvs/ui/ReviewController.h"

#include <QFileInfo>
#include <QMetaObject>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QVariantMap>

#include <array>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <optional>
#include <stdexcept>
#include <utility>

namespace dvs::ui {
namespace {

constexpr int kProjectionIntervalMilliseconds = 16;
constexpr float kLowAlignmentConfidence = 0.30F;
constexpr qsizetype kMaximumAlignmentTimelineMarkers = 256;

struct LocalFileCandidate final {
    std::filesystem::path path;
    QString filename;
};

struct LocalFileValidation final {
    std::optional<LocalFileCandidate> candidate;
    QString errorKey;
};

[[nodiscard]] LocalFileValidation validateLocalFile(const QUrl& url) {
    if (!url.isValid() || !url.isLocalFile() || url.toLocalFile().isEmpty()) {
        return LocalFileValidation{.errorKey = QStringLiteral("invalid-argument")};
    }

    const QFileInfo input{url.toLocalFile()};
    const QString canonicalPath = input.canonicalFilePath();
    if (!input.isFile() || canonicalPath.isEmpty()) {
        return LocalFileValidation{.errorKey = QStringLiteral("source-missing")};
    }

    const QFileInfo canonical{canonicalPath};
    return LocalFileValidation{
        .candidate =
            LocalFileCandidate{
                .path = std::filesystem::path{canonicalPath.toStdWString()},
                .filename = canonical.fileName(),
            },
    };
}

[[nodiscard]] ReviewController::ReviewDisplayState
mapDisplayState(const domain::SessionState state) noexcept {
    switch (state) {
    case domain::SessionState::kEmpty:
        return ReviewController::ReviewDisplayState::Empty;
    case domain::SessionState::kLoading:
        return ReviewController::ReviewDisplayState::Loading;
    case domain::SessionState::kReady:
        return ReviewController::ReviewDisplayState::Ready;
    case domain::SessionState::kInvalid:
        return ReviewController::ReviewDisplayState::Invalid;
    case domain::SessionState::kError:
        return ReviewController::ReviewDisplayState::Error;
    }
    return ReviewController::ReviewDisplayState::Error;
}

struct ReviewView final {
    QString sourceAFilename;
    QString sourceBFilename;
    QString sourceCFilename;
    ReviewController::ReviewDisplayState displayState = ReviewController::ReviewDisplayState::Empty;
    bool busy = false;
    bool playing = false;
    bool graphicsReady = false;
    qint64 currentFrame = -1;
    qulonglong totalFrames = 0U;
    QString sourceAErrorKey;
    QString sourceBErrorKey;
    QString sourceCErrorKey;
    QString pairErrorKey;
    QString frameMappingStatus;
    QString alignmentEstimateStatus;
    QString sequenceAlignmentStatus;
    QString manualAnchorStatus;
    QVariantList alignmentTimelineMarkers;
    bool manualAnchorActive = false;
    bool autoAlignmentActive = false;
    QStringList compatibilityWarningKeys;
    bool canOpen = false;
    bool canFirst = false;
    bool canPrevious = false;
    bool canNext = false;
    bool canLast = false;
    bool canPlay = false;
    bool canPause = false;

    [[nodiscard]] bool operator==(const ReviewView&) const = default;
};

[[nodiscard]] QString sourceName(const domain::SourceId sourceId) {
    return sourceId < 3U ? QString{QChar{static_cast<char16_t>(u'A' + sourceId)}}
                         : QString::number(sourceId);
}

void appendTimelineMarker(QVariantList& markers,
                          const domain::FrameId frameId,
                          const QString& kind,
                          const QString& source,
                          const int confidence = 100) {
    if (markers.size() >= kMaximumAlignmentTimelineMarkers) {
        return;
    }
    markers.push_back(QVariantMap{
        {QStringLiteral("frame"), QVariant::fromValue<qulonglong>(frameId.value())},
        {QStringLiteral("kind"), kind},
        {QStringLiteral("source"), source},
        {QStringLiteral("confidence"), confidence},
    });
}

} // namespace

class ReviewController::Impl final {
public:
    Impl(ReviewController& owner, Dependencies dependencies)
        : owner_(owner), dependencies_(std::move(dependencies)) {
        if (!dependencies_.submit || !dependencies_.snapshot ||
            !dependencies_.takeCompletedCommands) {
            throw std::invalid_argument{"Review controller dependencies must not be empty."};
        }

        QObject::connect(&projectionTimer_, &QTimer::timeout, &owner_, [this] { refresh(); });
        projectionTimer_.setInterval(kProjectionIntervalMilliseconds);
        refresh();
        if (!stopped_) {
            projectionTimer_.start();
        }
    }

    ~Impl() {
        projectionTimer_.stop();
    }

    [[nodiscard]] const ReviewView& view() const noexcept {
        return view_;
    }

    [[nodiscard]] bool openComparison(const QUrl& first, const QUrl& second) {
        return openComparisonSet(first, second, QUrl{}, -1);
    }

    [[nodiscard]] bool openComparisonSet(const QUrl& first,
                                         const QUrl& second,
                                         const QUrl& third,
                                         const int referenceSourceIndex) {
        if (!onOwnerThread() || stopped_) {
            return false;
        }
        refresh();
        if (stopped_ || pendingCommand_.has_value()) {
            return false;
        }

        LocalFileValidation validatedA;
        LocalFileValidation validatedB;
        LocalFileValidation validatedC;
        try {
            validatedA = validateLocalFile(first);
            validatedB = validateLocalFile(second);
            if (!third.isEmpty()) {
                validatedC = validateLocalFile(third);
            }
        } catch (...) {
            validatedA = LocalFileValidation{.errorKey = QStringLiteral("invalid-argument")};
            validatedB = LocalFileValidation{.errorKey = QStringLiteral("invalid-argument")};
            if (!third.isEmpty()) {
                validatedC = LocalFileValidation{.errorKey = QStringLiteral("invalid-argument")};
            }
        }

        sourceA_ = std::move(validatedA.candidate);
        sourceB_ = std::move(validatedB.candidate);
        sourceC_ = std::move(validatedC.candidate);
        localSourceAErrorKey_ = std::move(validatedA.errorKey);
        localSourceBErrorKey_ = std::move(validatedB.errorKey);
        localSourceCErrorKey_ = std::move(validatedC.errorKey);
        publishProjection();

        const int sourceCount = sourceC_.has_value() ? 3 : 2;
        if (!sourceA_.has_value() || !sourceB_.has_value() || !localSourceCErrorKey_.isEmpty() ||
            referenceSourceIndex < -1 || referenceSourceIndex >= sourceCount || !view_.canOpen) {
            return false;
        }
        const std::optional<application::CommandContext> context = allocateCommandContext();
        if (!context.has_value()) {
            failClosed();
            return false;
        }
        std::vector<application::OpenComparisonSource> sources;
        sources.reserve(static_cast<std::size_t>(sourceCount));
        const auto appendSource = [&sources, referenceSourceIndex](const LocalFileCandidate& source,
                                                                   const int index) {
            sources.push_back(application::OpenComparisonSource{
                .path = source.path,
                .role = index == referenceSourceIndex ? domain::ComparisonRole::kReference
                                                      : domain::ComparisonRole::kPrediction,
                .displayName = source.filename.toStdString(),
            });
        };
        appendSource(*sourceA_, 0);
        appendSource(*sourceB_, 1);
        if (sourceC_.has_value()) {
            appendSource(*sourceC_, 2);
        }
        return dispatch(application::OpenComparisonCommand{
            .context = *context,
            .sources = std::move(sources),
        });
    }

    [[nodiscard]] bool first() {
        return dispatchNavigation([](const ReviewView& view) { return view.canFirst; },
                                  [](const application::CommandContext& context) {
                                      return application::PlaybackCommand{
                                          application::FirstFrameCommand{.context = context}};
                                  });
    }

    [[nodiscard]] bool previous() {
        return stepFrames(-1);
    }

    [[nodiscard]] bool next() {
        return stepFrames(1);
    }

    [[nodiscard]] bool last() {
        return dispatchNavigation([](const ReviewView& view) { return view.canLast; },
                                  [](const application::CommandContext& context) {
                                      return application::PlaybackCommand{
                                          application::LastFrameCommand{.context = context}};
                                  });
    }

    [[nodiscard]] bool stepFrames(const qint64 delta) {
        if (delta == 0) {
            return false;
        }
        return dispatchNavigation(
            [delta](const ReviewView& view) { return delta < 0 ? view.canPrevious : view.canNext; },
            [delta](const application::CommandContext& context) {
                return application::PlaybackCommand{application::StepFramesCommand{
                    .context = context,
                    .delta = static_cast<std::int64_t>(delta),
                }};
            });
    }

    [[nodiscard]] bool seekFrame(const qint64 frame) {
        return dispatchNavigation(
            [frame](const ReviewView& view) {
                return view.canFirst && frame >= 0 &&
                       static_cast<qulonglong>(frame) < view.totalFrames &&
                       frame != view.currentFrame;
            },
            [frame](const application::CommandContext& context) {
                return application::PlaybackCommand{application::SeekFrameCommand{
                    .context = context,
                    .frameId = domain::FrameId{static_cast<std::int64_t>(frame)},
                }};
            });
    }

    [[nodiscard]] bool applyAlignmentOffsets(const qint64 sourceAFrames,
                                             const qint64 sourceBFrames,
                                             const qint64 sourceCFrames) {
        return dispatchNavigation(
            [](const ReviewView& view) { return view.canFirst; },
            [this, sourceAFrames, sourceBFrames, sourceCFrames](
                const application::CommandContext& context) {
                std::vector<application::SourceFrameOffset> offsets;
                const std::array<qint64, 3U> values{sourceAFrames, sourceBFrames, sourceCFrames};
                const std::size_t sourceCount = sourceC_.has_value() ? 3U : 2U;
                offsets.reserve(sourceCount);
                for (std::size_t index = 0U; index < sourceCount; ++index) {
                    if (values[index] != 0) {
                        offsets.push_back(application::SourceFrameOffset{
                            .sourceId = static_cast<domain::SourceId>(index),
                            .frames = static_cast<std::int64_t>(values[index]),
                        });
                    }
                }
                return application::PlaybackCommand{application::SetAlignmentOffsetsCommand{
                    .context = context,
                    .sourceOffsets = std::move(offsets),
                }};
            });
    }

    [[nodiscard]] bool estimateAlignment() {
        return dispatchNavigation(
            [](const ReviewView& view) { return view.canFirst; },
            [](const application::CommandContext& context) {
                return application::PlaybackCommand{
                    application::EstimateAlignmentCommand{.context = context}};
            });
    }

    [[nodiscard]] bool analyzeSequenceAlignment() {
        return dispatchNavigation(
            [](const ReviewView& view) { return view.canFirst; },
            [](const application::CommandContext& context) {
                return application::PlaybackCommand{
                    application::AnalyzeSequenceAlignmentCommand{.context = context}};
            });
    }

    [[nodiscard]] bool setManualAlignmentAnchor(const int sourceIndex,
                                                const qint64 canonicalFrame,
                                                const qint64 sourceFrame) {
        if (sourceIndex < 0 || sourceIndex >= (sourceC_.has_value() ? 3 : 2) ||
            canonicalFrame < 0 || sourceFrame < 0) {
            return false;
        }
        return dispatchNavigation(
            [](const ReviewView& view) { return view.canFirst; },
            [sourceIndex, canonicalFrame, sourceFrame](const application::CommandContext& context) {
                return application::PlaybackCommand{application::SetManualAlignmentAnchorCommand{
                    .context = context,
                    .sourceId = static_cast<domain::SourceId>(sourceIndex),
                    .anchor =
                        application::ManualAlignmentAnchor{
                            .canonicalFrameId =
                                domain::FrameId{static_cast<std::int64_t>(canonicalFrame)},
                            .sourceFrameId =
                                domain::FrameId{static_cast<std::int64_t>(sourceFrame)},
                        },
                }};
            });
    }

    [[nodiscard]] bool clearManualAlignmentAnchors() {
        return dispatchNavigation(
            [](const ReviewView& view) { return view.canFirst; },
            [](const application::CommandContext& context) {
                return application::PlaybackCommand{
                    application::ClearManualAlignmentAnchorsCommand{.context = context}};
            });
    }

    [[nodiscard]] bool play() {
        return dispatchTransport([](const ReviewView& view) { return view.canPlay; },
                                 [](const application::CommandContext& context) {
                                     return application::PlaybackCommand{
                                         application::PlayCommand{.context = context}};
                                 });
    }

    [[nodiscard]] bool pause() {
        return dispatchTransport([](const ReviewView& view) { return view.canPause; },
                                 [](const application::CommandContext& context) {
                                     return application::PlaybackCommand{
                                         application::PauseCommand{.context = context}};
                                 });
    }

    [[nodiscard]] bool togglePlayback() {
        if (!onOwnerThread() || stopped_) {
            return false;
        }
        refresh();
        if (stopped_) {
            return false;
        }
        return view_.playing ? pause() : play();
    }

    void stop() noexcept {
        if (stopped_) {
            return;
        }
        stopped_ = true;
        projectionTimer_.stop();
        pendingCommand_.reset();
        pendingTransportCommand_.reset();
        publishProjection();
    }

private:
    [[nodiscard]] bool onOwnerThread() const noexcept {
        return owner_.thread() == QThread::currentThread();
    }

    void refresh() noexcept {
        if (stopped_ || !onOwnerThread()) {
            return;
        }

        try {
            for (const application::CommandTerminal& terminal :
                 dependencies_.takeCompletedCommands()) {
                if (pendingCommand_.has_value() && terminal.context == *pendingCommand_) {
                    pendingCommand_.reset();
                }
                if (pendingTransportCommand_.has_value() &&
                    terminal.context == *pendingTransportCommand_) {
                    pendingTransportCommand_.reset();
                }
            }

            std::shared_ptr<const application::SessionSnapshot> snapshot = dependencies_.snapshot();
            if (!snapshot || !snapshot->isConsistent()) {
                failClosed();
                return;
            }
            snapshot_ = std::move(snapshot);
            publishProjection();
        } catch (...) {
            failClosed();
        }
    }

    void failClosed() noexcept {
        stopped_ = true;
        projectionTimer_.stop();
        pendingCommand_.reset();
        pendingTransportCommand_.reset();
        publishProjection();
    }

    void publishProjection() noexcept {
        ReviewView next;
        next.sourceAFilename = sourceA_.has_value() ? sourceA_->filename : QString{};
        next.sourceBFilename = sourceB_.has_value() ? sourceB_->filename : QString{};
        next.sourceCFilename = sourceC_.has_value() ? sourceC_->filename : QString{};
        next.sourceAErrorKey = localSourceAErrorKey_;
        next.sourceBErrorKey = localSourceBErrorKey_;
        next.sourceCErrorKey = localSourceCErrorKey_;

        if (snapshot_) {
            next.displayState = mapDisplayState(snapshot_->sessionState);
            next.graphicsReady = snapshot_->graphicsReady && !stopped_;
            next.currentFrame = snapshot_->displayedFrame.has_value()
                                    ? static_cast<qint64>(snapshot_->displayedFrame->value())
                                    : -1;
            next.totalFrames = static_cast<qulonglong>(snapshot_->canonicalFrameCount);
            next.playing = snapshot_->playbackState == domain::PlaybackState::kPlaying ||
                           snapshot_->playbackState == domain::PlaybackState::kBuffering;

            QStringList mappingParts;
            for (const application::PresentedSourceState& source : snapshot_->presentedSources) {
                const QString currentSourceName = sourceName(source.sourceId);
                if (source.matchKind == application::FrameMatchKind::Missing) {
                    mappingParts.push_back(
                        QStringLiteral("%1: Missing frame").arg(currentSourceName));
                } else if ((source.matchKind == application::FrameMatchKind::GlobalOffset ||
                            source.matchKind == application::FrameMatchKind::AutoAligned ||
                            source.matchKind == application::FrameMatchKind::ManualAnchor) &&
                           source.sourceFrameId.has_value()) {
                    const qint64 offset = next.currentFrame >= 0
                                              ? source.sourceFrameId->value() - next.currentFrame
                                              : 0;
                    QString origin = QStringLiteral("manual");
                    if (source.matchKind == application::FrameMatchKind::AutoAligned) {
                        origin = QStringLiteral("auto");
                    } else if (source.matchKind == application::FrameMatchKind::ManualAnchor) {
                        origin = QStringLiteral("anchor");
                    }
                    mappingParts.push_back(
                        QStringLiteral("%1: source frame %2 (%3 offset %4%5, %6%)")
                            .arg(currentSourceName)
                            .arg(source.sourceFrameId->value() + 1)
                            .arg(origin)
                            .arg(offset >= 0 ? QStringLiteral("+") : QString{})
                            .arg(offset)
                            .arg(qRound(source.alignmentConfidence * 100.0F)));
                }
            }
            next.frameMappingStatus = mappingParts.join(QStringLiteral("  |  "));
            QStringList estimateParts;
            for (const application::GlobalOffsetEstimate& estimate :
                 snapshot_->alignmentEstimates) {
                const QString currentSourceName = sourceName(estimate.sourceId);
                const QString signedOffset =
                    QStringLiteral("%1%2")
                        .arg(estimate.bestOffset >= 0 ? QStringLiteral("+") : QString{})
                        .arg(estimate.bestOffset);
                const int confidence = qRound(estimate.confidence * 100.0F);
                if (estimate.autoApplicable) {
                    next.autoAlignmentActive = true;
                    estimateParts.push_back(QStringLiteral("%1: auto %2 (%3%)")
                                                .arg(currentSourceName, signedOffset)
                                                .arg(confidence));
                } else {
                    estimateParts.push_back(
                        QStringLiteral("%1: suggested %2 (%3%, review manually)")
                            .arg(currentSourceName, signedOffset)
                            .arg(confidence));
                }
            }
            next.alignmentEstimateStatus = estimateParts.join(QStringLiteral("  |  "));
            QStringList sequenceParts;
            QVariantList lowConfidenceTimelineMarkers;
            for (const application::SequenceAlignmentResult& result :
                 snapshot_->sequenceAlignments) {
                const QString currentSourceName = sourceName(result.sourceId);
                QStringList anomalyParts;
                for (const application::SequenceAlignmentAnomaly& anomaly : result.anomalies) {
                    QString kind;
                    switch (anomaly.kind) {
                    case application::SequenceAlignmentAnomalyKind::TargetFrameMissing:
                        kind = QStringLiteral("missing");
                        break;
                    case application::SequenceAlignmentAnomalyKind::TargetFrameExtra:
                        kind = QStringLiteral("extra");
                        break;
                    case application::SequenceAlignmentAnomalyKind::TargetFrameDuplicate:
                        kind = QStringLiteral("duplicate");
                        break;
                    }
                    const std::optional<domain::FrameId> position =
                        anomaly.canonicalFrameId.has_value() ? anomaly.canonicalFrameId
                                                             : anomaly.sourceFrameId;
                    if (position.has_value()) {
                        appendTimelineMarker(
                            next.alignmentTimelineMarkers, *position, kind, currentSourceName);
                    }
                    anomalyParts.push_back(
                        position.has_value()
                            ? QStringLiteral("%1 @ %2").arg(kind).arg(position->value() + 1)
                            : kind);
                    if (anomalyParts.size() >= 3) {
                        break;
                    }
                }
                const QString mode =
                    result.autoApplicable ? QStringLiteral("mapped") : QStringLiteral("suggested");
                sequenceParts.push_back(QStringLiteral("%1: sequence %2, %3% confidence%4")
                                            .arg(currentSourceName)
                                            .arg(mode)
                                            .arg(qRound(result.confidence * 100.0F))
                                            .arg(anomalyParts.isEmpty()
                                                     ? QStringLiteral(", no local anomalies")
                                                     : QStringLiteral(", %1").arg(anomalyParts.join(
                                                           QStringLiteral(", ")))));
                if (result.autoApplicable) {
                    next.autoAlignmentActive = true;
                }

                bool inLowConfidenceRun = false;
                bool projectedLowConfidence = false;
                for (const application::SequenceAlignmentEntry& entry : result.entries) {
                    const bool lowConfidence = entry.sourceFrameId.has_value() &&
                                               entry.confidence < kLowAlignmentConfidence;
                    if (lowConfidence && !inLowConfidenceRun) {
                        appendTimelineMarker(lowConfidenceTimelineMarkers,
                                             entry.canonicalFrameId,
                                             QStringLiteral("low-confidence"),
                                             currentSourceName,
                                             qRound(entry.confidence * 100.0F));
                        projectedLowConfidence = true;
                    }
                    inLowConfidenceRun = lowConfidence;
                }
                if (!result.autoApplicable && !projectedLowConfidence && !result.entries.empty()) {
                    appendTimelineMarker(lowConfidenceTimelineMarkers,
                                         result.entries.front().canonicalFrameId,
                                         QStringLiteral("low-confidence"),
                                         currentSourceName,
                                         qRound(result.confidence * 100.0F));
                }
            }
            next.sequenceAlignmentStatus = sequenceParts.join(QStringLiteral("  |  "));
            QStringList anchorParts;
            for (const application::SourceAlignmentAnchors& sourceAnchors :
                 snapshot_->manualAlignmentAnchors) {
                const QString currentSourceName = sourceName(sourceAnchors.sourceId);
                QStringList pairs;
                for (const application::ManualAlignmentAnchor& anchor : sourceAnchors.anchors) {
                    pairs.push_back(QStringLiteral("%1↔%2")
                                        .arg(anchor.canonicalFrameId.value() + 1)
                                        .arg(anchor.sourceFrameId.value() + 1));
                    appendTimelineMarker(next.alignmentTimelineMarkers,
                                         anchor.canonicalFrameId,
                                         QStringLiteral("anchor"),
                                         currentSourceName);
                }
                anchorParts.push_back(
                    QStringLiteral("%1: anchors %2").arg(currentSourceName, pairs.join(", ")));
            }
            next.manualAnchorStatus = anchorParts.join(QStringLiteral("  |  "));
            next.manualAnchorActive = !snapshot_->manualAlignmentAnchors.empty();
            for (const QVariant& marker : lowConfidenceTimelineMarkers) {
                if (next.alignmentTimelineMarkers.size() >= kMaximumAlignmentTimelineMarkers) {
                    break;
                }
                next.alignmentTimelineMarkers.push_back(marker);
            }
            for (const domain::MediaErrorCode warning : snapshot_->compatibilityWarnings) {
                next.compatibilityWarningKeys.push_back(
                    QString::fromLatin1(domain::stableId(warning).data(),
                                        static_cast<qsizetype>(domain::stableId(warning).size())));
            }

            if (snapshot_->lastError.has_value()) {
                const QString key = QString::fromStdString(snapshot_->lastError->userMessageKey);
                const std::optional<domain::SourceId> errorSource = snapshot_->lastError->source;
                if (errorSource.has_value() && *errorSource == 0U) {
                    if (next.sourceAErrorKey.isEmpty()) {
                        next.sourceAErrorKey = key;
                    }
                } else if (errorSource.has_value() && *errorSource == 1U) {
                    if (next.sourceBErrorKey.isEmpty()) {
                        next.sourceBErrorKey = key;
                    }
                } else if (errorSource.has_value() && *errorSource == 2U) {
                    if (next.sourceCErrorKey.isEmpty()) {
                        next.sourceCErrorKey = key;
                    }
                } else {
                    next.pairErrorKey = key;
                }
            }
        }

        next.busy = pendingCommand_.has_value() && !stopped_;
        const bool transportPending = pendingTransportCommand_.has_value();
        const bool playbackDraining =
            snapshot_ && !next.playing && snapshot_->requestedFrame.has_value();
        const bool playbackBlocksCommands = next.playing || playbackDraining || transportPending;
        next.canOpen = next.graphicsReady && !next.busy && !playbackBlocksCommands;
        // Navigation stays available while playing or while an earlier navigation is in flight:
        // the coordinator pauses playback on the first frame step and coalesces rapid presses
        // onto the newest target (USERPLAN 3.1/6.2). Opens remain gated by transport activity.
        const bool canNavigate = next.graphicsReady && !next.busy &&
                                 next.displayState == ReviewDisplayState::Ready &&
                                 next.totalFrames != 0U;
        next.canFirst = canNavigate;
        next.canLast = canNavigate;
        next.canPrevious = canNavigate && next.currentFrame > 0;
        next.canNext = canNavigate && next.currentFrame >= 0 &&
                       static_cast<qulonglong>(next.currentFrame) + 1U < next.totalFrames;
        next.canPlay = next.graphicsReady && !next.busy && !playbackBlocksCommands &&
                       next.displayState == ReviewDisplayState::Ready && next.currentFrame >= 0 &&
                       next.totalFrames > 1U;
        next.canPause = !next.busy && !transportPending && next.playing;

        if (!(next == view_)) {
            view_ = std::move(next);
            Q_EMIT owner_.stateChanged();
        }
    }

    [[nodiscard]] std::optional<application::CommandContext> allocateCommandContext() noexcept {
        if (!snapshot_ || commandIdsExhausted_) {
            return std::nullopt;
        }
        const std::uint64_t commandId = nextCommandId_;
        if (nextCommandId_ == (std::numeric_limits<std::uint64_t>::max)()) {
            commandIdsExhausted_ = true;
        } else {
            ++nextCommandId_;
        }
        return application::CommandContext{
            .sessionId = snapshot_->sessionId,
            .sessionEpoch = snapshot_->sessionEpoch,
            .commandId = domain::CommandId{commandId},
        };
    }

    [[nodiscard]] bool dispatch(application::PlaybackCommand command) noexcept {
        const application::CommandContext context = application::commandContext(command);
        application::PortSubmitResult result = application::PortSubmitResult::Closed;
        try {
            result = dependencies_.submit(std::move(command));
        } catch (...) {
            failClosed();
            return false;
        }
        if (result != application::PortSubmitResult::Accepted) {
            return false;
        }
        pendingCommand_ = context;
        publishProjection();
        return true;
    }

    template <typename Enabled, typename Factory>
    [[nodiscard]] bool dispatchNavigation(Enabled enabled, Factory factory) {
        if (!onOwnerThread() || stopped_) {
            return false;
        }
        refresh();
        if (stopped_) {
            return false;
        }

        if (!enabled(view_)) {
            return false;
        }
        const std::optional<application::CommandContext> context = allocateCommandContext();
        if (!context.has_value()) {
            failClosed();
            return false;
        }
        return dispatch(factory(*context));
    }

    template <typename Enabled, typename Factory>
    [[nodiscard]] bool dispatchTransport(Enabled enabled, Factory factory) {
        if (!onOwnerThread() || stopped_) {
            return false;
        }
        refresh();
        if (stopped_ || !enabled(view_)) {
            return false;
        }
        const std::optional<application::CommandContext> context = allocateCommandContext();
        if (!context.has_value()) {
            failClosed();
            return false;
        }

        application::PlaybackCommand command = factory(*context);
        application::PortSubmitResult result = application::PortSubmitResult::Closed;
        try {
            result = dependencies_.submit(std::move(command));
        } catch (...) {
            failClosed();
            return false;
        }
        if (result != application::PortSubmitResult::Accepted) {
            return false;
        }
        pendingTransportCommand_ = *context;
        publishProjection();
        return true;
    }

    ReviewController& owner_;
    Dependencies dependencies_;
    QTimer projectionTimer_;
    std::shared_ptr<const application::SessionSnapshot> snapshot_;
    std::optional<LocalFileCandidate> sourceA_;
    std::optional<LocalFileCandidate> sourceB_;
    std::optional<LocalFileCandidate> sourceC_;
    QString localSourceAErrorKey_;
    QString localSourceBErrorKey_;
    QString localSourceCErrorKey_;
    std::optional<application::CommandContext> pendingCommand_;
    std::optional<application::CommandContext> pendingTransportCommand_;
    std::uint64_t nextCommandId_ = 1U;
    bool commandIdsExhausted_ = false;
    bool stopped_ = false;
    ReviewView view_;
};

ReviewController::ReviewController(Dependencies dependencies, QObject* const parent)
    : QObject(parent), impl_(std::make_unique<Impl>(*this, std::move(dependencies))) {}

ReviewController::~ReviewController() = default;

QString ReviewController::sourceAFilename() const {
    return impl_->view().sourceAFilename;
}

QString ReviewController::sourceBFilename() const {
    return impl_->view().sourceBFilename;
}

QString ReviewController::sourceCFilename() const {
    return impl_->view().sourceCFilename;
}

ReviewController::ReviewDisplayState ReviewController::displayState() const noexcept {
    return impl_->view().displayState;
}

bool ReviewController::busy() const noexcept {
    return impl_->view().busy;
}

bool ReviewController::playing() const noexcept {
    return impl_->view().playing;
}

bool ReviewController::graphicsReady() const noexcept {
    return impl_->view().graphicsReady;
}

qint64 ReviewController::currentFrame() const noexcept {
    return impl_->view().currentFrame;
}

qulonglong ReviewController::totalFrames() const noexcept {
    return impl_->view().totalFrames;
}

QString ReviewController::sourceAErrorKey() const {
    return impl_->view().sourceAErrorKey;
}

QString ReviewController::sourceBErrorKey() const {
    return impl_->view().sourceBErrorKey;
}

QString ReviewController::sourceCErrorKey() const {
    return impl_->view().sourceCErrorKey;
}

QString ReviewController::pairErrorKey() const {
    return impl_->view().pairErrorKey;
}

QString ReviewController::frameMappingStatus() const {
    return impl_->view().frameMappingStatus;
}

QString ReviewController::alignmentEstimateStatus() const {
    return impl_->view().alignmentEstimateStatus;
}

QString ReviewController::sequenceAlignmentStatus() const {
    return impl_->view().sequenceAlignmentStatus;
}

QString ReviewController::manualAnchorStatus() const {
    return impl_->view().manualAnchorStatus;
}

QVariantList ReviewController::alignmentTimelineMarkers() const {
    return impl_->view().alignmentTimelineMarkers;
}

bool ReviewController::manualAnchorActive() const noexcept {
    return impl_->view().manualAnchorActive;
}

bool ReviewController::autoAlignmentActive() const noexcept {
    return impl_->view().autoAlignmentActive;
}

QStringList ReviewController::compatibilityWarningKeys() const {
    return impl_->view().compatibilityWarningKeys;
}

bool ReviewController::canOpen() const noexcept {
    return impl_->view().canOpen;
}

bool ReviewController::canFirst() const noexcept {
    return impl_->view().canFirst;
}

bool ReviewController::canPrevious() const noexcept {
    return impl_->view().canPrevious;
}

bool ReviewController::canNext() const noexcept {
    return impl_->view().canNext;
}

bool ReviewController::canLast() const noexcept {
    return impl_->view().canLast;
}

bool ReviewController::canPlay() const noexcept {
    return impl_->view().canPlay;
}

bool ReviewController::canPause() const noexcept {
    return impl_->view().canPause;
}

bool ReviewController::openComparison(const QUrl& first, const QUrl& second) {
    return impl_->openComparison(first, second);
}

bool ReviewController::openComparisonSet(const QUrl& first,
                                         const QUrl& second,
                                         const QUrl& third,
                                         const int referenceSourceIndex) {
    return impl_->openComparisonSet(first, second, third, referenceSourceIndex);
}

bool ReviewController::first() {
    return impl_->first();
}

bool ReviewController::previous() {
    return impl_->previous();
}

bool ReviewController::next() {
    return impl_->next();
}

bool ReviewController::last() {
    return impl_->last();
}

bool ReviewController::stepFrames(const qint64 delta) {
    return impl_->stepFrames(delta);
}

bool ReviewController::seekFrame(const qint64 frame) {
    return impl_->seekFrame(frame);
}

bool ReviewController::applyAlignmentOffsets(const qint64 sourceAFrames,
                                             const qint64 sourceBFrames,
                                             const qint64 sourceCFrames) {
    return impl_->applyAlignmentOffsets(sourceAFrames, sourceBFrames, sourceCFrames);
}

bool ReviewController::estimateAlignment() {
    return impl_->estimateAlignment();
}

bool ReviewController::analyzeSequenceAlignment() {
    return impl_->analyzeSequenceAlignment();
}

bool ReviewController::setManualAlignmentAnchor(const int sourceIndex,
                                                const qint64 canonicalFrame,
                                                const qint64 sourceFrame) {
    return impl_->setManualAlignmentAnchor(sourceIndex, canonicalFrame, sourceFrame);
}

bool ReviewController::clearManualAlignmentAnchors() {
    return impl_->clearManualAlignmentAnchors();
}

bool ReviewController::play() {
    return impl_->play();
}

bool ReviewController::pause() {
    return impl_->pause();
}

bool ReviewController::togglePlayback() {
    return impl_->togglePlayback();
}

void ReviewController::stop() noexcept {
    if (thread() != QThread::currentThread()) {
        static_cast<void>(QMetaObject::invokeMethod(this, "stop", Qt::QueuedConnection));
        return;
    }
    impl_->stop();
}

} // namespace dvs::ui
