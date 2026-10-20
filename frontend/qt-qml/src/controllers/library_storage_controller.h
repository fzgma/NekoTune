#pragma once
#include "controllers/feature_controller.h"
class LibraryStorageController final : public FeatureController {
    Q_OBJECT
    Q_PROPERTY(QVariantMap status READ status NOTIFY changed)
    Q_PROPERTY(QVariantMap task READ task NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString message READ message NOTIFY changed)
  public:
    explicit LibraryStorageController(IpcClient &client);
    QVariantMap status() const { return m_status.toVariantMap(); }
    QVariantMap task() const { return m_task.toVariantMap(); }
    bool busy() const { return m_pending || m_task["state"] == "running" || m_task["state"] == "switching"; }
    QString message() const { return m_message; }
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void migrate();
    Q_INVOKABLE void verify();
    Q_INVOKABLE void backup(const QString &directory);
    Q_INVOKABLE void previewRestore(const QString &directory);
    Q_INVOKABLE void restore();
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void exportSongs(const QVariantList &ids, const QString &directory);
    Q_INVOKABLE void exportPlaylist(int id, const QString &directory);
    Q_INVOKABLE void removeRollback(const QString &id);
  signals:
    void changed();
    void restorePreviewReady(const QVariantMap &preview);

  private:
    void start(const QString &method, const QJsonObject &params = {});
    void receive(const QJsonObject &task);
    QJsonObject m_status, m_task;
    QString m_message, m_preview;
    bool m_pending = false;
};
