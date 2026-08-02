#pragma once

#include <QObject>
#include <QVariantList>
#include <QVariantMap>

#include <deque>

namespace dvs::ui {

class ReviewController;

// Owns review-shell state that must remain coherent across QML dialogs and external startup
// requests. Media truth stays in ReviewController; this object separates active backend sources
// from staged user choices and serializes startup ingress on the GUI thread.
class ReviewShellController final : public QObject {
    Q_OBJECT

    Q_PROPERTY(QVariantList activeSources READ activeSources NOTIFY stateChanged)
    Q_PROPERTY(QVariantList stagedSources READ stagedSources NOTIFY stateChanged)
    Q_PROPERTY(int canonicalSourceIndex READ canonicalSourceIndex NOTIFY stateChanged)
    Q_PROPERTY(qulonglong activeGeneration READ activeGeneration NOTIFY stateChanged)
    Q_PROPERTY(int stagedReferenceIndex READ stagedReferenceIndex WRITE setStagedReferenceIndex
                   NOTIFY stateChanged)
    Q_PROPERTY(int queuedStartupRequestCount READ queuedStartupRequestCount NOTIFY stateChanged)
    Q_PROPERTY(int queuedIntentCount READ queuedIntentCount NOTIFY stateChanged)
    Q_PROPERTY(bool startupRequestActive READ startupRequestActive NOTIFY stateChanged)
    Q_PROPERTY(bool chromeVisible READ chromeVisible WRITE setChromeVisible NOTIFY stateChanged)
    Q_PROPERTY(
        bool inspectorVisible READ inspectorVisible WRITE setInspectorVisible NOTIFY stateChanged)
    Q_PROPERTY(bool hasPendingAction READ hasPendingAction NOTIFY stateChanged)
    Q_PROPERTY(QVariantMap pendingAction READ pendingAction NOTIFY stateChanged)
    Q_PROPERTY(int openIntent READ openIntent NOTIFY stateChanged)
    Q_PROPERTY(qint64 inFrame READ inFrame NOTIFY stateChanged)
    Q_PROPERTY(qint64 outFrame READ outFrame NOTIFY stateChanged)
    Q_PROPERTY(double inMediaTime READ inMediaTime NOTIFY stateChanged)
    Q_PROPERTY(double outMediaTime READ outMediaTime NOTIFY stateChanged)
    Q_PROPERTY(bool rangePlaybackActive READ rangePlaybackActive NOTIFY stateChanged)
    Q_PROPERTY(bool rangeStartPending READ rangeStartPending NOTIFY stateChanged)

public:
    enum OpenIntent {
        NewReview = 0,
        ReplaceSources = 1,
        ChangeReference = 2,
    };
    Q_ENUM(OpenIntent)

    explicit ReviewShellController(ReviewController& review, QObject* parent = nullptr);

    [[nodiscard]] QVariantList activeSources() const;
    [[nodiscard]] QVariantList stagedSources() const;
    [[nodiscard]] int canonicalSourceIndex() const noexcept;
    [[nodiscard]] qulonglong activeGeneration() const noexcept;
    [[nodiscard]] int stagedReferenceIndex() const noexcept;
    [[nodiscard]] int queuedStartupRequestCount() const noexcept;
    [[nodiscard]] int queuedIntentCount() const noexcept;
    [[nodiscard]] bool startupRequestActive() const noexcept;
    [[nodiscard]] bool chromeVisible() const noexcept;
    [[nodiscard]] bool inspectorVisible() const noexcept;
    [[nodiscard]] bool hasPendingAction() const noexcept;
    [[nodiscard]] QVariantMap pendingAction() const;
    [[nodiscard]] int openIntent() const noexcept;
    [[nodiscard]] qint64 inFrame() const noexcept;
    [[nodiscard]] qint64 outFrame() const noexcept;
    [[nodiscard]] double inMediaTime() const noexcept;
    [[nodiscard]] double outMediaTime() const noexcept;
    [[nodiscard]] bool rangePlaybackActive() const noexcept;
    [[nodiscard]] bool rangeStartPending() const noexcept;

    void setStagedReferenceIndex(int sourceIndex);
    void setChromeVisible(bool visible);
    void setInspectorVisible(bool visible);

    Q_INVOKABLE bool stageSources(const QVariantList& sources, int referenceIndex);
    Q_INVOKABLE void clearStagedSources();
    Q_INVOKABLE bool moveStagedSource(int fromIndex, int toIndex);
    Q_INVOKABLE bool openStagedSources(bool preserveDisplayedTime);
    Q_INVOKABLE bool removeActiveSource(int sourceIndex);
    Q_INVOKABLE bool changeReference(int sourceIndex);
    Q_INVOKABLE bool closeSources();
    Q_INVOKABLE bool beginPendingAction(const QVariantMap& action);
    Q_INVOKABLE QVariantMap takePendingAction();
    Q_INVOKABLE void cancelPendingAction();
    Q_INVOKABLE bool enqueueStartupRequest(int kind, const QVariantList& files);
    Q_INVOKABLE QVariantMap takeNextStartupRequest();
    Q_INVOKABLE void completeStartupRequest();
    Q_INVOKABLE bool setRangeIn(qint64 frame, double mediaTime);
    Q_INVOKABLE bool setRangeOut(qint64 frame, double mediaTime);
    Q_INVOKABLE void remapRange(qint64 inFrame, qint64 outFrame);
    Q_INVOKABLE void clearRange();
    Q_INVOKABLE bool setRangePlaybackState(bool active, bool startPending);
    Q_INVOKABLE void setRangeStartPending(bool pending);

Q_SIGNALS:
    void stateChanged();
    void startupRequestAvailable();
    void intentQueued(const QString& message);
    void intentRejected(const QString& message);

private:
    enum class ReviewIntentKind {
        OpenSources,
        ReplaceSources,
        ChangeReference,
        CloseSources,
    };

    struct ReviewIntent final {
        ReviewIntentKind kind = ReviewIntentKind::OpenSources;
        QVariantList sources;
        int referenceIndex = 0;
    };

    struct StartupRequest final {
        int kind = 0;
        QVariantList files;
    };

    void synchronizeActiveSources();
    [[nodiscard]] bool submitOrQueue(ReviewIntent intent);
    [[nodiscard]] bool submitIntent(const ReviewIntent& intent);
    void enqueueIntent(ReviewIntent intent);
    void drainIntentQueue();
    [[nodiscard]] static QString intentDescription(ReviewIntentKind kind);

    ReviewController& review_;
    QVariantList activeSources_;
    QVariantList stagedSources_;
    int canonicalSourceIndex_ = -1;
    qulonglong activeGeneration_ = 0U;
    int stagedReferenceIndex_ = 0;
    std::deque<StartupRequest> startupRequests_;
    std::deque<ReviewIntent> reviewIntents_;
    bool startupRequestActive_ = false;
    bool chromeVisible_ = true;
    bool inspectorVisible_ = false;
    QVariantMap pendingAction_;
    int openIntent_ = NewReview;
    qint64 inFrame_ = -1;
    qint64 outFrame_ = -1;
    double inMediaTime_ = -1.0;
    double outMediaTime_ = -1.0;
    bool rangePlaybackActive_ = false;
    bool rangeStartPending_ = false;
};

} // namespace dvs::ui
