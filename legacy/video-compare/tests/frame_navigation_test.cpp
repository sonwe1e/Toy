#include <cassert>
#include "../frame_navigation.h"

int main() {
  {
    const auto plan = plan_frame_navigation(5, 11, 1);
    assert(plan.frame_offset == 4);
    assert(plan.decode_forward == 0);
    assert(plan.seek_backward == 0);
  }
  {
    const auto plan = plan_frame_navigation(0, 11, 1);
    assert(plan.frame_offset == 0);
    assert(plan.decode_forward == 1);
  }
  {
    const auto plan = plan_frame_navigation(5, 11, -1);
    assert(plan.frame_offset == 6);
    assert(plan.seek_backward == 0);
  }
  {
    const auto plan = plan_frame_navigation(10, 11, -1);
    assert(plan.frame_offset == 10);
    assert(plan.seek_backward == 1);
  }
  {
    const auto plan = plan_frame_navigation(2, 5, -4);
    assert(plan.frame_offset == 4);
    assert(plan.seek_backward == 2);
  }

  assert(frame_prefetch_needed(0, 5) == 5);
  assert(frame_prefetch_needed(3, 5) == 2);
  assert(frame_prefetch_needed(5, 5) == 0);
  assert(frame_prefetch_needed(8, 5) == 0);

  return 0;
}
