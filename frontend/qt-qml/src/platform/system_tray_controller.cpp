#include "platform/system_tray_controller.h"
#include "platform/system_media_bridge.h"
#include "platform/playback_window_controller.h"
#include "app_paths.h"
#include "i18n.h"
#include <QAction>
#include <QCloseEvent>
#include <QDebug>
#include <QIcon>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QSystemTrayIcon>

namespace {
// Generate every supported size from the same embedded geometry, without a theme dependency.
QIcon catIcon() {
    QIcon icon;
    for (int size : {16, 22, 24, 32, 48, 64, 128}) {
        QPixmap image(size, size);
        image.fill(Qt::transparent);
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.scale(size / 64., size / 64.);
        QPainterPath head;
        head.moveTo(10, 29); head.lineTo(7, 6); head.lineTo(25, 18);
        head.quadTo(32, 15, 39, 18); head.lineTo(57, 6); head.lineTo(54, 29);
        head.cubicTo(64, 60, 0, 60, 10, 29);
        painter.setPen(QPen(QColor("#292335"), 3));
        painter.setBrush(QColor("#B9A4FF"));
        painter.drawPath(head);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor("#F5B9D4"));
        painter.drawPolygon(QPolygonF{{11, 13}, {13, 27}, {21, 21}});
        painter.drawPolygon(QPolygonF{{53, 13}, {51, 27}, {43, 21}});
        painter.setBrush(QColor("#292335"));
        painter.drawEllipse(QRectF(20, 31, 5, 7));
        painter.drawEllipse(QRectF(39, 31, 5, 7));
        painter.drawPolygon(QPolygonF{{29, 40}, {35, 40}, {32, 44}});
        icon.addPixmap(image);
    }
    return icon;
}
QString menuText(QString text) { return text.replace('&', "&&"); }
}

SystemTrayController::SystemTrayController(SystemMediaBridge &media, I18n &translator,
                                         QObject *parent, AvailabilityProbe availability)
    : QObject(parent), m_media(media), m_translator(translator),
      m_availability(availability ? std::move(availability) : QSystemTrayIcon::isSystemTrayAvailable) {
    const auto saved = nekotune::AppPaths::setting("close_to_tray");
    if (saved.isBool()) m_closeToTray = saved.toBool();
    m_available = m_availability();
    m_poll.setInterval(2000);
    connect(&m_poll, &QTimer::timeout, this, &SystemTrayController::refreshAvailability);
    connect(&media, &SystemMediaBridge::readyChanged, this, &SystemTrayController::updateMenu);
    connect(&media, &SystemMediaBridge::snapshotChanged, this, &SystemTrayController::updateMenu);
    connect(&translator, &I18n::languageChanged, this, &SystemTrayController::updateMenu);
}
SystemTrayController::~SystemTrayController() { shutdown(); }

void SystemTrayController::setPlaybackWindows(PlaybackWindowController *windows) {
    if (m_playbackWindows || !windows || m_stopping) return;
    m_playbackWindows = windows;
    windows->setTrayAvailable(m_available);
    connect(windows, &PlaybackWindowController::changed, this, &SystemTrayController::updateMenu);
    connect(this, &SystemTrayController::trayAvailableChanged, windows, [this, windows] {
        windows->setTrayAvailable(m_available);
    });
}

void SystemTrayController::attachWindow(QWindow *window) {
    if (!window || m_window || m_stopping) return;
    m_window = window;
    if (window->windowState() != Qt::WindowMinimized) m_restoreState = window->windowState();
    window->installEventFilter(this);
    connect(window, &QWindow::windowStateChanged, this, [this](Qt::WindowState state) {
        if (state != Qt::WindowMinimized) m_restoreState = state;
        updateMenu();
    });
    connect(window, &QWindow::visibleChanged, this, &SystemTrayController::updateMenu);
    m_menu = std::make_unique<QMenu>();
    auto action = [this](const char *name) {
        auto *item = m_menu->addAction(QString());
        item->setObjectName(QString::fromLatin1(name));
        return item;
    };
    m_track = action("trayTrack"); m_track->setEnabled(false);
    m_menu->addSeparator();
    m_toggle = action("trayToggleWindow");
    m_playPause = action("trayPlayPause");
    m_previous = action("trayPrevious");
    m_next = action("trayNext");
    m_menu->addSeparator();
    m_mini = action("trayMiniMode"); m_mini->setCheckable(true);
    m_lyrics = action("trayDesktopLyrics"); m_lyrics->setCheckable(true);
    m_lockLyrics = action("trayLockDesktopLyrics"); m_lockLyrics->setCheckable(true);
    connect(m_mini, &QAction::triggered, this, [this] {
        if (m_playbackWindows) m_playbackWindows->setMiniMode(!m_playbackWindows->miniMode());
    });
    connect(m_lyrics, &QAction::triggered, this, [this] {
        if (m_playbackWindows) m_playbackWindows->setDesktopLyricsVisible(!m_playbackWindows->desktopLyricsVisible());
    });
    connect(m_lockLyrics, &QAction::triggered, this, [this] {
        if (m_playbackWindows) m_playbackWindows->setDesktopLyricsLocked(!m_playbackWindows->desktopLyricsLocked());
    });
    m_menu->addSeparator();
    m_quit = action("trayQuit");
    connect(m_toggle, &QAction::triggered, this, &SystemTrayController::toggleWindow);
    connect(m_playPause, &QAction::triggered, this, [this] {
        sendPlayback("player.toggle_play_pause", m_media.playback().state() == "playing" ? "can_pause" : "can_play");
    });
    connect(m_previous, &QAction::triggered, this, [this] { sendPlayback("player.previous", "can_go_previous"); });
    connect(m_next, &QAction::triggered, this, [this] { sendPlayback("player.next", "can_go_next"); });
    connect(m_quit, &QAction::triggered, this, &SystemTrayController::quit);
    connect(m_menu.get(), &QMenu::aboutToShow, this, [this] { refreshAvailability(); updateMenu(); });
    m_icon = new QSystemTrayIcon(catIcon(), this);
    m_icon->setContextMenu(m_menu.get());
    window->setIcon(m_icon->icon());
    connect(m_icon, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) showWindow();
    });
    updateMenu();
    // Qt registers a visible icon automatically when the desktop tray appears later.
    m_icon->show();
    m_poll.start();
    refreshAvailability();
}

void SystemTrayController::setCloseToTray(bool value) {
    if (m_closeToTray == value) return;
    if (!nekotune::AppPaths::saveSetting("close_to_tray", value))
        qWarning() << "Cannot save NekoTune close-to-tray preference";
    m_closeToTray = value;
    emit closeToTrayChanged();
}
void SystemTrayController::refreshAvailability() {
    if (m_stopping) return;
    const bool available = m_availability();
    if (m_available == available) return;
    m_available = available;
    emit trayAvailableChanged();
    if (!available && m_window && !m_window->isVisible()
        && (!m_playbackWindows || !m_playbackWindows->presentationVisible())) showWindow();
    updateMenu();
}
bool SystemTrayController::eventFilter(QObject *object, QEvent *event) {
    if (object != m_window || event->type() != QEvent::Close || m_stopping)
        return QObject::eventFilter(object, event);
    refreshAvailability();
    if (m_available && m_closeToTray) {
        static_cast<QCloseEvent *>(event)->ignore();
        hideWindow();
        return true;
    }
    quit();
    return false;
}
void SystemTrayController::hideWindow() {
    if (!m_window || m_stopping) return;
    refreshAvailability();
    if (!m_available) return;
    if (m_playbackWindows) { m_playbackWindows->hidePresentation(); return; }
    if (m_window->windowState() != Qt::WindowMinimized) m_restoreState = m_window->windowState();
    m_window->hide();
}
void SystemTrayController::showWindow() {
    if (!m_window || m_stopping) return;
    if (m_window->property("appearanceStartupPending").toBool()) return;
    if (m_playbackWindows) { m_playbackWindows->showMainWindow(); return; }
    // show() chooses the platform's default state and can discard maximization.
    // Use the explicit show variants to retain the state across hiding/minimizing.
    const auto restoreState = m_restoreState;
    if (restoreState == Qt::WindowMaximized) m_window->showMaximized();
    else if (restoreState == Qt::WindowFullScreen) m_window->showFullScreen();
    else m_window->showNormal();
    m_window->raise();
    m_window->requestActivate();
}
void SystemTrayController::toggleWindow() {
    if (!m_window || m_stopping) return;
    if (m_playbackWindows ? m_playbackWindows->presentationVisible()
                          : m_window->isVisible() && m_window->windowState() != Qt::WindowMinimized) hideWindow();
    else showWindow();
}
void SystemTrayController::quit() {
    if (m_stopping) return;
    shutdown();
    emit quitRequested();
}
void SystemTrayController::shutdown() {
    if (m_stopping) return;
    m_stopping = true;
    m_poll.stop();
    if (m_playbackWindows) m_playbackWindows->shutdown();
    if (m_window) m_window->removeEventFilter(this);
    if (m_icon) m_icon->hide();
}
void SystemTrayController::sendPlayback(const QString &method, const QString &capability) {
    if (m_stopping || !m_media.ready() || !m_media.capability(capability)) return;
    m_media.request(method, {}, [](const QString &) {});
}
void SystemTrayController::updateMenu() {
    if (!m_icon || m_stopping) return;
    auto text = [this](const char *key) { return m_translator.text(QString::fromLatin1(key)); };
    const auto song = m_media.playback().song();
    QString track = text("tray_no_song");
    if (m_media.ready() && !song.isEmpty()) {
        track = song.value("title").toString();
        if (track.isEmpty()) track = text("tray_no_song");
        const auto artist = song.value("artist").toString();
        if (!artist.isEmpty()) track += " — " + artist;
    }
    m_track->setText(menuText(track));
    const bool playing = m_media.playback().state() == "playing";
    const auto state = m_media.playback().state();
    const char *status = !m_media.ready() ? "tray_disconnected" : playing ? "tray_playing"
                        : (state == "paused" || state == "loading") ? "tray_paused" : "tray_stopped";
    m_icon->setToolTip("NekoTune\n" + track + "\n" + text(status));
    const bool shown = m_playbackWindows ? m_playbackWindows->presentationVisible()
                                        : m_window && m_window->isVisible() && m_window->windowState() != Qt::WindowMinimized;
    m_toggle->setText(text(shown ? "tray_hide" : "tray_show"));
    m_toggle->setEnabled(m_window && (!shown || m_available));
    m_playPause->setText(text(playing ? "tray_pause" : "tray_play"));
    m_playPause->setEnabled(m_media.ready() && m_media.capability(playing ? "can_pause" : "can_play"));
    m_previous->setText(text("tray_previous"));
    m_previous->setEnabled(m_media.ready() && m_media.capability("can_go_previous"));
    m_next->setText(text("tray_next"));
    m_next->setEnabled(m_media.ready() && m_media.capability("can_go_next"));
    const bool hasWindows = !m_playbackWindows.isNull();
    for (auto *item : {m_mini, m_lyrics, m_lockLyrics}) item->setVisible(hasWindows);
    m_mini->setText(text("mini_mode"));
    m_mini->setChecked(hasWindows && m_playbackWindows->miniMode());
    m_lyrics->setText(text("desktop_lyrics"));
    m_lyrics->setChecked(hasWindows && m_playbackWindows->desktopLyricsVisible());
    m_lyrics->setEnabled(hasWindows && m_playbackWindows->desktopLyricsSupported());
    m_lockLyrics->setText(text(hasWindows && m_playbackWindows->desktopLyricsLocked() ? "desktop_lyrics_unlock" : "desktop_lyrics_lock"));
    m_lockLyrics->setChecked(hasWindows && m_playbackWindows->desktopLyricsLocked());
    m_lockLyrics->setEnabled(hasWindows && m_playbackWindows->canLockDesktopLyrics() && m_playbackWindows->desktopLyricsVisible());
    m_quit->setText(text("tray_quit"));
}
