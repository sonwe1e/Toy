#pragma once

#include "dvs/ui/ReviewController.h"
#include "dvs/ui/ReviewPreferencesController.h"
#include "dvs/ui/ReviewShellController.h"

#include <QObject>

namespace dvs::ui {

// Stable composition object for QML. During the gradual migration its capability properties
// point at the existing controllers; each property can be replaced by a narrow ViewModel without
// changing the root context contract again.
class ReviewSessionFacade final : public QObject {
    Q_OBJECT

    Q_PROPERTY(ReviewShellController* session READ session CONSTANT)
    Q_PROPERTY(ReviewController* playback READ playback CONSTANT)
    Q_PROPERTY(ReviewController* alignment READ alignment CONSTANT)
    Q_PROPERTY(ReviewPreferencesController* comparison READ comparison CONSTANT)
    Q_PROPERTY(ReviewController* notifications READ notifications CONSTANT)
    Q_PROPERTY(ReviewShellController* shell READ shell CONSTANT)

public:
    ReviewSessionFacade(ReviewController& review,
                        ReviewPreferencesController& preferences,
                        ReviewShellController& shell,
                        QObject* parent = nullptr);

    [[nodiscard]] ReviewShellController* session() const noexcept;
    [[nodiscard]] ReviewController* playback() const noexcept;
    [[nodiscard]] ReviewController* alignment() const noexcept;
    [[nodiscard]] ReviewPreferencesController* comparison() const noexcept;
    [[nodiscard]] ReviewController* notifications() const noexcept;
    [[nodiscard]] ReviewShellController* shell() const noexcept;

private:
    ReviewController& review_;
    ReviewPreferencesController& preferences_;
    ReviewShellController& shell_;
};

} // namespace dvs::ui
