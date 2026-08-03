#include "dvs/ui/SourceIdentity.h"

#include <QDir>
#include <QFileInfo>

namespace dvs::ui {

QString canonicalSourceIdentity(const QUrl& source) {
    if (!source.isLocalFile()) {
        return source.toString(QUrl::FullyEncoded).toCaseFolded();
    }

    const QFileInfo file{source.toLocalFile()};
    QString path = file.canonicalFilePath();
    if (path.isEmpty()) {
        path = file.absoluteFilePath();
    }
    return QStringLiteral("%1|%2|%3")
        .arg(QDir::cleanPath(path).toCaseFolded())
        .arg(file.exists() ? file.size() : -1)
        .arg(file.exists() ? file.lastModified().toMSecsSinceEpoch() : -1);
}

} // namespace dvs::ui
