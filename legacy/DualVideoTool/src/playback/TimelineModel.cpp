#include "playback/TimelineModel.h"
#include <algorithm>

TimelineModel::TimelineModel(QObject* parent) : QObject(parent) {}

void TimelineModel::setTotalFrames(int64_t totalFrames) {
    totalFrames_ = totalFrames;
}

int64_t TimelineModel::totalFrames() const {
    return totalFrames_;
}

void TimelineModel::setClipStart(int64_t frame) {
    clipStart_ = std::clamp(frame, static_cast<int64_t>(0), totalFrames_ - 1);
    if (clipEnd_ && clipStart_ > clipEnd_) {
        clipEnd_ = clipStart_;
    }
    emit clipChanged();
}

void TimelineModel::setClipEnd(int64_t frame) {
    clipEnd_ = std::clamp(frame, static_cast<int64_t>(0), totalFrames_ - 1);
    if (clipStart_ && clipEnd_ < clipStart_) {
        clipStart_ = clipEnd_;
    }
    emit clipChanged();
}

void TimelineModel::clearClipStart() {
    clipStart_ = std::nullopt;
    emit clipChanged();
}

void TimelineModel::clearClipEnd() {
    clipEnd_ = std::nullopt;
    emit clipChanged();
}

void TimelineModel::clearClip() {
    clipStart_ = std::nullopt;
    clipEnd_ = std::nullopt;
    emit clipChanged();
}

std::optional<int64_t> TimelineModel::clipStart() const {
    return clipStart_;
}

std::optional<int64_t> TimelineModel::clipEnd() const {
    return clipEnd_;
}

bool TimelineModel::hasClip() const {
    return clipStart_.has_value() && clipEnd_.has_value();
}
