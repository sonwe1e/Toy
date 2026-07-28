#pragma once

#include <QObject>
#include <cstdint>
#include <optional>

class TimelineModel : public QObject {
    Q_OBJECT

public:
    explicit TimelineModel(QObject* parent = nullptr);

    void setTotalFrames(int64_t totalFrames);
    int64_t totalFrames() const;

    void setClipStart(int64_t frame);
    void setClipEnd(int64_t frame);
    void clearClipStart();
    void clearClipEnd();
    void clearClip();

    std::optional<int64_t> clipStart() const;
    std::optional<int64_t> clipEnd() const;
    bool hasClip() const;

signals:
    void clipChanged();

private:
    int64_t totalFrames_ = 0;
    std::optional<int64_t> clipStart_;
    std::optional<int64_t> clipEnd_;
};
