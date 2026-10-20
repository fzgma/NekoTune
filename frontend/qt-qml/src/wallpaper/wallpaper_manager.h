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
class AppControllers;
class WallpaperManager;

// C++ values are converted in the consuming engine. Do not share JavaScript
// objects or QML var bindings between the host and isolated theme engines.
class WallpaperContext final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap song READ song NOTIFY songChanged)
    Q_PROPERTY(QVariantMap lyrics READ lyrics NOTIFY lyricsChanged)
    Q_PROPERTY(double position READ position NOTIFY positionChanged)
    Q_PROPERTY(double duration READ duration NOTIFY durationChanged)
    Q_PROPERTY(double playbackRate READ playbackRate NOTIFY playbackRateChanged)
    Q_PROPERTY(bool connected READ connected NOTIFY playbackStateChanged)
    Q_PROPERTY(bool hasSong READ hasSong NOTIFY playbackStateChanged)
    Q_PROPERTY(bool playing READ playing NOTIFY playbackStateChanged)
    Q_PROPERTY(bool updating READ updating NOTIFY playbackStateChanged)
    Q_PROPERTY(QVariantMap preferences READ preferences NOTIFY preferencesChanged)
    Q_PROPERTY(QObject *screen READ screen NOTIFY screenChanged)
  public:
    WallpaperContext(WallpaperManager *manager, AppControllers *app, IpcClient *client);
    void setWindow(QWindow *window);
    QVariantMap song() const;
    QVariantMap lyrics() const;
    double position() const;
    double duration() const;
    double playbackRate() const;
    bool connected() const;
    bool hasSong() const;
    bool playing() const;
    bool updating() const;
    QVariantMap preferences() const;
    QObject *screen() const;
  signals:
    void songChanged();
    void lyricsChanged();
    void positionChanged();
    void durationChanged();
    void playbackRateChanged();
    void playbackStateChanged();
    void preferencesChanged();
    void screenChanged();
  private:
    WallpaperManager *m_manager;
    AppControllers *m_app;
    IpcClient *m_client;
    QPointer<QWindow> m_window;
};

// Owns presentation only. Never sends player commands or owns playback state.
// Construct after the QML engine; shutdown destroys all windows synchronously.
class WallpaperManager final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool requestedEnabled READ requestedEnabled NOTIFY changed)
    Q_PROPERTY(bool supported READ supported NOTIFY changed)
    Q_PROPERTY(QString unavailableReason READ unavailableReason NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(QVariantMap settings READ settings NOTIFY changed)
    Q_PROPERTY(QVariantList outputs READ outputs NOTIFY outputsChanged)
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
    void outputsChanged();
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
