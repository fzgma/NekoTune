#pragma once
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QWindow>
#include <functional>
#include <memory>

class QAction;
class QMenu;
class QSystemTrayIcon;
class I18n;
class SystemMediaBridge;

// Owns desktop window lifetime; playback remains behind the shared IPC bridge.
class SystemTrayController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool trayAvailable READ trayAvailable NOTIFY trayAvailableChanged)
    Q_PROPERTY(bool closeToTray READ closeToTray WRITE setCloseToTray NOTIFY closeToTrayChanged)
  public:
    using AvailabilityProbe = std::function<bool()>;
    SystemTrayController(SystemMediaBridge &media, I18n &translator, QObject *parent = nullptr,
                         AvailabilityProbe availability = {});
    ~SystemTrayController() override;
    bool trayAvailable() const { return m_available; }
    bool closeToTray() const { return m_closeToTray; }
    void setCloseToTray(bool value);
    void attachWindow(QWindow *window);
    void showWindow();
    void toggleWindow();
    void quit();
    void shutdown();
  signals:
    void trayAvailableChanged();
    void closeToTrayChanged();
    void quitRequested();
  protected:
    bool eventFilter(QObject *object, QEvent *event) override;
  private slots:
    void refreshAvailability();
  private:
    SystemMediaBridge &m_media;
    I18n &m_translator;
    AvailabilityProbe m_availability;
    QPointer<QWindow> m_window;
    QTimer m_poll;
    std::unique_ptr<QMenu> m_menu;
    QSystemTrayIcon *m_icon = nullptr;
    QAction *m_track = nullptr, *m_toggle = nullptr, *m_playPause = nullptr;
    QAction *m_previous = nullptr, *m_next = nullptr, *m_quit = nullptr;
    Qt::WindowState m_restoreState = Qt::WindowNoState;
    bool m_available = false, m_closeToTray = true, m_stopping = false;
    void hideWindow();
    void updateMenu();
    void sendPlayback(const QString &method, const QString &capability);
};
