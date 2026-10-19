#pragma once

#include "wallpaper/wallpaper_platform.h"
#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QVariantMap>
#include <QWindow>
#include <map>

class QQmlEngine;
class QScreen;
class IpcClient;

// Owns presentation only. Never sends player commands or owns playback state.
// Construct after the QML engine; shutdown destroys all windows synchronously.
class WallpaperManager final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool requestedEnabled READ requestedEnabled NOTIFY changed)
    Q_PROPERTY(bool supported READ supported NOTIFY changed)
    Q_PROPERTY(QString unavailableReason READ unavailableReason NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(QVariantMap settings READ settings NOTIFY changed)
    Q_PROPERTY(QVariantList outputs READ outputs NOTIFY changed)
    Q_PROPERTY(int runningCount READ runningCount NOTIFY changed)
    Q_PROPERTY(bool dataReady READ dataReady NOTIFY dataReadyChanged)
    Q_PROPERTY(bool linuxOverlay READ linuxOverlay CONSTANT)
    Q_PROPERTY(QVariantMap audioAnalysis READ audioAnalysis CONSTANT)
  public:
    explicit WallpaperManager(QObject *parent = nullptr, std::unique_ptr<WallpaperPlatform> platform = {});
    ~WallpaperManager() override;
    void initialize(QQmlEngine *engine, IpcClient *client);
    bool requestedEnabled() const { return m_requested; }
    bool supported() const { return m_reason.isEmpty(); }
    QString unavailableReason() const { return m_reason; }
    QString error() const { return m_error; }
    QVariantMap settings() const { return m_settings; }
    QVariantList outputs() const;
    int runningCount() const;
    bool dataReady() const { return m_dataReady; }
    bool linuxOverlay() const;
    QVariantMap audioAnalysis() const;
    Q_INVOKABLE bool setOption(const QString &key, const QVariant &value);
    Q_INVOKABLE void retry();
    void shutdown();
  signals:
    void changed();
    void dataReadyChanged();

  protected:
    bool eventFilter(QObject *object, QEvent *event) override;

  private:
    struct Output {
        QPointer<QScreen> screen;
        QPointer<QWindow> window;
        QString state = "idle";
        QString error;
        qint64 deadline = 0, nextAttempt = 0, recoveryDeadline = 0;
        int attempts = 0;
    };
    QPointer<QQmlEngine> m_engine;
    std::unique_ptr<WallpaperPlatform> m_platform;
    std::map<QScreen *, Output> m_outputs;
    QVariantMap m_settings;
    QTimer m_health;
    QElapsedTimer m_clock;
    QString m_reason, m_error;
    bool m_requested = false, m_stopping = false, m_dataReady = false;
    void addScreen(QScreen *screen);
    bool selected(QScreen *screen) const;
    static QString screenId(QScreen *screen);
    void reconcile();
    void start(Output &output);
    void destroy(Output &output);
    void fail(Output &output, const QString &error);
    void checkOutputs();
    void updateTimer();
};
