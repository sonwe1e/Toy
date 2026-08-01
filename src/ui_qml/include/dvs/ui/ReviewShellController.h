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
    Q_PROPERTY(bool startupRequestActive READ startupRequestActive NOTIFY stateChanged)
    Q_PROPERTY(bool chromeVisible READ chromeVisible WRITE setChromeVisible NOTIFY stateChanged)
    Q_PROPERTY(
        bool inspectorVisible READ inspectorVisible WRITE setInspectorVisible NOTIFY stateChanged)
    Q_PROPERTY(bool hasPendingAction READ hasPendingAction NOTIFY stateChanged)
    Q_PROPERTY(QVariantMap pendingAction READ pendingAction NOTIFY stateChanged)
    Q_PROPERTY(int openIntent READ openIntent NOTIFY stateChanged)

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
    [[nodiscard]] bool startupRequestActive() const noexcept;
    [[nodiscard]] bool chromeVisible() const noexcept;
    [[nodiscard]] bool inspectorVisible() const noexcept;
    [[nodiscard]] bool hasPendingAction() const noexcept;
    [[nodiscard]] QVariantMap pendingAction() const;
    [[nodiscard]] int openIntent() const noexcept;

    void setStagedReferenceIndex(int sourceIndex);
    void setChromeVisible(bool visible);
    void setInspectorVisible(bool visible);

    Q_INVOKABLE bool stageSources(const QVariantList& sources, int referenceIndex);
    Q_INVOKABLE void clearStagedSources();
    Q_INVOKABLE bool moveStagedSource(int fromIndex, int toIndex);
    Q_INVOKABLE bool openStagedSources(bool preserveDisplayedTime);
    Q_INVOKABLE bool removeActiveSource(int sourceIndex);
    Q_INVOKABLE bool changeReference(int sourceIndex);
    Q_INVOKABLE bool beginPendingAction(const QVariantMap& action);
    Q_INVOKABLE QVariantMap takePendingAction();
    Q_INVOKABLE void cancelPendingAction();
    Q_INVOKABLE bool enqueueStartupRequest(int kind, const QVariantList& files);
    Q_INVOKABLE QVariantMap takeNextStartupRequest();
    Q_INVOKABLE void completeStartupRequest();

Q_SIGNALS:
    void stateChanged();
    void startupRequestAvailable();

private:
    struct StartupRequest final {
        int kind = 0;
        QVariantList files;
    };

    void synchronizeActiveSources();

    ReviewController& review_;
    QVariantList activeSources_;
    QVariantList stagedSources_;
    int canonicalSourceIndex_ = -1;
    qulonglong activeGeneration_ = 0U;
    int stagedReferenceIndex_ = 0;
    std::deque<StartupRequest> startupRequests_;
    bool startupRequestActive_ = false;
    bool chromeVisible_ = true;
    bool inspectorVisible_ = false;
    QVariantMap pendingAction_;
    int openIntent_ = NewReview;
};

} // namespace dvs::ui
