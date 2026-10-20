#include "infrastructure/extensions/plugin_manager.h"
#include "app_paths.h"
#include "infrastructure/extensions/audio_proxy.h"
#include "infrastructure/extensions/crash_guard.h"
#include "infrastructure/extensions/extension_secrets.h"
#include "infrastructure/extensions/semver.h"
#include "infrastructure/extensions/theme_package.h"
#include "infrastructure/extensions/zip_archive.h"
#include <nekotune/sdk/extension_plugin.h>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QJsonDocument>
#include <QMutex>
#include <QPluginLoader>
#include <QPointer>
#include <QRegularExpression>
#include <QScopeGuard>
#include <QSaveFile>
#include <QThread>
#include <QTimer>
#include <QUuid>
#include <atomic>
#include <map>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

namespace nekotune {
namespace {
const QStringList uiKinds{"pages", "settings", "slots", "themes", "menus", "toolbars"};
const QStringList registrationKinds = QStringList{"services", "commands", "music", "lyrics"} + uiKinds;
const QStringList sourceKinds{"pages", "settings", "slots", "toolbars"};
constexpr int activationTimeout = 30'000;
constexpr int callTimeout = 30'000;

QVariant readJson(const QString &path, const QVariant &fallback) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return fallback;
    const auto document = QJsonDocument::fromJson(file.readAll());
    return document.isNull() ? fallback : document.toVariant();
}
Result<void> writeJson(const QString &path, const QVariant &value) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return failure(file.errorString(), ErrorCode::Io);
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    file.write(QJsonDocument::fromVariant(value).toJson());
    if (!file.commit())
        return failure(file.errorString(), ErrorCode::Io);
    return {};
}
using Done = std::function<void(Result<void>)>;
/// Run asynchronous steps one after another; stopOnError ends the chain at the first failure.
void sequence(QStringList items, std::function<void(const QString &, Done)> step, bool stopOnError, Done done) {
    if (items.isEmpty()) {
        done({});
        return;
    }
    const auto first = items.takeFirst();
    step(first, [items, step, stopOnError, done](Result<void> result) {
        if (!result && stopOnError)
            done(result);
        else
            sequence(items, step, stopOnError, done);
    });
}
struct PendingCall {
    std::atomic<bool> fired{false};
    IExtensionBackend::Completion done;
    QTimer *timer = nullptr;
};
void finishCall(const std::shared_ptr<PendingCall> &call, Result<QVariantMap> result) {
    if (call->fired.exchange(true))
        return;
    if (call->timer) {
        call->timer->stop();
        call->timer->deleteLater();
    }
    auto done = std::move(call->done);
    if (done)
        done(std::move(result));
}
} // namespace

struct PluginManager::Run {
    QString id;
    QString directory;
    quint64 generation = 0;
    QString state = "starting";
    sdk::IExtensionPlugin *plugin = nullptr;
    std::unique_ptr<Context> context;
    QHash<QString, QMap<QString, QVariantMap>> registrations;
    // Function objects and providers below belong to the plugin; stop() drops them all.
    QHash<QString, sdk::Handler> services, commands;
    QHash<QString, sdk::IMusicProvider *> music;
    QHash<QString, sdk::ILyricsProvider *> lyrics;
    std::map<quint64, std::pair<QString, sdk::EventHandler>> subscriptions;
    quint64 nextSubscription = 1;
    QVariantMap storage;
    QSet<QString> secretKeys;
    QList<Done> waiters;
    QList<std::shared_ptr<PendingCall>> calls;
    std::unique_ptr<QTimer> startTimer, debounce;
    std::unique_ptr<QFileSystemWatcher> watcher;
};

class PluginManager::Handle final : public sdk::Registration {
  public:
    Handle(PluginManager *manager, std::weak_ptr<Run> run, std::function<void(Run &)> remove)
        : m_manager(manager), m_run(std::move(run)), m_remove(std::move(remove)) {}
    ~Handle() override { dispose(); }
    void dispose() override {
        if (m_disposed.exchange(true) || !m_manager)
            return;
        m_manager->post([manager = m_manager, run = m_run, remove = m_remove] {
            if (const auto alive = run.lock(); alive && manager) {
                remove(*alive);
                manager->notifyChanged();
            }
        });
    }

  private:
    QPointer<PluginManager> m_manager;
    std::weak_ptr<Run> m_run;
    std::function<void(Run &)> m_remove;
    std::atomic<bool> m_disposed{false};
};

class PluginManager::Context final : public sdk::IExtensionContext {
  public:
    Context(PluginManager *manager, std::weak_ptr<Run> run, const Entry &entry, QString dataDirectory,
            QVariantMap config)
        : m_manager(manager), m_run(std::move(run)), m_id(entry.manifest.id()), m_version(entry.manifest.version()),
          m_directory(entry.directory), m_dataDirectory(std::move(dataDirectory)), m_config(std::move(config)) {}

    QString id() const override { return m_id; }
    QString version() const override { return m_version; }
    QString directory() const override { return m_directory; }
    QString dataDirectory() const override { return m_dataDirectory; }
    sdk::Params config() const override {
        QMutexLocker lock(&m_mutex);
        return m_config;
    }
    void setConfig(const QVariantMap &config) {
        QMutexLocker lock(&m_mutex);
        m_config = config;
    }
    void updateConfig(const sdk::Params &config, std::function<void(Result<void>)> done) override {
        auto reply = forward(std::move(done));
        onRun([manager = m_manager, config, reply](const std::shared_ptr<Run> &run) {
            const auto result = manager->setConfig(run->id, config);
            reply(result ? Result<void>() : Result<void>(result.error()));
        });
    }
    void hostCall(const QString &method, const sdk::Params &params, sdk::Reply done) override {
        auto reply = forward(std::move(done));
        onRun([manager = m_manager, method, params, reply](const std::shared_ptr<Run> &run) {
            // A starting extension holds the mutation queue; queuing behind it would deadlock.
            static const QRegularExpression lifecycle(R"(^extensions\.(install|enable|disable|reload|uninstall)$)");
            if (run->state != "running" && lifecycle.match(method).hasMatch())
                reply(failure("Defer lifecycle mutations until activation has completed"));
            else if (!manager->m_hostCall)
                reply(failure("Host unavailable"));
            else
                manager->m_hostCall(method, params, reply);
        });
    }
    sdk::RegistrationPtr registerService(const QString &id, sdk::Handler handler) override {
        return add("services", id, {}, [id, handler](Run &run) { run.services.insert(id, handler); },
                   [id](Run &run) { run.services.remove(id); });
    }
    void callService(const QString &extension, const QString &service, const sdk::Params &params,
                     sdk::Reply done) override {
        auto reply = forward(std::move(done));
        onRun([manager = m_manager, extension, service, params, reply](const std::shared_ptr<Run> &) {
            manager->invoke(extension, service, params, reply);
        });
    }
    sdk::RegistrationPtr registerCommand(const QString &id, sdk::Handler handler,
                                         const sdk::Params &descriptor) override {
        return add("commands", id, descriptor, [id, handler](Run &run) { run.commands.insert(id, handler); },
                   [id](Run &run) { run.commands.remove(id); });
    }
    sdk::RegistrationPtr registerUi(const QString &kind, const QString &id, const sdk::Params &descriptor) override {
        if (!uiKinds.contains(kind)) {
            log(QtWarningMsg, "Unknown UI registration kind: " + kind);
            return nullptr;
        }
        return add(kind, id, descriptor, [](Run &) {}, [](Run &) {});
    }
    sdk::RegistrationPtr registerMusicProvider(const QString &id, sdk::IMusicProvider *provider,
                                               const sdk::Params &descriptor) override {
        if (!provider)
            return nullptr;
        return add("music", id, descriptor, [id, provider](Run &run) { run.music.insert(id, provider); },
                   [id](Run &run) { run.music.remove(id); });
    }
    sdk::RegistrationPtr registerLyricsProvider(const QString &id, sdk::ILyricsProvider *provider,
                                                const sdk::Params &descriptor) override {
        if (!provider)
            return nullptr;
        return add("lyrics", id, descriptor, [id, provider](Run &run) { run.lyrics.insert(id, provider); },
                   [id](Run &run) { run.lyrics.remove(id); });
    }
    sdk::RegistrationPtr on(const QString &event, sdk::EventHandler handler) override {
        // Reserve the key now so the handle can remove a subscription installed later.
        const auto key = m_nextSubscription.fetch_add(1);
        onRun([key, event, handler](const std::shared_ptr<Run> &run) {
            if (run->state != "stopping")
                run->subscriptions.emplace(key, std::pair{event, handler});
        });
        return std::make_unique<Handle>(m_manager, m_run, [key](Run &run) { run.subscriptions.erase(key); });
    }
    void publish(const QString &event, const sdk::Params &data) override {
        onRun([manager = m_manager, event, data](const std::shared_ptr<Run> &run) {
            if (!validExtensionId(event)) {
                manager->recordLog(run->id, "error", "Invalid event name: " + event);
                return;
            }
            const QVariantMap message{
                {"event", QStringLiteral("extension.%1.%2").arg(run->id, event)}, {"extensionId", run->id}, {"data", data}};
            manager->broadcast(message);
            emit manager->eventReady(message);
        });
    }
    void storageGet(const QString &key, std::function<void(Result<QVariant>)> done) override {
        auto reply = forward(std::move(done));
        onRun([key, reply](const std::shared_ptr<Run> &run) {
            if (key.size() > 256)
                reply(failure("Invalid storage key"));
            else
                reply(run->storage.value(key));
        });
    }
    void storageSet(const QString &key, const QVariant &value, std::function<void(Result<void>)> done) override {
        auto reply = forward(std::move(done));
        onRun([dataDirectory = m_dataDirectory, key, value, reply](const std::shared_ptr<Run> &run) {
            if (key.size() > 256) {
                reply(failure("Invalid storage key"));
                return;
            }
            auto next = run->storage;
            next.insert(key, value);
            if (QJsonDocument::fromVariant(next).toJson(QJsonDocument::Compact).size() > 8 * 1024 * 1024) {
                reply(failure("SDK storage limit exceeded; use dataDirectory for larger files"));
                return;
            }
            const auto saved = writeJson(QDir(dataDirectory).filePath("storage.json"), next);
            if (saved)
                run->storage = next;
            reply(saved);
        });
    }
    void secretGet(const QString &key, std::function<void(Result<QString>)> done) override {
        auto reply = forward(std::move(done));
        secret("get", key, {}, [reply](Result<QVariantMap> result) {
            if (!result)
                reply(result.error());
            else
                reply(result.value().value("value").toString());
        });
    }
    void secretSet(const QString &key, const QString &value, std::function<void(Result<void>)> done) override {
        auto reply = forward(std::move(done));
        secret("set", key, value, [reply](Result<QVariantMap> result) {
            reply(result ? Result<void>() : Result<void>(result.error()));
        });
    }
    void secretDelete(const QString &key, std::function<void(Result<void>)> done) override {
        auto reply = forward(std::move(done));
        secret("delete", key, {}, [reply](Result<QVariantMap> result) {
            reply(result ? Result<void>() : Result<void>(result.error()));
        });
    }
    void reportTask(const QString &task, const sdk::Params &progress) override {
        auto data = progress;
        data.insert("task", task);
        publish("task", data);
    }
    void log(QtMsgType level, const QString &message) override {
        const QString name = level == QtDebugMsg ? "debug" : level == QtInfoMsg ? "info" : level == QtWarningMsg ? "warn" : "error";
        onRun([manager = m_manager, name, message](const std::shared_ptr<Run> &run) {
            manager->recordLog(run->id, name, message);
        });
    }

  private:
    void onRun(std::function<void(const std::shared_ptr<Run> &)> work) const {
        m_manager->post([run = m_run, work] {
            if (const auto alive = run.lock())
                work(alive);
        });
    }
    /// Return a host-side completion that calls the plugin's callback on the backend thread,
    /// or drops it when the run has stopped.
    template <class T> std::function<void(Result<T>)> forward(std::function<void(Result<T>)> done) const {
        return [manager = QPointer<PluginManager>(m_manager), run = m_run, id = m_id, done](Result<T> result) {
            if (!manager || !done)
                return;
            manager->post([manager, run, id, done, result] {
                if (const auto alive = run.lock(); alive && manager)
                    manager->guarded(id, [&] { done(result); });
            });
        };
    }
    sdk::RegistrationPtr add(const QString &kind, const QString &id, const QVariantMap &descriptor,
                             std::function<void(Run &)> install, std::function<void(Run &)> remove) {
        auto work = [manager = m_manager, kind, id, descriptor, install](const std::shared_ptr<Run> &run) {
            const auto added = manager->addRegistration(run, kind, id, descriptor);
            if (added)
                install(*run);
            else
                manager->recordLog(run->id, "error", added.error().message);
            return bool(added);
        };
        bool accepted = true;
        if (QThread::currentThread() == m_manager->thread()) {
            if (const auto run = m_run.lock())
                accepted = work(run);
            else
                accepted = false;
        } else
            onRun(work);
        if (!accepted)
            return nullptr;
        return std::make_unique<Handle>(m_manager, m_run, [kind, id, remove](Run &run) {
            run.registrations[kind].remove(id);
            remove(run);
        });
    }
    void secret(const QString &operation, const QString &key, const QString &value, IExtensionBackend::Completion done) {
        onRun([manager = m_manager, dataDirectory = m_dataDirectory, operation, key, value,
               done](const std::shared_ptr<Run> &run) {
            if (key.isEmpty() || key.size() > 200) {
                done(failure("Invalid secret key"));
                return;
            }
            const auto keysFile = QDir(dataDirectory).filePath("secret-keys.json");
            if (operation != "delete") {
                run->secretKeys.insert(key);
                writeJson(keysFile, QStringList(run->secretKeys.cbegin(), run->secretKeys.cend()));
            }
            if (!value.isEmpty())
                manager->m_secrets.insert(value);
            extensionSecret({{"extensionId", run->id}, {"key", key}, {"operation", operation}, {"value", value}},
                            [manager, run, keysFile, operation, key, done](Result<QVariantMap> result) {
                                if (operation == "delete") {
                                    run->secretKeys.remove(key);
                                    writeJson(keysFile, QStringList(run->secretKeys.cbegin(), run->secretKeys.cend()));
                                }
                                if (result && !result.value().value("value").toString().isEmpty())
                                    manager->m_secrets.insert(result.value().value("value").toString());
                                done(std::move(result));
                            });
        });
    }

    PluginManager *m_manager;
    std::weak_ptr<Run> m_run;
    const QString m_id, m_version, m_directory, m_dataDirectory;
    mutable QMutex m_mutex;
    QVariantMap m_config;
    std::atomic<quint64> m_nextSubscription{1'000'000};
};

template <class F> bool PluginManager::guarded(const QString &id, F &&call) {
    crashguard::Scope scope(id);
    QString error;
    try {
        call();
        return true;
    } catch (const std::exception &exception) {
        error = QStringLiteral("Unhandled exception: %1").arg(QString::fromLocal8Bit(exception.what()));
    } catch (...) {
        error = QStringLiteral("Unhandled exception");
    }
    // Fault from the event loop; the caller may still be iterating the run's registrations.
    post([this, id, error] { fault(id, error); });
    return false;
}

PluginManager::PluginManager(QObject *parent) : IExtensionBackend(parent) {
    m_audio = std::make_unique<AudioProxy>(
        [this](const QString &source, const QString &id, const QString &purpose, Completion done) {
            providerCall({{"source", source},
                          {"kind", "music"},
                          {"operation", purpose == "download" ? "download" : "resolve"},
                          {"params", QVariantMap{{"id", id}, {"purpose", purpose}}}},
                         std::move(done));
        });
}
PluginManager::~PluginManager() { shutdown(); }

QString PluginManager::root() const { return AppPaths::extensionsDirectory(); }
QString PluginManager::dataDirectory(const QString &id) const { return QDir(root()).filePath("data/" + id); }

void PluginManager::post(std::function<void()> work) {
    if (QThread::currentThread() == thread())
        work();
    else
        QMetaObject::invokeMethod(this, std::move(work), Qt::QueuedConnection);
}
Result<void> PluginManager::addRegistration(const std::shared_ptr<Run> &run, const QString &kind, const QString &id,
                                            QVariantMap descriptor) {
    if (run->state == "stopping")
        return failure("Extension is stopping");
    if (!registrationKinds.contains(kind) || !validExtensionId(id))
        return failure(QStringLiteral("Invalid registration: %1/%2").arg(kind, id));
    bool contributed = false;
    for (const auto &item : m_entries.value(run->id).manifest.contributes().value(kind).toList())
        contributed = contributed || item.toMap().value("id").toString() == id;
    if (contributed || run->registrations.value(kind).contains(id))
        return failure(QStringLiteral("Duplicate registration: %1/%2").arg(kind, id));
    if (sourceKinds.contains(kind))
        if (auto source = containedFile(run->directory, descriptor.value("source").toString()); !source)
            return source.error();
    if (kind == "themes")
        if (auto valid = themes::validate(descriptor, run->directory); !valid)
            return valid.error();
    descriptor.insert("id", run->id + "/" + id);
    descriptor.insert("localId", id);
    descriptor.insert("extensionId", run->id);
    run->registrations[kind].insert(id, descriptor);
    notifyChanged();
    return {};
}
void PluginManager::notifyChanged() {
    if (m_closing || m_changePending)
        return;
    m_changePending = true;
    QTimer::singleShot(0, this, [this] {
        m_changePending = false;
        if (m_closing)
            return;
        emit changed();
        auto event = snapshot();
        event.insert("event", "extensions.changed");
        emit eventReady(event);
    });
}
void PluginManager::reportError(const QString &message) {
    emit eventReady({{"event", "extensions.error"}, {"message", redact(message)}});
}
void PluginManager::reloading(const QStringList &ids, bool active) {
    emit eventReady({{"event", "extensions.reloading"}, {"ids", ids}, {"active", active}});
}
QString PluginManager::redact(QString message) const {
    for (const auto &secret : m_secrets)
        if (!secret.isEmpty())
            message.replace(secret, "[redacted]");
    return message;
}
void PluginManager::recordLog(const QString &id, const QString &level, const QString &message) {
    auto &list = m_logs[id];
    list.append({{"time", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
                 {"level", level},
                 {"message", redact(message).left(16'384)}});
    if (list.size() > 500)
        list.remove(0, list.size() - 500);
}

QVariantMap PluginManager::publicEntry(const QString &id) const {
    const auto &entry = m_entries[id];
    const auto run = m_runs.value(id);
    auto contributions = entry.manifest.contributes();
    if (run)
        for (const auto &kind : uiKinds) {
            auto list = contributions.value(kind).toList();
            for (const auto &item : run->registrations.value(kind))
                list.append(item);
            contributions.insert(kind, list);
        }
    for (auto it = contributions.begin(); it != contributions.end(); ++it) {
        if (it.value().typeId() != QMetaType::QVariantList)
            continue;
        auto list = it.value().toList();
        for (auto &value : list) {
            auto item = value.toMap();
            if (!item.contains("localId"))
                item.insert("localId", item.value("id"));
            item.insert("id", id + "/" + item.value("localId").toString());
            item.insert("extensionId", id);
            if (item.contains("source"))
                item.insert("source", QDir::cleanPath(QDir(entry.directory).filePath(item.value("source").toString())));
            value = item;
        }
        it.value() = list;
    }
    const bool running = run && run->state == "running";
    QVariantMap registrations;
    if (running)
        for (const auto &kind : registrationKinds) {
            QVariantList list;
            for (const auto &item : run->registrations.value(kind))
                list.append(item);
            registrations.insert(kind, list);
        }
    const auto &data = entry.manifest.data;
    return {{"id", id},
            {"name", data.value("name")},
            {"version", data.value("version")},
            {"description", data.value("description", QString())},
            {"directory", entry.directory},
            {"development", entry.development},
            {"trusted", entry.trusted},
            {"enabled", entry.enabled},
            {"state", run ? run->state : entry.state},
            {"error", redact(entry.error)},
            {"generation", run ? run->generation : 0},
            {"nativeModules", data.value("nativeModules").toBool()},
            {"type", data.value("type", "extension")},
            {"hasCode", entry.manifest.needsTrust()},
            {"themes", contributions.value("themes", QVariantList{})},
            {"contributes", running ? contributions : QVariantMap{}},
            {"registrations", registrations}};
}
QVariantMap PluginManager::snapshot() const {
    QVariantList extensions;
    for (const auto &id : m_order)
        extensions.append(publicEntry(id));
    return {{"extensions", extensions},
            {"selections", m_selections},
            {"safeMode", m_safeMode},
            {"catalogueReady", m_catalogueReady},
            {"startupComplete", m_startupComplete},
            {"runtimeReady", m_catalogueReady && !m_closing}};
}

void PluginManager::load() {
    // Unknown keys (bundledSeen, ...) are kept so switching runtimes does not lose state.
    m_extraState = readJson(QDir(root()).filePath("registry.json"), QVariantMap{}).toMap();
    m_selections = m_extraState.take("selections").toMap();
    m_developmentDirectories = m_extraState.take("developmentDirectories").toStringList();
    for (const auto &value : m_extraState.take("entries").toList()) {
        const auto stored = value.toMap();
        const auto id = stored.value("id").toString();
        if (!validExtensionId(id) || m_entries.contains(id))
            continue;
        Entry entry;
        entry.directory = stored.value("directory").toString();
        entry.development = stored.value("development").toBool();
        entry.enabled = stored.value("enabled").toBool();
        entry.trusted = stored.value("trusted").toBool();
        entry.previous = stored.value("previous").toMap();
        entry.error = stored.value("failure").toString();
        entry.state = entry.error.isEmpty() ? "disabled" : "failed";
        auto manifest = readManifest(entry.directory);
        if (manifest && manifest.value().id() == id)
            entry.manifest = manifest.value();
        else {
            entry.manifest.data = {{"id", id}, {"name", id}, {"version", "0.0.0"}};
            entry.state = "failed";
            entry.error = manifest ? QStringLiteral("Installed extension ID changed") : manifest.error().message;
        }
        m_entries.insert(id, entry);
        m_order.append(id);
    }
}
Result<void> PluginManager::save() {
    QVariantList entries;
    for (const auto &id : std::as_const(m_order)) {
        const auto &entry = m_entries[id];
        if (entry.development && !m_developmentDirectories.contains(entry.directory))
            m_developmentDirectories.append(entry.directory);
        if (entry.previous.value("development").toBool() &&
            !m_developmentDirectories.contains(entry.previous.value("directory").toString()))
            m_developmentDirectories.append(entry.previous.value("directory").toString());
        QVariantMap stored{{"id", id},
                           {"directory", entry.directory},
                           {"development", entry.development},
                           {"enabled", entry.enabled},
                           {"trusted", entry.trusted}};
        if (!entry.previous.isEmpty())
            stored.insert("previous", entry.previous);
        if (entry.state == "failed")
            stored.insert("failure", redact(entry.error));
        entries.append(stored);
    }
    auto state = m_extraState;
    state.insert("entries", entries);
    state.insert("selections", m_selections);
    state.insert("developmentDirectories", m_developmentDirectories);
    return writeJson(QDir(root()).filePath("registry.json"), state);
}

void PluginManager::enqueue(Mutation mutation, Completion done) {
    m_mutations.emplace_back(std::move(mutation), std::move(done));
    // Always start from the event loop so lifecycle work never runs inside extension code.
    QTimer::singleShot(0, this, &PluginManager::runMutations);
}
void PluginManager::runMutations() {
    if (m_mutating || m_mutations.empty())
        return;
    m_mutating = true;
    auto [mutation, done] = std::move(m_mutations.front());
    m_mutations.pop_front();
    QPointer<PluginManager> guard(this);
    mutation([guard, done](Result<QVariantMap> result) {
        // Only collect after the complete operation, including dependent restarts and rollback.
        if (guard)
            guard->cleanUnusedPackages();
        done(std::move(result));
        if (!guard)
            return;
        guard->m_mutating = false;
        QTimer::singleShot(0, guard, &PluginManager::runMutations);
    });
}

void PluginManager::start(HostCall hostCall) {
    m_hostCall = std::move(hostCall);
    m_safeMode = qEnvironmentVariable("NEKOTUNE_SAFE_MODE") == "1";
    QDir().mkpath(root());
    const auto sentinel = QDir(root()).filePath("crash-sentinel");
    const auto crashed = crashguard::takeSentinel(sentinel);
    crashguard::install(sentinel);
    // Development copies from the previous process are no longer mapped.
    QDir(QDir(root()).filePath("runtime")).removeRecursively();
    load();
    if (m_entries.contains(crashed)) {
        auto &entry = m_entries[crashed];
        entry.enabled = false;
        entry.state = "failed";
        entry.error = "The backend crashed while this extension was running; it has been disabled";
        if (auto saved = save(); !saved)
            reportError("Cannot persist crashed extension state: " + saved.error().message);
        reportError(QStringLiteral("%1: %2").arg(crashed, entry.error));
    }
    m_catalogueReady = true;
    notifyChanged();
    enqueue(
        [this](Completion done) {
            if (m_safeMode) {
                done(QVariantMap{});
                return;
            }
            QStringList order;
            QSet<QString> selectedOwners;
            for (const auto &value : std::as_const(m_selections))
                selectedOwners.insert(value.toString().section('/', 0, 0));
            for (const auto &id : std::as_const(m_order))
                if (selectedOwners.contains(id))
                    order.append(id);
            for (const auto &id : std::as_const(m_order))
                if (!selectedOwners.contains(id))
                    order.append(id);
            startup(0, order, [done](Result<void>) { done(QVariantMap{}); });
        },
        [this](Result<QVariantMap>) {
            m_startupComplete = true;
            notifyChanged();
        });
}
void PluginManager::startup(int, QStringList order, Done finished) {
    sequence(
        order,
        [this](const QString &id, Done next) {
            if (!m_entries.contains(id) || !m_entries[id].enabled || m_entries[id].state == "failed") {
                next({});
                return;
            }
            const auto block = [this, id](const QString &message) {
                auto &entry = m_entries[id];
                if (entry.state != "failed")
                    entry.state = "blocked";
                entry.error = message;
                notifyChanged();
            };
            const auto dependencies = dependencyOrder(id);
            if (!dependencies) {
                block(dependencies.error().message);
                next({});
                return;
            }
            for (const auto &dependency : dependencies.value())
                if (!m_entries[dependency].enabled || m_entries[dependency].state == "failed") {
                    block("Dependency is disabled or failed; enable it manually");
                    next({});
                    return;
                }
            enable(id, false, [block, next](Result<void> result) {
                if (!result)
                    block(result.error().message);
                next({});
            });
        },
        false, std::move(finished));
}
void PluginManager::shutdown() {
    if (m_closing)
        return;
    m_closing = true;
    m_audio->close();
    for (auto it = m_order.crbegin(); it != m_order.crend(); ++it)
        stop(*it);
    const auto mutations = std::exchange(m_mutations, {});
    for (const auto &[mutation, done] : mutations)
        done(failure("Extension runtime stopped"));
}

Result<QStringList> PluginManager::dependencyOrder(const QString &id) const {
    QStringList result, visiting;
    std::function<Result<void>(const QString &)> visit = [&](const QString &current) -> Result<void> {
        if (visiting.contains(current))
            return failure("Dependency cycle: " + (visiting + QStringList{current}).join(" → "));
        if (result.contains(current))
            return {};
        if (!m_entries.contains(current))
            return failure("Missing extension: " + current);
        visiting.append(current);
        const auto dependencies = m_entries[current].manifest.dependencies();
        for (auto it = dependencies.cbegin(); it != dependencies.cend(); ++it) {
            const auto range = it.value().toString();
            if (!m_entries.contains(it.key()) ||
                !semver::satisfies(m_entries[it.key()].manifest.version(), range))
                return failure(QStringLiteral("Dependency %1 must satisfy %2").arg(it.key(), range));
            if (auto nested = visit(it.key()); !nested)
                return nested;
        }
        visiting.removeLast();
        result.append(current);
        return {};
    };
    if (auto visited = visit(id); !visited)
        return visited.error();
    return result;
}
QStringList PluginManager::dependents(const QString &id) const {
    QStringList found{id};
    qsizetype size;
    do {
        size = found.size();
        for (const auto &candidate : m_order) {
            if (found.contains(candidate))
                continue;
            const auto dependencies = m_entries[candidate].manifest.dependencies();
            for (const auto &dependency : found)
                if (dependencies.contains(dependency)) {
                    found.append(candidate);
                    break;
                }
        }
    } while (size != found.size());
    return found;
}

void PluginManager::enable(const QString &id, bool trusted, Done done) {
    if (m_safeMode) {
        done(failure("Restart without --safe-mode to enable extensions"));
        return;
    }
    if (!m_entries.contains(id)) {
        done(failure("Extension not found"));
        return;
    }
    if (trusted)
        m_entries[id].trusted = true;
    QSet<QString> visited;
    std::function<Result<void>(const QString &)> inspect = [&](const QString &candidate) -> Result<void> {
        if (visited.contains(candidate))
            return {};
        visited.insert(candidate);
        if (!m_entries.contains(candidate))
            return failure("Dependency missing: " + candidate);
        auto &installed = m_entries[candidate];
        auto manifest = readManifest(installed.directory);
        if (!manifest)
            return manifest.error();
        if (manifest.value().id() != candidate)
            return failure("Manifest ID changed; reinstall the extension");
        installed.manifest = manifest.value();
        const auto dependencies = installed.manifest.dependencies();
        for (auto it = dependencies.cbegin(); it != dependencies.cend(); ++it)
            if (auto nested = inspect(it.key()); !nested)
                return nested;
        return {};
    };
    if (auto inspected = inspect(id); !inspected) {
        done(inspected);
        return;
    }
    if (m_entries[id].manifest.needsTrust() && !m_entries[id].trusted) {
        done(failure("Trust must be explicitly granted to " + id));
        return;
    }
    const auto order = dependencyOrder(id);
    if (!order) {
        done(order.error());
        return;
    }
    for (const auto &dependency : order.value())
        if (m_entries[dependency].manifest.needsTrust() && !m_entries[dependency].trusted) {
            done(failure("Enable and trust dependency first: " + dependency));
            return;
        }
    startChain(order.value(), 0, [this, done](Result<void> result) {
        if (result)
            if (auto saved = save(); !saved)
                result = saved;
        notifyChanged();
        done(result);
    });
}
void PluginManager::startChain(QStringList order, int index, Done done) {
    if (index >= order.size()) {
        done({});
        return;
    }
    const auto id = order[index];
    startOne(id, [this, order, index, id, done](Result<void> result) {
        if (!m_entries.contains(id)) {
            done(failure("Extension not found"));
            return;
        }
        auto &candidate = m_entries[id];
        if (!result) {
            if (candidate.previous.isEmpty()) {
                save();
                done(result);
                return;
            }
            stop(id);
            Entry restored;
            restored.directory = candidate.previous.value("directory").toString();
            restored.development = candidate.previous.value("development").toBool();
            restored.trusted = candidate.trusted;
            auto manifest = readManifest(restored.directory);
            if (!manifest) {
                save();
                done(failure("Activation failed and rollback is unavailable: " + manifest.error().message));
                return;
            }
            restored.manifest = manifest.value();
            m_entries[id] = restored;
            const auto message = result.error().message;
            startOne(id, [this, id, message, done](Result<void> restarted) {
                if (restarted && m_entries.contains(id))
                    m_entries[id].enabled = true;
                save();
                notifyChanged();
                done(failure("Activation rolled back: " + message));
            });
            return;
        }
        candidate.enabled = true;
        candidate.previous.clear();
        startChain(order, index + 1, done);
    });
}
Result<QString> PluginManager::loadablePath(const Entry &entry) {
    if (!entry.development)
        return entry.manifest.library;
    // Never map a development build in place: rebuilding it would corrupt the running image (and
    // Windows locks loaded DLLs), and a path that is already loaded cannot be loaded again. The
    // whole library directory is staged so sibling dependencies still resolve ($ORIGIN on Linux,
    // the DLL directory on Windows). Each distinct build gets its own copy.
    const QFileInfo library(entry.manifest.library);
    const QDir source = library.absoluteDir();
    QCryptographicHash hash(QCryptographicHash::Sha256);
    QStringList files;
    QDirIterator items(source.path(), QDir::Files | QDir::NoSymLinks, QDirIterator::Subdirectories);
    while (items.hasNext())
        files.append(source.relativeFilePath(items.next()));
    files.sort();
    if (files.size() > 256)
        return failure("Extension library directory has too many files");
    for (const auto &relative : std::as_const(files)) {
        QFile file(source.filePath(relative));
        if (!file.open(QIODevice::ReadOnly))
            return failure("Cannot read extension library: " + file.errorString(), ErrorCode::Io);
        hash.addData(relative.toUtf8());
        hash.addData(&file);
    }
    const QDir runtime(QDir(root()).filePath("runtime"));
    runtime.mkpath(".");
    const auto target = runtime.filePath(entry.manifest.id() + "-" + QString::fromLatin1(hash.result().toHex().left(16)));
    const auto staged = QDir(target).filePath(library.fileName());
    if (QFileInfo::exists(staged))
        return staged;
    const auto temporary = target + "." + QUuid::createUuid().toString(QUuid::WithoutBraces) + ".tmp";
    bool copied = QDir().mkpath(temporary);
    for (const auto &relative : std::as_const(files)) {
        const auto destination = QDir(temporary).filePath(relative);
        copied = copied && QDir().mkpath(QFileInfo(destination).absolutePath()) &&
                 QFile::copy(source.filePath(relative), destination);
    }
    if (!copied || !QDir().rename(temporary, target)) {
        QDir(temporary).removeRecursively();
        return failure("Cannot stage development library", ErrorCode::Io);
    }
    return staged;
}
void PluginManager::startOne(const QString &id, Done done) {
    auto &entry = m_entries[id];
    if (entry.manifest.needsTrust() && !entry.trusted) {
        done(failure("Trust must be explicitly granted to " + id));
        return;
    }
    if (const auto existing = m_runs.value(id)) {
        if (existing->state == "running")
            done({});
        else
            existing->waiters.append(std::move(done));
        return;
    }
    if (!entry.manifest.unavailable.isEmpty()) {
        entry.state = "failed";
        entry.error = entry.manifest.unavailable;
        entry.enabled = false;
        notifyChanged();
        done(failure(entry.error));
        return;
    }
    if (entry.manifest.data.value("nativeModules").toBool()) {
        m_nativeActivated.insert(id);
        m_retainedDirectories.insert(entry.directory);
    }
    QDir().mkpath(dataDirectory(id));
    QFile::setPermissions(dataDirectory(id), QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    entry.error.clear();
    entry.state = "starting";
    auto run = std::make_shared<Run>();
    run->id = id;
    run->directory = entry.directory;
    run->generation = ++m_generation;
    for (const auto &kind : registrationKinds)
        run->registrations.insert(kind, {});
    m_runs.insert(id, run);
    if (entry.manifest.library.isEmpty()) {
        run->state = "running";
        if (entry.development)
            watchDevelopment(run);
        notifyChanged();
        done({});
        return;
    }
    run->waiters.append(std::move(done));
    const auto path = loadablePath(entry);
    if (!path) {
        activated(run, path.error());
        return;
    }
    auto *loader = m_loaders.value(path.value());
    if (!loader) {
        loader = new QPluginLoader(path.value(), this);
        m_loaders.insert(path.value(), loader);
    }
    // metaData() reads the ELF/PE section without mapping the library or running its code.
    const auto iid = loader->metaData().value("IID").toString();
    if (iid != QStringLiteral(NEKOTUNE_EXTENSION_IID)) {
        activated(run, failure(iid.isEmpty() ? QStringLiteral("Library is not a Qt plugin: ") + loader->errorString()
                                             : QStringLiteral("Unsupported extension interface %1").arg(iid)));
        return;
    }
    // Once mapped, the package directory must survive until exit (Windows also locks the DLL).
    m_retainedDirectories.insert(entry.directory);
    bool loaded = false;
#ifdef Q_OS_WIN
    // Windows does not search a DLL's own directory for its dependencies. SetDllDirectory is
    // process-wide, so keep it only for this load on the backend thread.
    SetDllDirectoryW(reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(QFileInfo(path.value()).absolutePath()).utf16()));
    const auto restoreSearchPath = qScopeGuard([] { SetDllDirectoryW(nullptr); });
#endif
    if (!guarded(id, [&] { loaded = loader->load(); }))
        return;
    auto *plugin = loaded ? qobject_cast<sdk::IExtensionPlugin *>(loader->instance()) : nullptr;
    if (!plugin) {
        activated(run, failure(loaded ? QStringLiteral("Library does not implement IExtensionPlugin")
                                       : loader->errorString()));
        return;
    }
    const auto data = dataDirectory(id);
    run->storage = readJson(QDir(data).filePath("storage.json"), QVariantMap{}).toMap();
    for (const auto &key : readJson(QDir(data).filePath("secret-keys.json"), QVariantList{}).toStringList())
        run->secretKeys.insert(key);
    run->plugin = plugin;
    run->context = std::make_unique<Context>(this, run, entry, data, readConfig(id));
    std::weak_ptr<Run> weak = run;
    run->startTimer = std::make_unique<QTimer>();
    run->startTimer->setSingleShot(true);
    connect(run->startTimer.get(), &QTimer::timeout, this, [this, weak] {
        if (const auto alive = weak.lock(); alive && alive->state == "starting")
            activated(alive, failure("Activation timed out"));
    });
    run->startTimer->start(activationTimeout);
    notifyChanged();
    QPointer<PluginManager> guard(this);
    guarded(id, [&] {
        plugin->activate(*run->context, [guard, weak](Result<void> result) {
            if (!guard)
                return;
            guard->post([guard, weak, result] {
                if (const auto alive = weak.lock(); alive && guard && alive->state == "starting")
                    guard->activated(alive, result);
            });
        });
    });
}
void PluginManager::activated(const std::shared_ptr<Run> &run, Result<void> result) {
    if (run->startTimer)
        run->startTimer->stop();
    const auto waiters = std::exchange(run->waiters, {});
    if (result) {
        run->state = "running";
        if (m_entries.value(run->id).development)
            watchDevelopment(run);
        notifyChanged();
    } else {
        const auto message = result.error().message;
        recordLog(run->id, "error", message);
        stop(run->id);
        if (m_entries.contains(run->id)) {
            auto &entry = m_entries[run->id];
            entry.state = "failed";
            entry.error = message;
            entry.enabled = false;
        }
        notifyChanged();
    }
    for (const auto &waiter : waiters)
        waiter(result);
}
void PluginManager::stop(const QString &id) {
    const auto run = m_runs.value(id);
    if (!run || run->state == "stopping")
        return;
    run->state = "stopping";
    m_audio->revoke(id);
    run->startTimer.reset();
    run->debounce.reset();
    run->watcher.reset();
    if (run->plugin)
        guarded(id, [&] { run->plugin->deactivate(); });
    // Every remaining function object or provider pointer belongs to the plugin; drop them before
    // anything else can reach them.
    run->services.clear();
    run->commands.clear();
    run->music.clear();
    run->lyrics.clear();
    run->subscriptions.clear();
    for (auto &kind : run->registrations)
        kind.clear();
    for (const auto &call : std::exchange(run->calls, {}))
        finishCall(call, failure("Extension stopped"));
    for (const auto &waiter : std::exchange(run->waiters, {}))
        waiter(failure("Extension stopped"));
    m_runs.remove(id);
    if (m_entries.contains(id))
        m_entries[id].state = "disabled";
    notifyChanged();
}
void PluginManager::fault(const QString &id, const QString &message) {
    const auto run = m_runs.value(id);
    if (!run || run->state == "stopping" || !m_entries.contains(id))
        return;
    if (run->state == "starting") {
        activated(run, failure(message));
        return;
    }
    recordLog(id, "error", message);
    stop(id);
    auto &entry = m_entries[id];
    entry.error = message;
    entry.state = "failed";
    entry.enabled = false;
    auto others = dependents(id);
    for (auto it = others.crbegin(); it != others.crend(); ++it)
        if (*it != id) {
            stop(*it);
            m_entries[*it].state = "blocked";
            m_entries[*it].error = "Dependency unavailable: " + id;
        }
    if (auto saved = save(); !saved)
        reportError(QStringLiteral("Cannot persist disabled extension %1: %2").arg(id, saved.error().message));
    notifyChanged();
}
void PluginManager::disable(const QString &id) {
    const auto affected = dependents(id);
    for (auto it = affected.crbegin(); it != affected.crend(); ++it) {
        stop(*it);
        if (*it != id) {
            m_entries[*it].state = "blocked";
            m_entries[*it].error = "Dependency disabled: " + id;
        }
    }
    m_entries[id].enabled = false;
    notifyChanged();
}
void PluginManager::reload(const QString &id, Done done) {
    if (!m_entries.contains(id)) {
        done(failure("Extension not found"));
        return;
    }
    if (m_entries[id].manifest.data.value("nativeModules").toBool()) {
        done(failure("Native QML modules require application restart"));
        return;
    }
    const auto manifest = readManifest(m_entries[id].directory);
    if (!manifest) {
        done(manifest.error());
        return;
    }
    if (manifest.value().id() != id) {
        done(failure("Reload cannot change extension ID"));
        return;
    }
    const auto affected = dependents(id);
    reloading(affected, true);
    for (auto it = affected.crbegin(); it != affected.crend(); ++it)
        stop(*it);
    m_entries[id].manifest = manifest.value();
    sequence(
        affected,
        [this](const QString &dependent, Done next) {
            if (m_entries.value(dependent).enabled)
                enable(dependent, false, next);
            else
                next({});
        },
        true,
        [this, affected, done](Result<void> result) {
            reloading(affected, false);
            done(result);
        });
}
void PluginManager::install(const QVariantMap &params, Completion done) {
    const auto path = params.value("path").toString();
    if (path.isEmpty()) {
        done(failure("Local package path required"));
        return;
    }
    const bool development = params.value("development").toBool();
    const bool themeOnly = params.value("themeOnly").toBool() || path.endsWith(".nktheme.zip");
    QString directory;
    if (development) {
        directory = QFileInfo(path).canonicalFilePath();
        if (directory.isEmpty() || !QFileInfo(directory).isDir()) {
            done(failure("Extension directory not found"));
            return;
        }
    } else {
        // Unpack into a staging directory first; only a validated package gets its final name.
        const QDir packages(QDir(root()).filePath("packages"));
        packages.mkpath(".");
        const auto stage = packages.filePath(".staging-" + QUuid::createUuid().toString(QUuid::WithoutBraces));
        zip::Limits limits;
        if (themeOnly) {
            limits.archiveBytes = themes::archiveLimit;
            limits.unpackedBytes = themes::unpackedLimit;
        }
        const auto extracted = zip::extract(path, stage, limits);
        const auto staged = extracted ? readManifest(stage) : Result<ExtensionManifest>(extracted.error());
        if (!staged) {
            QDir(stage).removeRecursively();
            done(staged.error());
            return;
        }
        if (staged.value().isTheme() && QFileInfo(path).size() > themes::archiveLimit) {
            QDir(stage).removeRecursively();
            done(failure("Theme archive exceeds 20 MiB"));
            return;
        }
        directory = packages.filePath(QStringLiteral("%1-%2-%3").arg(staged.value().id(), staged.value().version(),
                                                                     QUuid::createUuid().toString(QUuid::WithoutBraces)));
        if (!QDir().rename(stage, directory)) {
            QDir(stage).removeRecursively();
            done(failure("Cannot move extension package into place", ErrorCode::Io));
            return;
        }
    }
    // Re-read at the final location: library paths in the manifest are absolute.
    auto read = readManifest(directory);
    if (!read) {
        if (!development)
            QDir(directory).removeRecursively();
        done(read.error());
        return;
    }
    const auto discard = [this, development, directory, done](const QString &message) {
        if (!development)
            QDir(directory).removeRecursively();
        done(failure(message));
    };
    const auto manifest = read.value();
    const auto id = manifest.id();
    if (params.contains("expectedId")) {
        const auto expected = params.value("expectedId").toString();
        if (!m_entries.contains(expected)) {
            discard("Extension to update is not installed");
            return;
        }
        if (id != expected) {
            discard("theme_update_id_mismatch");
            return;
        }
    }
    if (params.value("themeOnly").toBool() && !manifest.isTheme()) {
        discard("Use the extension manager to install executable extensions");
        return;
    }
    if (m_nativeActivated.contains(id)) {
        discard("Native QML modules were loaded; restart in safe mode before updating");
        return;
    }
    const bool hadOld = m_entries.contains(id);
    if (hadOld && !params.value("replace").toBool()) {
        discard("Extension already installed; set replace to update");
        return;
    }
    const auto old = m_entries.value(id);
    const auto affected = hadOld ? dependents(id) : QStringList{};
    if (!affected.isEmpty())
        reloading(affected, true);
    for (auto it = affected.crbegin(); it != affected.crend(); ++it)
        stop(*it);
    const bool trustRetained = hadOld && old.trusted && (old.manifest.needsTrust() || !manifest.themeHasCode);
    const bool codeNeedsTrust = manifest.isTheme() && manifest.themeHasCode && !trustRetained;
    Entry entry;
    entry.directory = directory;
    entry.development = development;
    entry.manifest = manifest;
    entry.trusted = trustRetained;
    // Importing code into a formerly data-only theme waits for an explicit trust decision.
    entry.enabled = hadOld && old.enabled && !codeNeedsTrust;
    if (hadOld && (!old.enabled || codeNeedsTrust))
        entry.previous = !old.previous.isEmpty() ? old.previous
                                                 : QVariantMap{{"directory", old.directory},
                                                               {"development", old.development},
                                                               {"enabled", false},
                                                               {"trusted", old.trusted}};
    m_entries.insert(id, entry);
    if (!hadOld)
        m_order.append(id);
    auto finish = [this, affected, id, done](Result<void> result) {
        if (!affected.isEmpty())
            reloading(affected, false);
        notifyChanged();
        if (!result)
            done(result.error());
        else
            done(QVariantMap{{"extension", publicEntry(id)}});
    };
    auto restartOthers = [this, affected, id](Done next) {
        QStringList others;
        for (const auto &other : affected)
            if (other != id && m_entries.value(other).enabled)
                others.append(other);
        sequence(others, [this](const QString &other, Done step) { enable(other, false, step); }, true, next);
    };
    auto rollback = [this, hadOld, old, id, restartOthers, finish](const QString &message) {
        stop(id);
        if (!hadOld) {
            m_entries.remove(id);
            m_order.removeAll(id);
            save();
            finish(failure("Update rolled back: " + message));
            return;
        }
        m_entries.insert(id, old);
        auto restart = [this, restartOthers, finish, message](Result<void>) {
            restartOthers([this, finish, message](Result<void>) {
                save();
                finish(failure("Update rolled back: " + message));
            });
        };
        if (old.enabled)
            enable(id, false, restart);
        else
            restart({});
    };
    auto activate = [this, id](Done next) {
        if (m_entries[id].enabled)
            enable(id, false, next);
        else
            next({});
    };
    activate([this, restartOthers, rollback, finish](Result<void> result) {
        if (!result) {
            rollback(result.error().message);
            return;
        }
        if (auto saved = save(); !saved) {
            rollback(saved.error().message);
            return;
        }
        restartOthers([rollback, finish](Result<void> restarted) {
            if (!restarted)
                rollback(restarted.error().message);
            else
                finish({});
        });
    });
}
void PluginManager::uninstall(const QVariantMap &params, Completion done) {
    const auto id = params.value("id").toString();
    if (!m_entries.contains(id)) {
        done(failure("Extension not found"));
        return;
    }
    if (m_entries[id].manifest.isTheme() && m_selections.value("theme").toString().startsWith(id + "/")) {
        const auto previous = m_selections;
        m_selections.insert("theme", QString());
        if (auto saved = save(); !saved) {
            m_selections = previous;
            done(saved.error());
            return;
        }
    }
    disable(id);
    const bool clearData = params.value("clearData").toBool();
    const auto keys = clearData ? readJson(QDir(dataDirectory(id)).filePath("secret-keys.json"), QVariantList{}).toStringList()
                                : QStringList{};
    sequence(
        keys,
        [id](const QString &key, Done next) {
            extensionSecret({{"extensionId", id}, {"key", key}, {"operation", "delete"}},
                            [next](Result<QVariantMap> result) {
                                next(result ? Result<void>() : Result<void>(result.error()));
                            });
        },
        true,
        [this, id, clearData, done](Result<void> cleared) {
            if (!cleared) {
                done(cleared.error());
                return;
            }
            m_entries.remove(id);
            m_order.removeAll(id);
            m_logs.remove(id);
            if (auto saved = save(); !saved) {
                done(saved.error());
                return;
            }
            if (clearData)
                QDir(dataDirectory(id)).removeRecursively();
            notifyChanged();
            done(snapshot());
        });
}
Result<QVariantMap> PluginManager::select(const QVariantMap &params) {
    if (params.value("slot").typeId() != QMetaType::QString || params.value("contribution").typeId() != QMetaType::QString)
        return failure("Invalid selection");
    const auto slot = params.value("slot").toString();
    const auto contribution = params.value("contribution").toString();
    if (!contribution.isEmpty()) {
        bool available = false;
        if (slot == "theme")
            for (const auto &theme : themes::catalog().value("themes").toList())
                available = available || theme.toMap().value("id") == contribution;
        for (const auto &value : snapshot().value("extensions").toList())
            for (const auto &item : value.toMap().value("contributes").toMap().value(slot == "theme" ? "themes" : "slots").toList()) {
                const auto map = item.toMap();
                if (map.value("id") == contribution && (slot == "theme" || map.value("slot") == slot))
                    available = true;
            }
        if (!available)
            return failure("Contribution is unavailable for this slot");
    }
    const auto previous = m_selections;
    m_selections.insert(slot, contribution);
    if (auto saved = save(); !saved) {
        m_selections = previous;
        return saved.error();
    }
    notifyChanged();
    return snapshot();
}
void PluginManager::cleanUnusedPackages() {
    const QDir packages(QDir(root()).filePath("packages"));
    const QFileInfo rootInfo(packages.path());
    if (!rootInfo.exists())
        return;
    if (rootInfo.isSymLink() || !rootInfo.isDir()) {
        reportError("Extension package cleanup skipped: packages is not a real directory");
        return;
    }
    // References on disk also protect the last committed version if saving a mutation failed.
    const auto stored = readJson(QDir(root()).filePath("registry.json"), QVariant()).toMap();
    if (!stored.value("entries").canConvert<QVariantList>())
        return;
    QStringList references = m_developmentDirectories + stored.value("developmentDirectories").toStringList();
    for (const auto &directory : std::as_const(m_retainedDirectories))
        references.append(directory);
    for (const auto &run : std::as_const(m_runs))
        references.append(run->directory);
    for (const auto &entry : std::as_const(m_entries))
        references << entry.directory << entry.previous.value("directory").toString();
    for (const auto &value : stored.value("entries").toList())
        references << value.toMap().value("directory").toString()
                   << value.toMap().value("previous").toMap().value("directory").toString();
    QSet<QString> retained;
    for (const auto &reference : std::as_const(references))
        if (!reference.isEmpty()) {
            // Keep both spellings: a development directory may point into a package via a link.
            retained.insert(QDir::cleanPath(QFileInfo(reference).absoluteFilePath()));
            if (const auto canonical = QFileInfo(reference).canonicalFilePath(); !canonical.isEmpty())
                retained.insert(canonical);
        }
    static const QString uuid = "[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}";
    static const QRegularExpression staging("^\\.staging-" + uuid + "$");
    static const QRegularExpression package("^([a-z][a-z0-9._-]{1,100})-(v?\\d+\\.\\d+\\.\\d+[^/]*)-" + uuid + "$");
    const auto base = rootInfo.canonicalFilePath();
    for (const auto &item : QDir(base).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden)) {
        const auto match = package.match(item.fileName());
        if (!staging.match(item.fileName()).hasMatch() &&
            !(match.hasMatch() && semver::parse(match.captured(2).startsWith('v') ? match.captured(2).mid(1) : match.captured(2))))
            continue;
        const auto target = base + "/" + item.fileName();
        bool keep = item.isSymLink();
        for (const auto &reference : std::as_const(retained))
            keep = keep || reference == target || reference.startsWith(target + "/") || target.startsWith(reference + "/");
        if (keep)
            continue;
        if (!QDir(target).removeRecursively())
            reportError("Cannot clean extension package " + target);
    }
}
Result<QVariantMap> PluginManager::exportTheme(const QVariantMap &params) {
    const auto id = params.value("contribution").toString().isEmpty() ? QStringLiteral("builtin/yozakura")
                                                                       : params.value("contribution").toString();
    const auto destination = params.value("path").toString();
    for (const auto &theme : themes::catalog().value("themes").toList())
        if (theme.toMap().value("id") == id)
            return themes::exportTheme(theme.toMap(), {}, {}, destination);
    const auto owner = id.section('/', 0, 0), local = id.section('/', 1);
    if (!m_entries.contains(owner))
        return failure("Theme is unavailable for export");
    const auto &entry = m_entries[owner];
    QVariantMap theme;
    for (const auto &value : entry.manifest.contributes().value("themes").toList())
        if (value.toMap().value("id") == local)
            theme = value.toMap();
    if (theme.isEmpty())
        if (const auto run = m_runs.value(owner))
            theme = run->registrations.value("themes").value(local);
    if (theme.isEmpty())
        return failure("Theme is unavailable for export");
    if (auto manifest = readManifest(entry.directory); !manifest)
        return manifest.error();
    if (auto valid = themes::validate(theme, entry.directory); !valid)
        return valid.error();
    return themes::exportTheme(theme, entry.directory, entry.manifest.data, destination);
}
QVariantMap PluginManager::readConfig(const QString &id) const {
    return readJson(QDir(dataDirectory(id)).filePath("config.json"), m_entries.value(id).manifest.data.value("config", QVariantMap{}))
        .toMap();
}
Result<QVariantMap> PluginManager::setConfig(const QString &id, const QVariant &value) {
    if (!m_entries.contains(id) || value.typeId() != QMetaType::QVariantMap ||
        QJsonDocument::fromVariant(value).toJson(QJsonDocument::Compact).size() > 256 * 1024)
        return failure("Invalid extension configuration");
    const auto config = value.toMap();
    if (auto saved = writeJson(QDir(dataDirectory(id)).filePath("config.json"), config); !saved)
        return saved.error();
    if (const auto run = m_runs.value(id); run && run->context) {
        run->context->setConfig(config);
        deliver(run, {{"event", "extension.config_changed"}, {"config", config}});
    }
    return QVariantMap{{"config", config}};
}

PluginManager::Completion PluginManager::pending(const std::shared_ptr<Run> &run, Completion done, int timeoutMs) {
    run->calls.removeIf([](const auto &call) { return call->fired.load(); });
    auto call = std::make_shared<PendingCall>();
    call->done = std::move(done);
    call->timer = new QTimer(this);
    call->timer->setSingleShot(true);
    connect(call->timer, &QTimer::timeout, this, [call] { finishCall(call, failure("Extension request timed out")); });
    call->timer->start(timeoutMs);
    run->calls.append(call);
    QPointer<PluginManager> guard(this);
    return [guard, call](Result<QVariantMap> result) {
        if (!guard || call->fired.load())
            return;
        guard->post([call, result] { finishCall(call, result); });
    };
}
void PluginManager::invoke(const QString &id, QString service, const QVariantMap &params, Completion done) {
    const auto run = m_runs.value(id);
    if (!run || run->state != "running") {
        done(failure("Extension is not running"));
        return;
    }
    if (service.isEmpty()) {
        done(failure("Service name required"));
        return;
    }
    if (service.startsWith(id + "/"))
        service = service.mid(id.size() + 1);
    const auto handler = run->services.value(service, run->commands.value(service));
    if (!handler) {
        done(failure("Unknown service/command: " + service));
        return;
    }
    const auto reply = pending(run, std::move(done), callTimeout);
    guarded(id, [&] { handler(params, reply); });
}
void PluginManager::providerCall(const QVariantMap &params, Completion done) {
    const auto source = params.value("source").toString();
    const auto slash = source.indexOf('/');
    const auto id = source.left(slash), local = source.mid(slash + 1);
    const auto kind = params.value("kind").toString();
    const auto run = slash > 0 ? m_runs.value(id) : nullptr;
    if (!run || run->state != "running" || !run->registrations.value(kind).contains(local)) {
        done(failure("Source is unavailable"));
        return;
    }
    const auto operation = params.value("operation").toString();
    const auto arguments = params.value("params").toMap();
    if (kind == "lyrics") {
        auto *provider = run->lyrics.value(local);
        if (operation != "search" && operation != "resolve") {
            done(failure("Unsupported provider operation: " + operation));
            return;
        }
        const auto reply = pending(run, std::move(done), callTimeout);
        guarded(id, [&] {
            if (operation == "search")
                provider->search(arguments, reply);
            else
                provider->resolve(arguments, reply);
        });
        return;
    }
    auto *provider = run->music.value(local);
    if (!provider || !QStringList{"search", "track", "resolve", "download", "downloaded"}.contains(operation)) {
        done(failure("Unsupported provider operation: " + operation));
        return;
    }
    const int timeout = operation == "download" ? 610'000 : operation == "downloaded" ? 120'000 : callTimeout;
    const auto reply = pending(run, std::move(done), timeout);
    const auto trackId = arguments.value("id").toString();
    guarded(id, [&] {
        if (operation == "search")
            provider->search(arguments.value("query").toString(), arguments.value("cursor").toString(),
                             [reply](Result<sdk::SearchPage> page) {
                                 if (!page) {
                                     reply(page.error());
                                     return;
                                 }
                                 QVariantList tracks;
                                 for (const auto &track : page.value().tracks)
                                     tracks.append(track.toParams());
                                 QVariantMap result{{"tracks", tracks}};
                                 if (!page.value().cursor.isEmpty())
                                     result.insert("cursor", page.value().cursor);
                                 reply(result);
                             });
        else if (operation == "track")
            provider->track(trackId, [reply](Result<sdk::Track> track) {
                reply(track ? Result<QVariantMap>(track.value().toParams()) : Result<QVariantMap>(track.error()));
            });
        else if (operation == "downloaded")
            provider->downloaded(trackId, arguments.value("path").toString(), arguments.value("song_id").toInt(),
                                 arguments.value("existing").toBool(), reply);
        else
            provider->resolve(trackId, operation == "download" ? QStringLiteral("download") : arguments.value("purpose", "play").toString(),
                              [reply](Result<sdk::ResolvedAudio> audio) {
                                  reply(audio ? Result<QVariantMap>(audio.value().toParams())
                                              : Result<QVariantMap>(audio.error()));
                              });
    });
}
void PluginManager::resolveAudio(const QVariantMap &params, Completion done) {
    const auto source = params.value("source").toString();
    const auto id = params.value("id").toString();
    const auto data = QFileInfo(dataDirectory(source.section('/', 0, 0))).canonicalFilePath();
    QPointer<PluginManager> guard(this);
    providerCall({{"source", source}, {"kind", "music"}, {"operation", "resolve"}, {"params", QVariantMap{{"id", id}, {"purpose", "play"}}}},
                 [guard, source, id, data, done](Result<QVariantMap> result) {
                     if (!guard)
                         return;
                     if (!result) {
                         done(result);
                         return;
                     }
                     const QUrl url(result.value().value("url").toString());
                     if (!url.isLocalFile()) {
                         guard->m_audio->lease(source, id, result.value(), done);
                         return;
                     }
                     // Prepared local files play directly but must stay inside the extension's data.
                     const auto file = QFileInfo(url.toLocalFile()).canonicalFilePath();
                     if (data.isEmpty() || file.isEmpty() || !file.startsWith(data + "/") || !QFileInfo(file).isFile()) {
                         done(failure("Extension audio files must be inside the extension data directory"));
                         return;
                     }
                     done(QVariantMap{{"url", QUrl::fromLocalFile(file).toString()}});
                 });
}

void PluginManager::deliver(const std::shared_ptr<Run> &run, const QVariantMap &event) {
    const auto name = event.value("event").toString();
    // Copy first: handlers may subscribe or dispose while running.
    const auto subscriptions = run->subscriptions;
    for (const auto &[key, subscription] : subscriptions)
        if (subscription.first == "*" || subscription.first == name)
            if (run->subscriptions.contains(key))
                guarded(run->id, [&] { subscription.second(event); });
}
void PluginManager::broadcast(const QVariantMap &event) {
    for (const auto &run : QList(m_runs.cbegin(), m_runs.cend()))
        if (run->state == "running")
            deliver(run, event);
}
void PluginManager::publish(const QVariantMap &event) {
    if (!m_closing)
        broadcast(event);
}
void PluginManager::watchDevelopment(const std::shared_ptr<Run> &run) {
    // QFileSystemWatcher is not recursive and reports content changes only for watched files.
    QStringList paths{run->directory};
    QDirIterator items(run->directory, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::NoSymLinks, QDirIterator::Subdirectories);
    while (items.hasNext() && paths.size() < 1024) {
        const auto path = items.next();
        if (!path.contains("/node_modules/") && !path.endsWith(".tmp"))
            paths.append(path);
    }
    run->watcher = std::make_unique<QFileSystemWatcher>(paths);
    run->debounce = std::make_unique<QTimer>();
    run->debounce->setSingleShot(true);
    run->debounce->setInterval(350);
    const auto id = run->id;
    connect(run->debounce.get(), &QTimer::timeout, this, [this, id] {
        enqueue(
            [this, id](Completion done) {
                reload(id, [done](Result<void> result) {
                    done(result ? Result<QVariantMap>(QVariantMap{}) : Result<QVariantMap>(result.error()));
                });
            },
            [this, id](Result<QVariantMap> result) {
                if (!result)
                    recordLog(id, "error", result.error().message);
            });
    });
    auto *debounce = run->debounce.get();
    connect(run->watcher.get(), &QFileSystemWatcher::directoryChanged, debounce, qOverload<>(&QTimer::start));
    connect(run->watcher.get(), &QFileSystemWatcher::fileChanged, debounce, qOverload<>(&QTimer::start));
}

void PluginManager::request(const QString &method, const QVariantMap &params, Completion done) {
    if (!m_catalogueReady || m_closing) {
        done(failure("Extension runtime is not available"));
        return;
    }
    const auto id = params.value("id").toString();
    if (method == "extensions.list")
        done(snapshot());
    else if (method == "extensions.logs") {
        QVariantList logs;
        for (auto item : m_logs.value(id)) {
            item.insert("message", redact(item.value("message").toString()));
            logs.append(item);
        }
        done(QVariantMap{{"logs", logs}});
    } else if (method == "extensions.get_config") {
        if (!m_entries.contains(id))
            done(failure("Extension not found"));
        else
            done(QVariantMap{{"config", readConfig(id)}});
    } else if (method == "extensions.call")
        invoke(id, params.value("service").toString(), params.value("params").toMap(), std::move(done));
    else if (method == "extensions.provider_call")
        providerCall(params, std::move(done));
    else if (method == "extensions.resolve_audio")
        resolveAudio(params, std::move(done));
    else if (method == "extensions.release_audio") {
        m_audio->release(params.value("url").toString());
        done(QVariantMap{});
    } else if (method == "extensions.cancel_download") {
        m_audio->cancelDownload(params.value("task").toString());
        done(QVariantMap{});
    } else if (method == "extensions.download_audio") {
        const auto source = params.value("source").toString();
        const auto task = params.value("task").toString();
        QPointer<PluginManager> guard(this);
        m_audio->download(
            source, params.value("id").toString(), params.value("base").toString(), task,
            dataDirectory(source.section('/', 0, 0)),
            [guard, task](qint64 received, qint64 total) {
                if (guard)
                    emit guard->eventReady({{"event", "music.download"},
                                            {"task", task},
                                            {"state", "running"},
                                            {"received", received},
                                            {"total", total}});
            },
            std::move(done));
    }
    else if (method == "extensions.download_completed")
        providerCall({{"source", params.value("source")}, {"kind", "music"}, {"operation", "downloaded"}, {"params", params}},
                     std::move(done));
    else
        enqueue(
            [this, method, params, id](Completion finish) {
                const auto reply = [this, finish](Result<void> result) {
                    if (result)
                        finish(snapshot());
                    else
                        finish(result.error());
                };
                if (method == "extensions.install")
                    install(params, finish);
                else if (method == "extensions.uninstall")
                    uninstall(params, finish);
                else if (method == "extensions.set_config")
                    finish(setConfig(id, params.value("config")));
                else if (method == "extensions.select")
                    finish(select(params));
                else if (method == "extensions.export_theme")
                    finish(exportTheme(params));
                else if (!m_entries.contains(id))
                    finish(failure(method == "extensions.enable" || method == "extensions.disable" ||
                                           method == "extensions.reload"
                                       ? "Extension not found"
                                       : "Unknown extension operation: " + method));
                else if (method == "extensions.enable")
                    enable(id, params.value("trusted").toBool(), [this, reply](Result<void> result) {
                        // Enabling a dependency may unblock extensions that were waiting for it.
                        QStringList blocked;
                        for (const auto &other : std::as_const(m_order))
                            if (m_entries[other].enabled && m_entries[other].state == "blocked")
                                blocked.append(other);
                        sequence(
                            blocked, [this](const QString &other, Done next) { enable(other, false, next); }, false,
                            [reply, result](Result<void>) { reply(result); });
                    });
                else if (method == "extensions.disable") {
                    disable(id);
                    reply(save());
                } else if (method == "extensions.reload")
                    reload(id, reply);
                else
                    finish(failure("Unknown extension operation: " + method));
            },
            std::move(done));
}
} // namespace nekotune
