#pragma once

#include "dvs/ui/ReviewShellController.h"

namespace dvs::ui {

// Public name for the session-facing API consumed by QML and desktop automation. The existing
// controller type remains as a source-compatible implementation detail while downstream code
// migrates away from the old shell terminology.
using ReviewSessionFacade = ReviewShellController;

} // namespace dvs::ui
