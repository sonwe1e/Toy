#pragma once

namespace dvs::media::testing {

// This private seam gives component tests deterministic worker admission without adding a test
// control to MediaProbe's installed adapter interface.
using WorkerAdmissionHook = void (*)(void*) noexcept;

class ScopedMediaProbeWorkerAdmissionHook final {
public:
    ScopedMediaProbeWorkerAdmissionHook(WorkerAdmissionHook hook, void* context) noexcept;
    ~ScopedMediaProbeWorkerAdmissionHook();

    ScopedMediaProbeWorkerAdmissionHook(const ScopedMediaProbeWorkerAdmissionHook&) = delete;
    ScopedMediaProbeWorkerAdmissionHook&
    operator=(const ScopedMediaProbeWorkerAdmissionHook&) = delete;

private:
    WorkerAdmissionHook previousHook_ = nullptr;
    void* previousContext_ = nullptr;
};

} // namespace dvs::media::testing
