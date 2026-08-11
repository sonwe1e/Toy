#pragma once

#include "dvs/application/PlaybackTrace.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace dvs::platform {

// Default in-memory trace sink used when no file export is requested. Accumulates events in a
// vector bounded by `capacity`; once full it stops appending (events are still counted by the
// buffer's overflow counter) so a diagnostic capture cannot grow without bound. Tests use this
// to assert on the recorded trace without touching the filesystem.
class MemoryTraceSink final : public application::ITraceSink {
public:
    explicit MemoryTraceSink(std::size_t capacity = 4096U);

    void append(const application::TraceEvent& event) override;
    void recordOverflow(std::uint64_t lostCount) override;

    [[nodiscard]] std::uint64_t overflowCount() const noexcept;
    [[nodiscard]] const std::vector<application::TraceEvent>& events() const noexcept;
    void clear() noexcept;

private:
    std::vector<application::TraceEvent> events_;
    std::size_t capacity_;
    std::uint64_t overflow_ = 0U;
};

// File-backed trace sink. Appends one JSON object per line (JSONL) so a trace can be streamed
// incrementally without buffering the whole run in memory, and so export survives a crash. The
// file is opened on the first append and closed on destruction; all calls must come from a
// single thread (the export thread), never the coordinator worker.
class FileTraceSink final : public application::ITraceSink {
public:
    explicit FileTraceSink(std::filesystem::path path);
    ~FileTraceSink() override;

    FileTraceSink(const FileTraceSink&) = delete;
    FileTraceSink& operator=(const FileTraceSink&) = delete;

    void append(const application::TraceEvent& event) override;
    void recordOverflow(std::uint64_t lostCount) override;

    [[nodiscard]] std::uint64_t overflowCount() const noexcept;
    [[nodiscard]] const std::filesystem::path& path() const noexcept;

private:
    void writeHeader() noexcept;

    std::filesystem::path path_;
    void* file_ =
        nullptr; // owned FILE*; kept opaque here to avoid pulling <cstdio> into the header
    std::uint64_t overflow_ = 0U;
    bool headerWritten_ = false;
};

} // namespace dvs::platform
