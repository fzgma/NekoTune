#include "wallpaper/wallpaper_manager.h"
#include "app_paths.h"
#include "ipc_client.h"
#include "controllers/app_controllers.h"

#include <QCryptographicHash>
#include <QGuiApplication>
#include <QJsonObject>
#include <QPlatformSurfaceEvent>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QScreen>
#include <cmath>

namespace {
const QVariantMap defaults{{"enabled", false}, {"autoRestore", false},     {"target", "primary"},
                           {"screenId", ""},   {"coverBackground", false}, {"blur", false},
                           {"lyrics", true},   {"nextLine", true},         {"spectrum", false},
                           {"updateHz", 30},   {"intensity", 0.35},        {"transparent", true},
                           {"style", "classic"}, {"themeWallpaper", ""}};

bool valid(const QString &key, const QVariant &value) {
    if (!defaults.contains(key))
        return false;
    if (defaults.value(key).metaType().id() == QMetaType::Bool)
        return value.metaType().id() == QMetaType::Bool;
    if (key == "target")
        return value == "primary" || value == "all" || value == "selected";
    if (key == "style")
        return value.metaType().id() == QMetaType::QString &&
               (value == "classic" || value == "vinyl" || value == "lyrics" || value == "theme");
    if (key == "themeWallpaper")
        return value.metaType().id() == QMetaType::QString && value.toString().size() <= 200;
    if (key == "screenId")
        return value.metaType().id() == QMetaType::QString && value.toString().size() <= 128;
    if (value.metaType().id() != QMetaType::Int && value.metaType().id() != QMetaType::Double &&
        value.metaType().id() != QMetaType::LongLong)
        return false;
    const double number = value.toDouble();
    if (key == "updateHz")
        return number == 30 || number == 60;
    return key == "intensity" && std::isfinite(number) && number >= 0 && number <= 1;
}
} // namespace

WallpaperContext::WallpaperContext(WallpaperManager *manager, AppControllers *app, IpcClient *client)
    : QObject(manager), m_manager(manager), m_app(app), m_client(client) {
    connect(manager, &WallpaperManager::changed, this, &WallpaperContext::preferencesChanged);
    connect(manager, &WallpaperManager::dataReadyChanged, this, &WallpaperContext::playbackStateChanged);
    connect(client, &IpcClient::connectedChanged, this, &WallpaperContext::playbackStateChanged);
    connect(app->playback, &PlaybackController::songChanged, this, &WallpaperContext::songChanged);
    connect(app->playback, &PlaybackController::songChanged, this, &WallpaperContext::playbackStateChanged);
    connect(app->playback, &PlaybackController::positionChanged, this, &WallpaperContext::positionChanged);
    connect(app->playback, &PlaybackController::durationChanged, this, &WallpaperContext::durationChanged);
    connect(app->playback, &PlaybackController::playbackRateChanged, this, &WallpaperContext::playbackRateChanged);
    connect(app->playback, &PlaybackController::stateChanged, this, &WallpaperContext::playbackStateChanged);
    connect(app->lyrics, &LyricsController::changed, this, &WallpaperContext::lyricsChanged);
}

void WallpaperContext::setWindow(QWindow *window) {
    m_window = window;
    connect(window, &QWindow::visibleChanged, this, &WallpaperContext::playbackStateChanged);
    connect(window, &QWindow::screenChanged, this, &WallpaperContext::screenChanged);
    connect(window, SIGNAL(outputRunningChanged()), this, SIGNAL(playbackStateChanged()));
    emit playbackStateChanged();
    emit screenChanged();
}

QVariantMap WallpaperContext::song() const { return m_app->playback->song(); }
QVariantMap WallpaperContext::lyrics() const { return m_app->lyrics->current(); }
double WallpaperContext::position() const { return m_app->playback->position(); }
double WallpaperContext::duration() const { return m_app->playback->duration(); }
double WallpaperContext::playbackRate() const { return m_app->playback->playbackRate(); }
bool WallpaperContext::connected() const { return m_client->connected() && m_manager->dataReady(); }
bool WallpaperContext::hasSong() const {
    const auto current = song();
    return !current.value("resource_key").toString().isEmpty() ||
           !current.value("song_hash").toString().isEmpty();
}
bool WallpaperContext::playing() const { return updating() && m_app->playback->state() == "playing"; }
bool WallpaperContext::updating() const {
    return m_window && m_window->isVisible() && m_window->property("outputRunning").toBool() &&
           connected() && hasSong();
}
QVariantMap WallpaperContext::preferences() const { return m_manager->settings(); }
QObject *WallpaperContext::screen() const { return m_window ? m_window->screen() : QGuiApplication::primaryScreen(); }

WallpaperManager::WallpaperManager(QObject *parent, std::unique_ptr<WallpaperPlatform> platform)
    : QObject(parent), m_platform(platform ? std::move(platform) : createWallpaperPlatform()),
      m_settings(defaults) {
    connect(this, &WallpaperManager::changed, this, &WallpaperManager::outputsChanged);
    const auto saved = nekotune::AppPaths::setting("wallpaper").toObject().toVariantMap();
    for (auto it = saved.begin(); it != saved.end(); ++it)
        if (valid(it.key(), it.value()))
            m_settings[it.key()] = it.value();
    // A preference cannot advertise an analysis pipeline that does not exist.
    m_settings["spectrum"] = false;
    m_clock.start();
    m_health.setInterval(500);
    connect(&m_health, &QTimer::timeout, this, &WallpaperManager::checkOutputs);
    for (auto *screen : QGuiApplication::screens())
        addScreen(screen);
    connect(qGuiApp, &QGuiApplication::screenAdded, this, [this](QScreen *screen) {
        if (m_stopping)
            return;
        addScreen(screen);
        reconcile();
    });
    connect(qGuiApp, &QGuiApplication::screenRemoved, this, [this](QScreen *screen) {
        auto it = m_outputs.find(screen);
        if (it != m_outputs.end()) {
            destroy(it->second); // Before Qt migrates the surface to a
                                 // different output.
            m_outputs.erase(it);
        }
        QTimer::singleShot(0, this, &WallpaperManager::reconcile);
        emit changed();
    });
    connect(qGuiApp, &QGuiApplication::primaryScreenChanged, this, &WallpaperManager::reconcile);
}

WallpaperManager::~WallpaperManager() { shutdown(); }

void WallpaperManager::initialize(QQmlEngine *engine, IpcClient *client) {
    if (m_engine || m_stopping || !engine || !client)
        return;
    m_engine = engine;
    // The socket may connect before the complete snapshot. Do not animate stale
    // controller data until server.connected has been delivered to controllers.
    connect(client, &IpcClient::connectedChanged, this, [this, client] {
        if (!client->connected() && m_dataReady) {
            m_dataReady = false;
            emit dataReadyChanged();
        }
    });
    connect(client, &IpcClient::eventReceived, this, [this](const QJsonObject &event) {
        if (event.value("event") == "server.connected" && !m_stopping) {
            m_dataReady = true;
            emit dataReadyChanged();
        }
    });
    m_reason = m_platform->unavailableReason();
    m_requested = m_settings.value("enabled").toBool() && m_settings.value("autoRestore").toBool() &&
                  qEnvironmentVariable("NEKOTUNE_SAFE_MODE") != "1";
    reconcile();
}

bool WallpaperManager::linuxOverlay() const {
#ifdef Q_OS_LINUX
    return true;
#else
    return false;
#endif
}

QVariantMap WallpaperManager::audioAnalysis() const {
    return {{"spectrumAvailable", false},
            {"reason", "wallpaper_spectrum_unavailable"},
            {"source", "none"},
            {"bands", QVariantList{}}};
}

QString WallpaperManager::screenId(QScreen *screen) {
    // Connector plus EDID where available; never persist a pointer or enumeration
    // index.
    const auto identity = (screen->name() + '\n' + screen->manufacturer() + '\n' + screen->model() + '\n' +
                           screen->serialNumber())
                              .toUtf8();
    return QString::fromLatin1(QCryptographicHash::hash(identity, QCryptographicHash::Sha256).toHex());
}

void WallpaperManager::addScreen(QScreen *screen) {
    if (m_outputs.contains(screen))
        return;
    m_outputs[screen].screen = screen;
    auto geometryChanged = [this] {
        QTimer::singleShot(0, this, [this] {
            if (!m_stopping) {
                checkOutputs();
                emit changed();
            }
        });
    };
    connect(screen, &QScreen::geometryChanged, this, geometryChanged);
    connect(screen, &QScreen::logicalDotsPerInchChanged, this, geometryChanged);
    connect(screen, &QScreen::orientationChanged, this, geometryChanged);
}

bool WallpaperManager::selected(QScreen *screen) const {
    const QString target = m_settings.value("target").toString();
    if (target == "all")
        return true;
    if (target == "primary")
        return screen == QGuiApplication::primaryScreen();
    if (screenId(screen) != m_settings.value("screenId").toString())
        return false;
    // Ambiguous identifiers are safer left inactive than assigned to the wrong
    // screen.
    int matches = 0;
    for (const auto &[candidate, output] : m_outputs)
        if (output.screen && screenId(candidate) == screenId(screen))
            ++matches;
    return matches == 1;
}

QVariantList WallpaperManager::outputs() const {
    QVariantList result;
    bool found = false;
    for (auto *screen : QGuiApplication::screens()) {
        auto it = m_outputs.find(screen);
        if (it == m_outputs.end())
            continue;
        const auto &output = it->second;
        const bool chosen = selected(screen);
        found |= chosen;
        result.append(QVariantMap{{"id", screenId(screen)},
                                  {"name", screen->name()},
                                  {"selected", chosen},
                                  {"state", output.state},
                                  {"error", output.error},
                                  {"themeStatus", output.window ? output.window->property("themeStatus").toString() : QString{}},
                                  {"themeError", output.window ? output.window->property("themeError").toString() : QString{}},
                                  {"width", screen->size().width()},
                                  {"height", screen->size().height()},
                                  {"scale", screen->devicePixelRatio()}});
    }
    if (!found && m_settings.value("target") == "selected")
        result.append(QVariantMap{{"id", m_settings.value("screenId")},
                                  {"name", ""},
                                  {"selected", true},
                                  {"state", "unavailable"},
                                  {"error", "wallpaper_display_missing"}});
    return result;
}

int WallpaperManager::runningCount() const {
    int count = 0;
    for (const auto &[screen, output] : m_outputs)
        if (output.window && output.state == "running")
            ++count;
    return count;
}

bool WallpaperManager::setOption(const QString &key, const QVariant &value) {
    if (m_stopping)
        return false;
    if (!valid(key, value) || (key == "spectrum" && value.toBool())) {
        m_error = key == "spectrum" ? "wallpaper_spectrum_unavailable" : "wallpaper_invalid_setting";
        emit changed();
        return false;
    }
    // Always allow immediate shutdown, even when settings storage is unavailable.
    if (key == "enabled" && !value.toBool()) {
        m_requested = false;
        reconcile();
    }
    auto next = m_settings;
    next[key] = value;
    if (!nekotune::AppPaths::saveSetting("wallpaper", QJsonObject::fromVariantMap(next))) {
        m_error = "wallpaper_save_failed";
        emit changed();
        return false;
    }
    m_settings = next;
    m_error.clear();
    if (key == "enabled")
        m_requested = value.toBool();
    if (key == "enabled" || key == "target" || key == "screenId") {
        for (auto &[screen, output] : m_outputs) {
            if (!output.window) {
                output.state = "idle";
                output.error.clear();
                output.attempts = 0;
                output.recoveryDeadline = 0;
            }
        }
        reconcile();
    }
    emit changed();
    return true;
}

void WallpaperManager::destroy(Output &output) {
    if (!output.window)
        return;
    auto *window = output.window.data();
    output.window.clear();
    window->removeEventFilter(this);
    window->setProperty("outputRunning", false);
    window->hide();
    delete window;
}

void WallpaperManager::start(Output &output) {
    if (!m_engine || !output.screen || m_stopping)
        return;
    QQmlComponent component(m_engine, QUrl("qrc:/qml/wallpaper/Wallpaper.qml"));
    auto *context = m_engine->rootContext();
    auto *app = qobject_cast<AppControllers *>(context->contextProperty("controllers").value<QObject *>());
    auto *client = qobject_cast<IpcClient *>(context->contextProperty("ipcClient").value<QObject *>());
    if (!app || !client) {
        output.state = "failed";
        output.error = "wallpaper_qml_failed";
        return;
    }
    auto *wallpaperContext = new WallpaperContext(this, app, client);
    auto *object =
        component.createWithInitialProperties({{"manager", QVariant::fromValue(this)},
                                               {"app", context->contextProperty("controllers")},
                                               {"translator", context->contextProperty("i18n")},
                                               {"transport", context->contextProperty("ipcClient")},
                                               {"wallpaperData", QVariant::fromValue(wallpaperContext)}});
    auto *window = qobject_cast<QWindow *>(object);
    if (!window) {
        qWarning() << "Wallpaper QML:" << component.errors();
        delete object;
        delete wallpaperContext;
        output.state = "failed";
        output.error = "wallpaper_qml_failed";
        return;
    }
    QQmlEngine::setObjectOwnership(window, QQmlEngine::CppOwnership);
    window->QObject::setParent(this);
    wallpaperContext->setParent(window);
    wallpaperContext->setWindow(window);
    window->setTransientParent(nullptr);
    window->setFlags(Qt::Window | Qt::FramelessWindowHint | Qt::WindowDoesNotAcceptFocus |
                     Qt::WindowTransparentForInput | Qt::Tool);
    output.window = window;
    // QML presentation failures fall back within this window. They do not mean
    // the native desktop surface failed or needs to be recreated.
    connect(window, SIGNAL(themeStatusChanged()), this, SIGNAL(outputsChanged()));
    connect(window, SIGNAL(themeErrorChanged()), this, SIGNAL(outputsChanged()));
    const auto error = m_platform->prepare(window, output.screen);
    if (!error.isEmpty()) {
        fail(output, error);
        return;
    }
    window->installEventFilter(this);
    output.state = "starting";
    output.error.clear();
    output.deadline = m_clock.elapsed() + 8000;
    window->show();
}

void WallpaperManager::fail(Output &output, const QString &error) {
    destroy(output);
    output.error = error;
    output.state = "failed";
    const qint64 now = m_clock.elapsed();
    if (!output.recoveryDeadline)
        output.recoveryDeadline = now + 120000;
    if (m_requested && !m_stopping && m_platform->retryable() && output.attempts < 8 &&
        now < output.recoveryDeadline) {
        output.nextAttempt = now + qMin(30000, 1000 << output.attempts++);
        output.state = "retrying";
    }
    qWarning() << "Wallpaper output" << (output.screen ? output.screen->name() : QString()) << error
               << output.state;
}

void WallpaperManager::reconcile() {
    if (m_stopping)
        return;
    for (auto &[screen, output] : m_outputs) {
        if (!m_requested || !selected(screen) || !supported()) {
            destroy(output);
            output.state = m_requested && selected(screen) && !supported() ? "unavailable" : "idle";
            output.error = output.state == "unavailable" ? m_reason : QString();
        } else if (!output.window && output.state == "idle")
            start(output);
    }
    updateTimer();
    emit changed();
}

void WallpaperManager::checkOutputs() {
    if (m_stopping || !m_requested)
        return;
    bool changedState = false;
    for (auto &[screen, output] : m_outputs) {
        if (!output.screen)
            continue;
        const QString previous = output.state;
        if (output.window) {
            const auto error = m_platform->check(output.window, screen);
            if (!error.isEmpty())
                fail(output, error);
            else if (output.state == "starting") {
                if (output.window->isExposed()) {
                    output.state = "running";
                    output.window->setProperty("outputRunning", true);
                    output.attempts = 0;
                    output.recoveryDeadline = 0;
                    qInfo() << "NekoTune wallpaper mapped on" << screen->name();
                } else if (m_clock.elapsed() >= output.deadline)
                    fail(output, "wallpaper_map_timeout");
            }
        } else if (output.state == "retrying" && m_clock.elapsed() >= output.nextAttempt) {
            if (m_clock.elapsed() >= output.recoveryDeadline)
                output.state = "failed";
            else if (selected(screen))
                start(output);
        }
        changedState |= previous != output.state;
    }
    updateTimer();
    if (changedState)
        emit changed();
}

void WallpaperManager::updateTimer() {
    bool needed = false;
    bool mapping = false;
    for (const auto &[screen, output] : m_outputs)
        needed |= output.state == "starting" || output.state == "retrying" ||
                  (output.window && m_platform->needsHealthCheck());
    for (const auto &[screen, output] : m_outputs)
        mapping |= output.state == "starting";
    m_health.setInterval(mapping ? 100 : 1000);
    if (needed && m_requested && !m_stopping)
        m_health.start();
    else
        m_health.stop();
}

bool WallpaperManager::eventFilter(QObject *object, QEvent *event) {
    if (m_stopping)
        return false;
    if (event->type() == QEvent::Close || (event->type() == QEvent::PlatformSurface &&
                                           static_cast<QPlatformSurfaceEvent *>(event)->surfaceEventType() ==
                                               QPlatformSurfaceEvent::SurfaceAboutToBeDestroyed)) {
        const QPointer<QWindow> window = qobject_cast<QWindow *>(object);
        if (window)
            window->setProperty("outputRunning", false);
        QTimer::singleShot(0, this, [this, window] {
            if (m_stopping || !window)
                return;
            for (auto &[screen, output] : m_outputs)
                if (output.window == window) {
                    fail(output, "wallpaper_surface_lost");
                    break;
                }
            updateTimer();
            emit changed();
        });
    }
    return false;
}

void WallpaperManager::retry() {
    if (m_stopping)
        return;
    m_reason = m_platform->unavailableReason();
    for (auto &[screen, output] : m_outputs)
        if (!output.window) {
            output.state = "idle";
            output.attempts = 0;
            output.recoveryDeadline = 0;
        }
    reconcile();
}

void WallpaperManager::shutdown() {
    if (m_stopping)
        return;
    m_stopping = true;
    m_health.stop();
    m_requested = false;
    m_dataReady = false;
    emit dataReadyChanged();
    for (auto &[screen, output] : m_outputs) {
        destroy(output);
        output.state = "idle";
    }
    emit changed();
}
