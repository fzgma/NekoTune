#pragma once

#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QRect>
#include <QTimer>
#include <QWindow>

class QQmlEngine;

// Owns presentation windows only; playback and lyrics remain shared controller state.
class PlaybackWindowController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool miniMode READ miniMode WRITE setMiniMode NOTIFY changed)
    Q_PROPERTY(bool miniAlwaysOnTop READ miniAlwaysOnTop WRITE setMiniAlwaysOnTop NOTIFY changed)
    Q_PROPERTY(bool desktopLyricsVisible READ desktopLyricsVisible WRITE setDesktopLyricsVisible NOTIFY changed)
    Q_PROPERTY(bool desktopLyricsLocked READ desktopLyricsLocked WRITE setDesktopLyricsLocked NOTIFY changed)
    Q_PROPERTY(bool desktopLyricsSupported READ desktopLyricsSupported CONSTANT)
    Q_PROPERTY(QString desktopLyricsUnavailableReason READ desktopLyricsUnavailableReason CONSTANT)
    Q_PROPERTY(bool wayland READ wayland CONSTANT)
    Q_PROPERTY(bool canLockDesktopLyrics READ canLockDesktopLyrics NOTIFY changed)
  public:
    explicit PlaybackWindowController(QObject *parent = nullptr,
                                      const QString &platform = {});
    ~PlaybackWindowController() override;
    void initialize(QQmlEngine *engine, QWindow *mainWindow);
    bool miniMode() const { return m_miniMode; }
    bool miniAlwaysOnTop() const { return m_alwaysOnTop; }
    bool desktopLyricsVisible() const;
    bool desktopLyricsLocked() const { return m_locked; }
    bool desktopLyricsSupported() const;
    QString desktopLyricsUnavailableReason() const;
    bool wayland() const { return m_platform.startsWith("wayland"); }
    bool canLockDesktopLyrics() const { return m_trayAvailable && desktopLyricsSupported(); }
    bool presentationVisible() const;
    void setTrayAvailable(bool available);
    void setMiniMode(bool enabled);
    void setMiniAlwaysOnTop(bool enabled);
    void setDesktopLyricsVisible(bool visible);
    void setDesktopLyricsLocked(bool locked);
    Q_INVOKABLE void showMainWindow();
    void hidePresentation();
    void shutdown();
    Q_INVOKABLE void startMove(QWindow *window);
    Q_INVOKABLE void startResize(QWindow *window, int edges);
    Q_INVOKABLE void updatePointer(QWindow *window, double globalX, double globalY);
    Q_INVOKABLE void endPointer();
  signals:
    void changed();
    void errorOccurred(const QString &key);
  protected:
    bool eventFilter(QObject *object, QEvent *event) override;
  private:
    QPointer<QQmlEngine> m_engine;
    QPointer<QWindow> m_main, m_mini, m_lyrics;
    QTimer m_saveTimer;
    QJsonObject m_preferences;
    QString m_platform;
    Qt::WindowState m_mainState = Qt::WindowNoState;
    bool m_miniMode = false, m_alwaysOnTop = false, m_locked = false;
    bool m_trayAvailable = false, m_stopping = false;
    QPointer<QWindow> m_pointerWindow;
    QPoint m_pointerStart;
    QRect m_pointerGeometry;
    Qt::Edges m_resizeEdges;
    QWindow *createWindow(bool mini);
    void applyFlags(QWindow *window, bool mini);
    void restoreGeometry(QWindow *window, bool mini);
    void savePreferences();
};
