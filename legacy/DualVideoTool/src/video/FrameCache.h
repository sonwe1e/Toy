#pragma once

#include "video/VideoFrame.h"
#include <unordered_map>
#include <mutex>
#include <optional>

class FrameCache {
public:
    explicit FrameCache(int capacity = 240);

    bool contains(int64_t frameIndex) const;
    std::optional<VideoFrame> get(int64_t frameIndex) const;
    void insert(int64_t frameIndex, const VideoFrame& frame);
    void pruneAround(int64_t centerFrame, int backwardRadius, int forwardRadius);
    void clear();
    size_t size() const;

private:
    mutable std::mutex mutex_;
    std::unordered_map<int64_t, VideoFrame> cache_;
    int capacity_;
};
