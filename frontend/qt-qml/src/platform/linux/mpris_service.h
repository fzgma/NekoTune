#pragma once
#include "platform/system_media_bridge.h"
#include <QDBusConnection>
#include <QDBusVirtualObject>
#include <QTimer>

class MprisService final : public QDBusVirtualObject {
    Q_OBJECT
  public:
    explicit MprisService(SystemMediaBridge &bridge, QObject *parent = nullptr);
    ~MprisService() override;
    QString serviceName() const { return m_serviceName; }
    QString introspect(const QString &path) const override;
    bool handleMessage(const QDBusMessage &message, const QDBusConnection &connection) override;
    void shutdown();
  private:
    SystemMediaBridge &m_bridge;
    QString m_connectionName;
    QDBusConnection m_bus;
    QString m_serviceName;
    QVariantMap m_published;
    QTimer m_publishTimer;
    void updateRegistration();
    void publishChanges();
    QVariantMap properties(const QString &interface) const;
    QVariantMap metadata() const;
    void dispatch(const QDBusMessage &message, const QDBusConnection &connection,
                  const QString &method, const QJsonObject &params = {});
};
