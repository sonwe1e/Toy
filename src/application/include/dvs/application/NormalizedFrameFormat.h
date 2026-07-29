#pragma once

namespace dvs::application {

// Adapter-neutral normalized storage profiles. Concrete CPU/GPU resources remain private to
// their adapters; this value only describes the sample layout carried between those adapters.
enum class NormalizedFrameFormat {
    Nv12_8,
    P010_10,
    Bgra8,
};

} // namespace dvs::application
