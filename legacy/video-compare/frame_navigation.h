#pragma once

#include <algorithm>
#include <cstddef>

struct FrameNavigationPlan {
  int frame_offset{0};
  int decode_forward{0};
  int seek_backward{0};
};

// Frames are stored newest-first. frame_offset therefore also describes how
// many already-decoded frames are available in front of the displayed frame.
inline FrameNavigationPlan plan_frame_navigation(const int frame_offset, const size_t cached_frames, const int delta) {
  FrameNavigationPlan plan;
  plan.frame_offset = std::max(0, frame_offset);

  const int last_cached_offset = cached_frames == 0 ? -1 : static_cast<int>(cached_frames - 1);

  if (delta > 0) {
    const int cached_forward = std::min(delta, plan.frame_offset);
    plan.frame_offset -= cached_forward;
    plan.decode_forward = delta - cached_forward;
  } else if (delta < 0) {
    const int requested_backward = -delta;
    const int cached_backward = std::max(0, last_cached_offset - plan.frame_offset);
    const int move_in_cache = std::min(requested_backward, cached_backward);
    plan.frame_offset += move_in_cache;
    plan.seek_backward = requested_backward - move_in_cache;
  }

  return plan;
}

inline int frame_prefetch_needed(const int frame_offset, const size_t radius) {
  return std::max(0, static_cast<int>(radius) - std::max(0, frame_offset));
}
