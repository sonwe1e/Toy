#include "dvs/platform/TraceSink.h"

#include <cstdint>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <string>
#include <vector>

namespace dvs::platform {

namespace {

// Encodes a single trace event as one JSON object on one line (JSONL). Kept dependency-free:
// no JSON library, no allocator on the hot path beyond the reused `buffer`. Field order matches
// trace-schema.md. Command and request are emitted as their integer values; a nullopt command
// is emitted as null.
void appendEventJson(void* file, const application::TraceEvent& event) {
    if (file == nullptr) {
        return;
    }
    auto* handle = static_cast<std::FILE*>(file);
    char buffer[256];
    const auto command = event.identity.command;
    // snprintf returns the would-be length (excluding the terminating NUL). Clamp to the buffer
    // size so a truncated line never reads past the array; the line is simply dropped to keep
    // the export well-formed rather than emitting a partial JSON object.
    const int written = std::snprintf(
        buffer,
        sizeof(buffer),
        R"({"t":%llu,"kind":%u,"s":%llu,"e":%llu,"topo":%llu,"tl":%llu,"al":%llu,"gen":%llu,"dev":%llu,"req":%llu,"cmd":%s,"p":%llu})"
        "\n",
        static_cast<unsigned long long>(event.timestampMicroseconds),
        static_cast<unsigned>(event.kind),
        static_cast<unsigned long long>(event.identity.session.value()),
        static_cast<unsigned long long>(event.identity.epoch.value()),
        static_cast<unsigned long long>(event.identity.topology.value()),
        static_cast<unsigned long long>(event.identity.timeline.value()),
        static_cast<unsigned long long>(event.identity.alignment.value()),
        static_cast<unsigned long long>(event.identity.generation.value()),
        static_cast<unsigned long long>(event.identity.device.value()),
        static_cast<unsigned long long>(event.identity.request.value()),
        command.has_value() ? std::to_string(command->value()).c_str() : "null",
        static_cast<unsigned long long>(event.payload));
    if (written > 0 && written < static_cast<int>(sizeof(buffer))) {
        std::fwrite(buffer, 1, static_cast<std::size_t>(written), handle);
    }
}

} // namespace

MemoryTraceSink::MemoryTraceSink(const std::size_t capacity) : capacity_(capacity) {
    events_.reserve(capacity);
}

void MemoryTraceSink::append(const application::TraceEvent& event) {
    if (events_.size() < capacity_) {
        events_.push_back(event);
    }
}

void MemoryTraceSink::recordOverflow(const std::uint64_t lostCount) {
    overflow_ += lostCount;
}

std::uint64_t MemoryTraceSink::overflowCount() const noexcept {
    return overflow_;
}

const std::vector<application::TraceEvent>& MemoryTraceSink::events() const noexcept {
    return events_;
}

void MemoryTraceSink::clear() noexcept {
    events_.clear();
    overflow_ = 0U;
}

FileTraceSink::FileTraceSink(std::filesystem::path path) : path_(std::move(path)) {}

FileTraceSink::~FileTraceSink() {
    if (file_ != nullptr) {
        std::fclose(static_cast<std::FILE*>(file_));
        file_ = nullptr;
    }
}

void FileTraceSink::writeHeader() noexcept {
    if (headerWritten_ || file_ == nullptr) {
        return;
    }
    auto* handle = static_cast<std::FILE*>(file_);
    // 19 bytes: the literal has 19 characters; writing 20 would emit the terminating NUL.
    static constexpr char kHeader[] = "{\"traceVersion\":1}\n";
    std::fwrite(kHeader, 1, sizeof(kHeader) - 1U, handle);
    headerWritten_ = true;
}

void FileTraceSink::append(const application::TraceEvent& event) {
    if (file_ == nullptr) {
        if (path_.has_parent_path()) {
            std::error_code ec;
            std::filesystem::create_directories(path_.parent_path(), ec);
        }
        std::FILE* handle = nullptr;
        if (fopen_s(&handle, path_.string().c_str(), "wb") != 0) {
            return;
        }
        file_ = handle;
    }
    writeHeader();
    appendEventJson(file_, event);
}

void FileTraceSink::recordOverflow(const std::uint64_t lostCount) {
    overflow_ += lostCount;
}

std::uint64_t FileTraceSink::overflowCount() const noexcept {
    return overflow_;
}

const std::filesystem::path& FileTraceSink::path() const noexcept {
    return path_;
}

} // namespace dvs::platform
