#pragma once

#include "dvs/application/Events.h"
#include "dvs/application/SessionSnapshot.h"

#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <vector>

namespace dvs::application {

// Thread-safe publication boundary shared by the coordinator worker and UI readers. It owns no
// workflow state and never invokes callbacks while holding its lock.
class CoordinatorPublication final {
public:
    [[nodiscard]] std::shared_ptr<const SessionSnapshot> snapshot() const;
    [[nodiscard]] std::vector<CommandTerminal> takeCompletedCommands();
    [[nodiscard]] std::shared_ptr<const std::vector<SequenceAlignmentResult>>
    acceptedSequenceAlignments() const;

    void publish(const SessionSnapshot& state,
                 const std::vector<SequenceAlignmentResult>& sequenceAlignmentMaps);
    void complete(CommandTerminal terminal);

private:
    mutable std::mutex mutex_;
    std::shared_ptr<const SessionSnapshot> snapshot_;
    std::shared_ptr<const std::vector<SequenceAlignmentResult>> sequenceAlignmentMaps_;
    std::uint64_t sequenceAlignmentRevision_ = std::numeric_limits<std::uint64_t>::max();
    std::vector<CommandTerminal> completedCommands_;
};

} // namespace dvs::application
