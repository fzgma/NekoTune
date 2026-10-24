#pragma once
#include <QStringList>
#include <utility>

namespace nekotune {
/// Extensions loaded by the previous session and its consecutive unattributed abnormal exits.
class SessionMarker {
  public:
    explicit SessionMarker(QString path) : m_path(std::move(path)) {}
    struct Previous {
        bool ended = true;
        QStringList loaded;
        int unattributed = 0;
    };
    Previous begin(bool attributed);
    void loaded(const QString &extensionId);
    void end();

  private:
    void save() const;
    QString m_path;
    QStringList m_loaded;
    int m_unattributed = 0;
};
} // namespace nekotune
