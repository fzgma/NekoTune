#include "platform/playback_window_controller.h"
#include "app_paths.h"

#include <QCloseEvent>
#include <QCursor>
#include <QDebug>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QScreen>
#include <algorithm>

PlaybackWindowController::PlaybackWindowController(QObject *parent, const QString &platform)
    : QObject(parent), m_platform(platform.isEmpty() ? QGuiApplication::platformName() : platform) {
    m_preferences = nekotune::AppPaths::setting("playback_windows").toObject();
    m_alwaysOnTop = m_preferences.value("mini_always_on_top").toBool(false);
    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(300);
    connect(&m_saveTimer, &QTimer::timeout, this, &PlaybackWindowController::savePreferences);
    connect(qGuiApp, &QGuiApplication::screenRemoved, this, [this] {
        // Screen changes settle after the removed screen's windows are reassigned.
        QTimer::singleShot(0, this, [this] {
            if (m_stopping) return;
            savePreferences();
            if (m_mini) restoreGeometry(m_mini, true);
            if (m_lyrics) restoreGeometry(m_lyrics, false);
        });
    });
}

PlaybackWindowController::~PlaybackWindowController() {
    shutdown();
    delete m_mini.data();
    delete m_lyrics.data();
}

void PlaybackWindowController::initialize(QQmlEngine *engine, QWindow *mainWindow) {
    if (m_main || m_stopping) return;
    m_engine = engine;
    m_main = mainWindow;
    if (!m_main) return;
    m_mainState = mainWindow->windowState() == Qt::WindowMinimized
                      ? Qt::WindowNoState : mainWindow->windowState();
    connect(mainWindow, &QWindow::windowStateChanged, this, [this](Qt::WindowState state) {
        if (state != Qt::WindowMinimized) m_mainState = state;
    });
}

bool PlaybackWindowController::desktopLyricsSupported() const {
    return m_platform == "xcb" || m_platform == "windows";
}

QString PlaybackWindowController::desktopLyricsUnavailableReason() const {
    if (desktopLyricsSupported()) return {};
    return wayland() ? QStringLiteral("desktop_lyrics_wayland")
                     : QStringLiteral("desktop_lyrics_unsupported");
}

bool PlaybackWindowController::desktopLyricsVisible() const {
    return m_lyrics && m_lyrics->isVisible();
}

bool PlaybackWindowController::presentationVisible() const {
    auto *window = m_miniMode ? m_mini.data() : m_main.data();
    return window && window->isVisible() && window->windowState() != Qt::WindowMinimized;
}

QWindow *PlaybackWindowController::createWindow(bool mini) {
    if (!m_engine) {
        emit errorOccurred("playback_window_failed");
        return nullptr;
    }
    QQmlComponent component(m_engine, QUrl(mini ? "qrc:/qml/windows/MiniPlayerWindow.qml"
                                              : "qrc:/qml/windows/DesktopLyricsWindow.qml"));
    auto *context = m_engine->rootContext();
    auto *object = component.createWithInitialProperties({
        {"manager", QVariant::fromValue(this)}, {"app", context->contextProperty("controllers")},
        {"translator", context->contextProperty("i18n")}, {"transport", context->contextProperty("ipcClient")}});
    auto *window = qobject_cast<QWindow *>(object);
    if (!window) {
        qWarning() << "Cannot create playback window:" << component.errors();
        delete object;
        emit errorOccurred("playback_window_failed");
        return nullptr;
    }
    // QObject ownership must not introduce a transient/native parent.
    QQmlEngine::setObjectOwnership(window, QQmlEngine::CppOwnership);
    window->QObject::setParent(this);
    window->setTransientParent(nullptr);
    window->setIcon(m_main ? m_main->icon() : QIcon());
    applyFlags(window, mini);
    restoreGeometry(window, mini);
    window->installEventFilter(this);
    connect(window, &QWindow::visibleChanged, this, &PlaybackWindowController::changed);
    return window;
}

void PlaybackWindowController::applyFlags(QWindow *window, bool mini) {
    if (!window) return;
    const bool visible = window->isVisible();
    const QRect geometry = window->geometry();
    auto flags = Qt::Window | Qt::FramelessWindowHint;
    if (!mini || m_alwaysOnTop) flags |= Qt::WindowStaysOnTopHint;
    if (!mini) {
        flags |= Qt::Tool | Qt::WindowDoesNotAcceptFocus;
        if (m_locked) flags |= Qt::WindowTransparentForInput;
    }
    window->setFlags(flags);
    window->setGeometry(geometry);
    if (visible) window->show();
}

void PlaybackWindowController::restoreGeometry(QWindow *window, bool mini) {
    QScreen *screen = m_main ? m_main->screen() : QGuiApplication::primaryScreen();
    const auto saved = m_preferences.value(mini ? "mini" : "desktop_lyrics").toObject();
    const int x = saved.value("x").toInt();
    const int y = saved.value("y").toInt();
    const bool hasPosition = saved.value("x").isDouble() && saved.value("y").isDouble();
    if (hasPosition) {
        for (auto *candidate : QGuiApplication::screens()) {
            if (candidate->availableGeometry().contains(QPoint(x, y))) { screen = candidate; break; }
        }
    }
    if (!screen) return;
    const QRect available = screen->availableGeometry();
    const int minWidth = std::min(360, available.width());
    const int minHeight = std::min(mini ? 144 : 120, available.height());
    window->setMinimumSize(QSize(minWidth, minHeight));
    const int width = std::clamp(saved.value("width").toInt(mini ? 440 : 900), minWidth, available.width());
    const int height = std::clamp(saved.value("height").toInt(mini ? 160 : 150), minHeight, available.height());
    const int defaultX = available.x() + (available.width() - width) / 2;
    const int defaultY = mini ? available.y() + (available.height() - height) / 2
                             : available.bottom() - height - 24;
    window->setGeometry(std::clamp(hasPosition ? x : defaultX, available.x(), available.right() - width + 1),
                        std::clamp(hasPosition ? y : defaultY, available.y(), available.bottom() - height + 1),
                        width, height);
}

void PlaybackWindowController::setMiniMode(bool enabled) {
    if (m_stopping || !m_main || m_main->property("appearanceStartupPending").toBool()) return;
    if (!enabled) { showMainWindow(); return; }
    if (!m_mini) m_mini = createWindow(true);
    if (!m_mini) return;
    m_mini->showNormal();
    m_mini->raise();
    m_mini->requestActivate();
    m_miniMode = true;
    m_main->hide();
    emit changed();
}

void PlaybackWindowController::showMainWindow() {
    if (m_stopping || !m_main || m_main->property("appearanceStartupPending").toBool()) return;
    if (m_mainState == Qt::WindowMaximized) m_main->showMaximized();
    else if (m_mainState == Qt::WindowFullScreen) m_main->showFullScreen();
    else m_main->showNormal();
    m_main->raise();
    m_main->requestActivate();
    if (m_mini) m_mini->hide();
    m_miniMode = false;
    emit changed();
}

void PlaybackWindowController::hidePresentation() {
    if (m_stopping || !m_trayAvailable) return;
    if (m_miniMode && m_mini) m_mini->hide();
    else if (m_main) m_main->hide();
    emit changed();
}

void PlaybackWindowController::setMiniAlwaysOnTop(bool enabled) {
    if (m_stopping || m_alwaysOnTop == enabled) return;
    m_alwaysOnTop = enabled;
    applyFlags(m_mini, true);
    savePreferences();
    emit changed();
}

void PlaybackWindowController::setDesktopLyricsVisible(bool visible) {
    if (m_stopping || visible == desktopLyricsVisible()) return;
    if (visible) {
        if (!desktopLyricsSupported()) return;
        if (!m_lyrics) m_lyrics = createWindow(false);
        if (!m_lyrics) return;
        m_locked = false;
        applyFlags(m_lyrics, false);
        m_lyrics->show();
    } else {
        m_locked = false;
        applyFlags(m_lyrics, false);
        m_lyrics->hide();
    }
    emit changed();
}

void PlaybackWindowController::setDesktopLyricsLocked(bool locked) {
    if (m_stopping || locked == m_locked) return;
    if (locked && (!canLockDesktopLyrics() || !desktopLyricsVisible())) return;
    m_locked = locked;
    endPointer();
    applyFlags(m_lyrics, false);
    emit changed();
}

void PlaybackWindowController::setTrayAvailable(bool available) {
    if (m_trayAvailable == available || m_stopping) return;
    m_trayAvailable = available;
    if (!available) {
        setDesktopLyricsLocked(false);
        if (!presentationVisible()) showMainWindow();
    }
    emit changed();
}

void PlaybackWindowController::savePreferences() {
    auto capture = [this](QWindow *window, const QString &key) {
        if (!window) return;
        const QRect geometry = window->geometry();
        m_preferences.insert(key, QJsonObject{{"x", geometry.x()}, {"y", geometry.y()},
                                              {"width", geometry.width()}, {"height", geometry.height()}});
    };
    capture(m_mini, "mini");
    capture(m_lyrics, "desktop_lyrics");
    m_preferences.insert("mini_always_on_top", m_alwaysOnTop);
    if (!nekotune::AppPaths::saveSetting("playback_windows", m_preferences)) {
        qWarning() << "Cannot save playback window preferences";
        emit errorOccurred("playback_window_save_failed");
    }
}

bool PlaybackWindowController::eventFilter(QObject *object, QEvent *event) {
    if (m_stopping || (object != m_mini && object != m_lyrics)) return false;
    if (event->type() == QEvent::Close) {
        static_cast<QCloseEvent *>(event)->ignore();
        if (object == m_mini) showMainWindow();
        else setDesktopLyricsVisible(false);
        return true;
    }
    if (event->type() == QEvent::Move || event->type() == QEvent::Resize) m_saveTimer.start();
    return false;
}

void PlaybackWindowController::startMove(QWindow *window) {
    if (m_stopping || !window || (window != m_mini && window != m_lyrics) || (window == m_lyrics && m_locked)) return;
    endPointer();
    if (window->startSystemMove() || !desktopLyricsSupported()) return;
    m_pointerWindow = window;
    m_pointerStart = QCursor::pos();
    m_pointerGeometry = window->geometry();
}

void PlaybackWindowController::startResize(QWindow *window, int edges) {
    if (m_stopping || !window || (window != m_mini && window != m_lyrics) || (window == m_lyrics && m_locked)) return;
    endPointer();
    const auto resizeEdges = Qt::Edges(edges);
    if (window->startSystemResize(resizeEdges) || !desktopLyricsSupported()) return;
    m_pointerWindow = window;
    m_pointerStart = QCursor::pos();
    m_pointerGeometry = window->geometry();
    m_resizeEdges = resizeEdges;
}

void PlaybackWindowController::updatePointer(QWindow *window, double globalX, double globalY) {
    if (!window || m_pointerWindow != window) return;
    const QPoint delta = QPoint(qRound(globalX), qRound(globalY)) - m_pointerStart;
    QRect next = m_pointerGeometry;
    if (!m_resizeEdges) next.translate(delta);
    else {
        if (m_resizeEdges.testFlag(Qt::RightEdge)) next.setWidth(std::max(window->minimumWidth(), next.width() + delta.x()));
        if (m_resizeEdges.testFlag(Qt::BottomEdge)) next.setHeight(std::max(window->minimumHeight(), next.height() + delta.y()));
    }
    window->setGeometry(next);
}

void PlaybackWindowController::endPointer() {
    m_pointerWindow.clear();
    m_resizeEdges = {};
}

void PlaybackWindowController::shutdown() {
    if (m_stopping) return;
    m_stopping = true;
    m_saveTimer.stop();
    endPointer();
    if (m_mini || m_lyrics) savePreferences();
    for (auto *window : {m_mini.data(), m_lyrics.data()}) {
        if (window) { window->removeEventFilter(this); window->hide(); }
    }
}
