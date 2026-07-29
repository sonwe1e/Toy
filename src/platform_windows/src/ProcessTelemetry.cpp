#include "dvs/platform/ProcessTelemetry.h"

#include "WindowsApi.h"

#include <Psapi.h>
#include <TlHelp32.h>

namespace dvs::platform {
namespace {

[[nodiscard]] std::size_t currentProcessThreadCount() noexcept {
    const DWORD processId = GetCurrentProcessId();
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0U);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return 0U;
    }

    THREADENTRY32 entry{};
    entry.dwSize = sizeof(THREADENTRY32);
    std::size_t count = 0U;
    if (Thread32First(snapshot, &entry) != FALSE) {
        do {
            if (entry.th32OwnerProcessID == processId) {
                ++count;
            }
        } while (Thread32Next(snapshot, &entry) != FALSE);
    }
    CloseHandle(snapshot);
    return count;
}

[[nodiscard]] std::size_t currentProcessWorkingSetBytes() noexcept {
    PROCESS_MEMORY_COUNTERS counters{};
    counters.cb = sizeof(PROCESS_MEMORY_COUNTERS);
    if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)) == FALSE) {
        return 0U;
    }
    return counters.WorkingSetSize;
}

} // namespace

ProcessTelemetry sampleCurrentProcessTelemetry() noexcept {
    return ProcessTelemetry{
        .threadCount = currentProcessThreadCount(),
        .workingSetBytes = currentProcessWorkingSetBytes(),
    };
}

} // namespace dvs::platform
