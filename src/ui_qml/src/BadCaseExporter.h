#pragma once

#include "dvs/application/SessionSnapshot.h"

#include <QImage>
#include <QString>

namespace dvs::ui::internal {

struct BadCaseExportResult final {
    QString folder;
    QString error;

    [[nodiscard]] bool succeeded() const noexcept {
        return !folder.isEmpty() && error.isEmpty();
    }
};

[[nodiscard]] BadCaseExportResult exportBadCaseEvidence(
    const QImage& image, const application::SessionSnapshot& evidence, const QString& parentPath);

} // namespace dvs::ui::internal
