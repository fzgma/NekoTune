#include "infrastructure/extensions/runtime/host_context.h"
#include "domain/audio/pcm_source.h"
#include "infrastructure/extensions/extension_secrets.h"
#include "infrastructure/extensions/host_gateway.h"
#include "infrastructure/extensions/package/json_file.h"
#include "infrastructure/extensions/plugin_manager.h"
#include <QDir>
#include <QJsonDocument>
#include <QPointer>
#include <QThread>

namespace nekotune {
class HostRegistration final : public sdk::Registration {
    using Run = ExtensionRuntime;

  public:
    HostRegistration(PluginManager *manager, std::weak_ptr<Run> run, std::function<void(Run &)> remove)
        : m_manager(manager), m_run(std::move(run)), m_remove(std::move(remove)) {}
    ~HostRegistration() override { dispose(); }
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

HostContext::HostContext(PluginManager *manager, std::weak_ptr<Run> run, QString id, QString version, QString directory,
                         QString dataDirectory, QVariantMap config)
    : m_manager(manager), m_run(std::move(run)), m_id(std::move(id)), m_version(std::move(version)),
      m_directory(std::move(directory)), m_dataDirectory(std::move(dataDirectory)), m_config(std::move(config)) {}

QString HostContext::id() const { return m_id; }

QString HostContext::version() const { return m_version; }

QString HostContext::directory() const { return m_directory; }

QString HostContext::dataDirectory() const { return m_dataDirectory; }

sdk::Params HostContext::config() const {
    QMutexLocker lock(&m_mutex);
    return m_config;
}

void HostContext::setConfig(const QVariantMap &config) {
    QMutexLocker lock(&m_mutex);
    m_config = config;
}

void HostContext::updateConfig(const sdk::Params &config, std::function<void(Result<void>)> done) {
    auto reply = forward(std::move(done));
    onRun([manager = m_manager, config, reply](const std::shared_ptr<Run> &run) {
        const auto result = manager->setConfig(run->id, config);
        reply(result ? Result<void>() : Result<void>(result.error()));
    });
}

void HostContext::hostCall(const QString &method, const sdk::Params &params, sdk::Reply done) {
    auto reply = forward(std::move(done));
    onRun([manager = m_manager, method, params, reply](const std::shared_ptr<Run> &run) {
        // Also keeps lifecycle mutations out: a starting extension holds the mutation queue.
        if (const auto allowed = checkHostCall(method); !allowed) {
            manager->recordLog(run->id, "error", allowed.error().message);
            reply(allowed.error());
        } else if (!manager->m_hostCall)
            reply(failure("Host unavailable"));
        else
            manager->m_hostCall(method, params, reply);
    });
}

sdk::RegistrationPtr HostContext::registerService(const QString &id, sdk::Handler handler) {
    return add(
        "services", id, {}, [id, handler](Run &run) { run.services.insert(id, handler); },
        [id](Run &run) { run.services.remove(id); });
}

void HostContext::callService(const QString &extension, const QString &service, const sdk::Params &params,
                              sdk::Reply done) {
    auto reply = forward(std::move(done));
    onRun([manager = m_manager, extension, service, params, reply](const std::shared_ptr<Run> &) {
        manager->invoke(extension, service, params, reply);
    });
}

sdk::RegistrationPtr HostContext::registerCommand(const QString &id, sdk::Handler handler,
                                                  const sdk::Params &descriptor) {
    return add(
        "commands", id, descriptor, [id, handler](Run &run) { run.commands.insert(id, handler); },
        [id](Run &run) { run.commands.remove(id); });
}

sdk::RegistrationPtr HostContext::registerUi(const QString &kind, const QString &id, const sdk::Params &descriptor) {
    if (!RegistrationScope::uiKinds().contains(kind)) {
        log(QtWarningMsg, "Unknown UI registration kind: " + kind);
        return nullptr;
    }
    return add(kind, id, descriptor, [](Run &) {}, [](Run &) {});
}

sdk::RegistrationPtr HostContext::registerMusicProvider(const QString &id, sdk::IMusicProvider *provider,
                                                        const sdk::Params &descriptor) {
    if (!provider)
        return nullptr;
    return add(
        "music", id, descriptor, [id, provider](Run &run) { run.music.insert(id, provider); },
        [id](Run &run) { run.music.remove(id); });
}

sdk::RegistrationPtr HostContext::registerLyricsProvider(const QString &id, sdk::ILyricsProvider *provider,
                                                         const sdk::Params &descriptor) {
    if (!provider)
        return nullptr;
    return add(
        "lyrics", id, descriptor, [id, provider](Run &run) { run.lyrics.insert(id, provider); },
        [id](Run &run) { run.lyrics.remove(id); });
}

sdk::RegistrationPtr HostContext::on(const QString &event, sdk::EventHandler handler) {
    // Reserve the key now so the handle can remove a subscription installed later.
    const auto key = m_nextSubscription.fetch_add(1);
    onRun([key, event, handler](const std::shared_ptr<Run> &run) {
        if (run->state != ExtensionState::Stopping)
            run->subscriptions.emplace(key, std::pair{event, handler});
    });
    return std::make_unique<HostRegistration>(m_manager, m_run, [key](Run &run) { run.subscriptions.erase(key); });
}

sdk::PcmCapabilities HostContext::pcmCapabilities() const {
    if (QThread::currentThread() != m_manager->thread() || !m_manager->m_pcm)
        return {false, {}, QStringLiteral("pcm_unavailable")};
    return m_manager->m_pcm->capabilities();
}

sdk::RegistrationPtr HostContext::subscribePcm(sdk::PcmHandler handler) {
    if (QThread::currentThread() != m_manager->thread() || !handler || !m_manager->m_pcm)
        return nullptr;
    const auto run = m_run.lock();
    if (!run || run->state == ExtensionState::Stopping)
        return nullptr;
    const auto key = m_nextSubscription.fetch_add(1);
    auto active = std::make_shared<std::atomic<bool>>(true);
    auto registration = m_manager->m_pcm->subscribe([manager = QPointer<PluginManager>(m_manager), weak = m_run,
                                                     id = m_id, active, handler](const sdk::PcmBlock &block) {
        const auto alive = weak.lock();
        if (manager && alive && alive->state == ExtensionState::Running && active->load())
            manager->guarded(id, [&] { handler(block); });
    });
    if (!registration)
        return nullptr;
    run->pcm.emplace(key, std::move(registration));
    class Subscription final : public sdk::Registration {
      public:
        Subscription(std::shared_ptr<std::atomic<bool>> active, sdk::RegistrationPtr handle)
            : active(std::move(active)), handle(std::move(handle)) {}
        ~Subscription() override { dispose(); }
        void dispose() override {
            *active = false;
            handle.reset();
        }

      private:
        std::shared_ptr<std::atomic<bool>> active;
        sdk::RegistrationPtr handle;
    };
    return std::make_unique<Subscription>(
        active, std::make_unique<HostRegistration>(m_manager, m_run, [key](Run &alive) { alive.pcm.erase(key); }));
}

sdk::RegistrationPtr HostContext::registerShuffleStrategy(const QString &id, sdk::IShuffleProvider *provider,
                                                          const sdk::Params &descriptor) {
    if (!provider || descriptor.value("name").toString().trimmed().isEmpty()) {
        log(QtWarningMsg, "Shuffle strategy requires a provider and a name");
        return nullptr;
    }
    return add(
        "shuffle", id, descriptor,
        [id, provider, manager = m_manager](Run &run) {
            run.shuffle.insert(id, {provider, ++manager->m_generation, false});
        },
        [id](Run &run) { run.shuffle.remove(id); });
}

void HostContext::publish(const QString &event, const sdk::Params &data) {
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

void HostContext::storageGet(const QString &key, std::function<void(Result<QVariant>)> done) {
    auto reply = forward(std::move(done));
    onRun([key, reply](const std::shared_ptr<Run> &run) {
        if (key.size() > 256)
            reply(failure("Invalid storage key"));
        else
            reply(run->storage.value(key));
    });
}

void HostContext::storageSet(const QString &key, const QVariant &value, std::function<void(Result<void>)> done) {
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
        const auto saved = writeJsonFile(QDir(dataDirectory).filePath("storage.json"), next);
        if (saved)
            run->storage = next;
        reply(saved);
    });
}

void HostContext::secretGet(const QString &key, std::function<void(Result<QString>)> done) {
    auto reply = forward(std::move(done));
    secret("get", key, {}, [reply](Result<QVariantMap> result) {
        if (!result)
            reply(result.error());
        else
            reply(result.value().value("value").toString());
    });
}

void HostContext::secretSet(const QString &key, const QString &value, std::function<void(Result<void>)> done) {
    auto reply = forward(std::move(done));
    secret("set", key, value,
           [reply](Result<QVariantMap> result) { reply(result ? Result<void>() : Result<void>(result.error())); });
}

void HostContext::secretDelete(const QString &key, std::function<void(Result<void>)> done) {
    auto reply = forward(std::move(done));
    secret("delete", key, {},
           [reply](Result<QVariantMap> result) { reply(result ? Result<void>() : Result<void>(result.error())); });
}

void HostContext::reportTask(const QString &task, const sdk::Params &progress) {
    auto data = progress;
    data.insert("task", task);
    publish("task", data);
}

void HostContext::log(QtMsgType level, const QString &message) {
    const QString name = level == QtDebugMsg     ? "debug"
                         : level == QtInfoMsg    ? "info"
                         : level == QtWarningMsg ? "warn"
                                                 : "error";
    onRun([manager = m_manager, name, message](const std::shared_ptr<Run> &run) {
        manager->recordLog(run->id, name, message);
    });
}

void HostContext::onRun(std::function<void(const std::shared_ptr<Run> &)> work) const {
    m_manager->post([run = m_run, work] {
        if (const auto alive = run.lock())
            work(alive);
    });
}

template <class T> std::function<void(Result<T>)> HostContext::forward(std::function<void(Result<T>)> done) const {
    return [manager = QPointer<PluginManager>(m_manager), run = m_run, id = m_id, done](Result<T> result) {
        if (!manager || !done)
            return;
        manager->post([manager, run, id, done, result] {
            if (const auto alive = run.lock(); alive && manager)
                manager->guarded(id, [&] { done(result); });
        });
    };
}

sdk::RegistrationPtr HostContext::add(const QString &kind, const QString &id, const QVariantMap &descriptor,
                                      std::function<void(Run &)> install, std::function<void(Run &)> remove) {
    auto work = [manager = m_manager, kind, id, descriptor, install](const std::shared_ptr<Run> &run) {
        const auto added = manager->addRegistration(run, kind, id, descriptor);
        if (added) {
            install(*run);
            if (kind == "shuffle")
                emit manager->shuffleCatalogueChanged();
        } else
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
    return std::make_unique<HostRegistration>(m_manager, m_run, [kind, id, remove, manager = m_manager](Run &run) {
        run.registrations.remove(kind, id);
        remove(run);
        if (kind == "shuffle")
            emit manager->shuffleCatalogueChanged();
    });
}

void HostContext::secret(const QString &operation, const QString &key, const QString &value,
                         IExtensionBackend::Completion done) {
    onRun([manager = m_manager, dataDirectory = m_dataDirectory, operation, key, value,
           done](const std::shared_ptr<Run> &run) {
        if (key.isEmpty() || key.size() > 200) {
            done(failure("Invalid secret key"));
            return;
        }
        const auto keysFile = QDir(dataDirectory).filePath("secret-keys.json");
        if (operation != "delete") {
            run->secretKeys.insert(key);
            writeJsonFile(keysFile, QStringList(run->secretKeys.cbegin(), run->secretKeys.cend()));
        }
        if (!value.isEmpty())
            manager->m_secrets.insert(value);
        extensionSecret({{"extensionId", run->id}, {"key", key}, {"operation", operation}, {"value", value}},
                        [manager, run, keysFile, operation, key, done](Result<QVariantMap> result) {
                            if (operation == "delete") {
                                run->secretKeys.remove(key);
                                writeJsonFile(keysFile, QStringList(run->secretKeys.cbegin(), run->secretKeys.cend()));
                            }
                            if (result && !result.value().value("value").toString().isEmpty())
                                manager->m_secrets.insert(result.value().value("value").toString());
                            done(std::move(result));
                        });
    });
}

ExtensionRuntime::~ExtensionRuntime() = default;
} // namespace nekotune
