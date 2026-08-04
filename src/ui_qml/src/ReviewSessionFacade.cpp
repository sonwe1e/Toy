#include "dvs/ui/ReviewSessionFacade.h"

#include "dvs/ui/ReviewController.h"
#include "dvs/ui/ReviewPreferencesController.h"
#include "dvs/ui/ReviewShellController.h"

namespace dvs::ui {

ReviewSessionFacade::ReviewSessionFacade(ReviewController& review,
                                         ReviewPreferencesController& preferences,
                                         ReviewShellController& shell,
                                         QObject* const parent)
    : QObject(parent), review_(review), preferences_(preferences), shell_(shell) {}

ReviewShellController* ReviewSessionFacade::session() const noexcept {
    return &shell_;
}

ReviewController* ReviewSessionFacade::playback() const noexcept {
    return &review_;
}

ReviewController* ReviewSessionFacade::alignment() const noexcept {
    return &review_;
}

ReviewPreferencesController* ReviewSessionFacade::comparison() const noexcept {
    return &preferences_;
}

ReviewController* ReviewSessionFacade::notifications() const noexcept {
    return &review_;
}

ReviewShellController* ReviewSessionFacade::shell() const noexcept {
    return &shell_;
}

} // namespace dvs::ui
