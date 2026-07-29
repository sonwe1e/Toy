#include "SourceFrameCache.h"

#include <utility>

namespace dvs::media::internal {

SourceFrameCache::SourceFrameCache(const std::size_t capacityBytes) noexcept
    : capacityBytes_(capacityBytes) {}

std::optional<CachedSourceFrame> SourceFrameCache::find(const SourceFrameCacheKey& key) {
    const EntryList::iterator entry = findEntry(key);
    if (entry == entries_.end()) {
        return std::nullopt;
    }
    entries_.splice(entries_.begin(), entries_, entry);
    return entries_.front().frame;
}

void SourceFrameCache::insert(SourceFrameCacheKey key, CachedSourceFrame frame) {
    const std::size_t bytes = frame.handle.accountedBytes();
    if (capacityBytes_ == 0U || bytes == 0U || bytes > capacityBytes_) {
        return;
    }

    const EntryList::iterator existing = findEntry(key);
    if (existing != entries_.end()) {
        retainedBytes_ -= existing->bytes;
        entries_.erase(existing);
    }
    evictToFit(bytes);
    entries_.push_front(Entry{
        .key = std::move(key),
        .frame = std::move(frame),
        .bytes = bytes,
    });
    retainedBytes_ += bytes;
}

void SourceFrameCache::clear() noexcept {
    entries_.clear();
    retainedBytes_ = 0U;
}

std::size_t SourceFrameCache::capacityBytes() const noexcept {
    return capacityBytes_;
}

std::size_t SourceFrameCache::retainedBytes() const noexcept {
    return retainedBytes_;
}

std::size_t SourceFrameCache::entryCount() const noexcept {
    return entries_.size();
}

SourceFrameCache::EntryList::iterator SourceFrameCache::findEntry(const SourceFrameCacheKey& key) {
    for (EntryList::iterator entry = entries_.begin(); entry != entries_.end(); ++entry) {
        if (entry->key == key) {
            return entry;
        }
    }
    return entries_.end();
}

void SourceFrameCache::evictToFit(const std::size_t incomingBytes) noexcept {
    while (!entries_.empty() && retainedBytes_ + incomingBytes > capacityBytes_) {
        retainedBytes_ -= entries_.back().bytes;
        entries_.pop_back();
    }
}

} // namespace dvs::media::internal
