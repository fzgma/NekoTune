#pragma once
#include <QObject>
#include <QTimer>
#include <memory>

class QFileSystemWatcher;

namespace nekotune {
/// Watches a development directory (non-recursive Qt watcher, so every entry is added) and emits
/// changed() once edits settle.
class DevWatcher final : public QObject {
    Q_OBJECT
  public:
    explicit DevWatcher(const QString &directory, QObject *parent = nullptr);
    ~DevWatcher() override;
  signals:
    void changed();

  private:
    std::unique_ptr<QFileSystemWatcher> m_watcher;
    QTimer m_debounce;
};
} // namespace nekotune
