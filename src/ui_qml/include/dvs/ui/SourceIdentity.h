#pragma once

#include <QString>
#include <QUrl>

namespace dvs::ui {

// Produces the UI-facing identity used to bind asynchronous source operations to one immutable
// local-file revision. Keep this in the UI adapter so controller projections and shell actions
// resolve exactly the same canonical path, size, and modification timestamp.
[[nodiscard]] QString canonicalSourceIdentity(const QUrl& source);

} // namespace dvs::ui
