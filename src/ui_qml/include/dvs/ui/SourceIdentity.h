#pragma once

#include <QString>
#include <QUrl>

#include <cstdint>

namespace dvs::ui {

// Produces the UI-facing identity used to bind asynchronous source operations to one immutable
// local-file revision. Keep this in the UI adapter so controller projections and shell actions
// resolve exactly the same canonical path, size, and modification timestamp.
[[nodiscard]] QString canonicalSourceIdentity(const QUrl& source);

// Composes the same identity string from caller-supplied fields. Path normalization (canonical
// then absolute fallback, cleanPath, case fold) matches canonicalSourceIdentity exactly; the
// byte size and modification timestamp are injected rather than re-read from disk so that a
// snapshot-frozen descriptor produces a stable identity across projections.
[[nodiscard]] QString composeSourceIdentity(const QString& path,
                                            std::int64_t byteSize,
                                            std::int64_t modifiedMs);

} // namespace dvs::ui
