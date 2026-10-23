#include "infrastructure/extensions/runtime/session_marker.h"
#include "infrastructure/extensions/package/json_file.h"
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <limits>

namespace nekotune {
SessionMarker::Previous SessionMarker::begin(bool attributed) {
    Previous previous;
    if (QFileInfo::exists(m_path)) {
        previous.ended = false;
        const auto record = readJsonFile(m_path, QVariantMap{}).toMap();
        previous.loaded = record.value("loaded").toStringList();
        if (!attributed && !previous.loaded.isEmpty()) {
            const auto count = qBound(0, record.value("unattributed").toInt(), std::numeric_limits<int>::max() - 1);
            previous.unattributed = count + 1;
        }
    }
    m_loaded.clear();
    m_unattributed = previous.unattributed;
    save();
    return previous;
}
void SessionMarker::loaded(const QString &extensionId) {
    if (m_loaded.contains(extensionId))
        return;
    m_loaded.append(extensionId);
    save();
}
void SessionMarker::end() {
    QFile::remove(m_path);
    m_loaded.clear();
    m_unattributed = 0;
}
void SessionMarker::save() const {
    const auto saved = writeJsonFile(m_path, QVariantMap{{"loaded", m_loaded}, {"unattributed", m_unattributed}});
    if (!saved)
        qWarning("Cannot persist extension session marker: %s", qPrintable(saved.error().message));
}
} // namespace nekotune
