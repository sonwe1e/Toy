#pragma once

#include "dvs/application/Commands.h"
#include "dvs/application/Events.h"
#include "dvs/application/Ports.h"
#include "dvs/application/SessionSnapshot.h"

#include <QObject>
#include <QString>

#include <functional>
#include <memory>
#include <vector>

class QUrl;

namespace dvs::ui {

// GUI-thread projection of the immutable application snapshot. Media work and playback timing
// remain behind the supplied application functions; the 16 ms timer only drains and projects.
class ReviewController final : public QObject {
    Q_OBJECT

    Q_PROPERTY(QString sourceAFilename READ sourceAFilename NOTIFY stateChanged)
    Q_PROPERTY(QString sourceBFilename READ sourceBFilename NOTIFY stateChanged)
    Q_PROPERTY(ReviewDisplayState displayState READ displayState NOTIFY stateChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(bool playing READ playing NOTIFY stateChanged)
    Q_PROPERTY(bool graphicsReady READ graphicsReady NOTIFY stateChanged)
    Q_PROPERTY(qint64 currentFrame READ currentFrame NOTIFY stateChanged)
    Q_PROPERTY(qulonglong totalFrames READ totalFrames NOTIFY stateChanged)
    Q_PROPERTY(QString sourceAErrorKey READ sourceAErrorKey NOTIFY stateChanged)
    Q_PROPERTY(QString sourceBErrorKey READ sourceBErrorKey NOTIFY stateChanged)
    Q_PROPERTY(QString pairErrorKey READ pairErrorKey NOTIFY stateChanged)
    Q_PROPERTY(bool canOpen READ canOpen NOTIFY stateChanged)
    Q_PROPERTY(bool canFirst READ canFirst NOTIFY stateChanged)
    Q_PROPERTY(bool canPrevious READ canPrevious NOTIFY stateChanged)
    Q_PROPERTY(bool canNext READ canNext NOTIFY stateChanged)
    Q_PROPERTY(bool canLast READ canLast NOTIFY stateChanged)
    Q_PROPERTY(bool canPlay READ canPlay NOTIFY stateChanged)
    Q_PROPERTY(bool canPause READ canPause NOTIFY stateChanged)

public:
    enum class ReviewDisplayState {
        Empty,
        Loading,
        Ready,
        Invalid,
        Error,
    };
    Q_ENUM(ReviewDisplayState)

    struct Dependencies final {
        std::function<application::PortSubmitResult(application::PlaybackCommand)> submit;
        std::function<std::shared_ptr<const application::SessionSnapshot>()> snapshot;
        std::function<std::vector<application::CommandTerminal>()> takeCompletedCommands;
    };

    explicit ReviewController(Dependencies dependencies, QObject* parent = nullptr);
    ~ReviewController() override;

    ReviewController(const ReviewController&) = delete;
    ReviewController& operator=(const ReviewController&) = delete;
    ReviewController(ReviewController&&) = delete;
    ReviewController& operator=(ReviewController&&) = delete;

    [[nodiscard]] QString sourceAFilename() const;
    [[nodiscard]] QString sourceBFilename() const;
    [[nodiscard]] ReviewDisplayState displayState() const noexcept;
    [[nodiscard]] bool busy() const noexcept;
    [[nodiscard]] bool playing() const noexcept;
    [[nodiscard]] bool graphicsReady() const noexcept;
    // Zero-based canonical frame ID. -1 means that no frame has been presented.
    [[nodiscard]] qint64 currentFrame() const noexcept;
    [[nodiscard]] qulonglong totalFrames() const noexcept;
    [[nodiscard]] QString sourceAErrorKey() const;
    [[nodiscard]] QString sourceBErrorKey() const;
    [[nodiscard]] QString pairErrorKey() const;
    [[nodiscard]] bool canOpen() const noexcept;
    [[nodiscard]] bool canFirst() const noexcept;
    [[nodiscard]] bool canPrevious() const noexcept;
    [[nodiscard]] bool canNext() const noexcept;
    [[nodiscard]] bool canLast() const noexcept;
    [[nodiscard]] bool canPlay() const noexcept;
    [[nodiscard]] bool canPause() const noexcept;

    Q_INVOKABLE bool openComparison(const QUrl& first, const QUrl& second);
    Q_INVOKABLE bool first();
    Q_INVOKABLE bool previous();
    Q_INVOKABLE bool next();
    Q_INVOKABLE bool last();
    Q_INVOKABLE bool stepFrames(qint64 delta);
    Q_INVOKABLE bool seekFrame(qint64 frame);
    Q_INVOKABLE bool play();
    Q_INVOKABLE bool pause();
    Q_INVOKABLE bool togglePlayback();

    // Stops timer/backend access and makes every command fail closed. Calls from another thread
    // are queued to the controller's GUI thread; the runtime calls this before closing ingress.
    Q_INVOKABLE void stop() noexcept;

Q_SIGNALS:
    void stateChanged();

private:
    class Impl;

    std::unique_ptr<Impl> impl_;
};

} // namespace dvs::ui
