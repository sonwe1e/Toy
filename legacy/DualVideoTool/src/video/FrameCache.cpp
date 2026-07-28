#include "video/FrameCache.h"

FrameCache::FrameCache(int capacity) : capacity_(capacity) {}

bool FrameCache::contains(int64_t frameIndex) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return cache_.count(frameIndex) > 0;
}

std::optional<VideoFrame> FrameCache::get(int64_t frameIndex) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = cache_.find(frameIndex);
    if (it != cache_.end()) {
        return it->second;
    }
    return std::nullopt;
}

void FrameCache::insert(int64_t frameIndex, const VideoFrame& frame) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (static_cast<int>(cache_.size()) >= capacity_ && cache_.count(frameIndex) == 0) {
        // Evict the entry farthest from the most recently inserted frame
        auto farthest = cache_.begin();
        int64_t maxDist = 0;
        for (auto it = cache_.begin(); it != cache_.end(); ++it) {
            int64_t dist = std::abs(it->first - frameIndex);
            if (dist > maxDist) {
                maxDist = dist;
                farthest = it;
            }
        }
        cache_.erase(farthest);
    }
    cache_[frameIndex] = frame;
}

void FrameCache::pruneAround(int64_t centerFrame, int backwardRadius, int forwardRadius) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto it = cache_.begin(); it != cache_.end(); ) {
        int64_t delta = it->first - centerFrame;
        if (delta < -backwardRadius || delta > forwardRadius) {
            it = cache_.erase(it);
        } else {
            ++it;
        }
    }
}

void FrameCache::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    cache_.clear();
}

size_t FrameCache::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return cache_.size();
}
