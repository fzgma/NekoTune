#pragma once
#include <QPointer>
#include <QQmlEngine>
#include <QQuickItem>
#include <memory>
class QQmlContext;
class ExtensionView : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(QVariantMap descriptor READ descriptor WRITE setDescriptor NOTIFY descriptorChanged)
    Q_PROPERTY(QObject *controllers MEMBER controllers NOTIFY contextChanged)
    Q_PROPERTY(QObject *translator MEMBER translator NOTIFY contextChanged)
    Q_PROPERTY(QObject *hostWindow MEMBER hostWindow NOTIFY contextChanged)
    Q_PROPERTY(QObject *wallpaperContext MEMBER wallpaperContext NOTIFY contextChanged)
    Q_PROPERTY(bool ready READ ready NOTIFY readyChanged)
    Q_PROPERTY(QString error READ error NOTIFY readyChanged)
    Q_PROPERTY(QQuickItem *item READ item NOTIFY readyChanged)
  public:
    explicit ExtensionView(QQuickItem *parent = nullptr);
    ~ExtensionView() override;
    QVariantMap descriptor() const { return m_descriptor; }
    void setDescriptor(const QVariantMap &descriptor);
    bool ready() const { return m_item; }
    bool loading() const { return m_scheduled; }
    QString error() const { return m_error; }
    QQuickItem *item() const { return m_item; }
    Q_INVOKABLE bool invokeAction(const QString &method);
    QObject *controllers = nullptr;
    QObject *translator = nullptr;
    QObject *hostWindow = nullptr;
    QObject *wallpaperContext = nullptr;
  signals:
    void descriptorChanged();
    void contextChanged();
    void readyChanged();

  protected:
    void componentComplete() override;
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;

  private:
    void schedule();
    void rebuild();
    void updateContext();
    QVariantMap m_descriptor;
    QVariantMap m_loadedDescriptor;
    std::shared_ptr<QQmlEngine> m_engine;
    std::unique_ptr<QQmlContext> m_context;
    QPointer<QQuickItem> m_item;
    QString m_error;
    bool m_scheduled = false;
};
void registerExtensionQmlTypes();
