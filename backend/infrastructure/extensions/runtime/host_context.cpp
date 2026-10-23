#include "infrastructure/extensions/runtime/host_context.h"
#include "domain/audio/pcm_source.h"
#include "infrastructure/extensions/host_gateway.h"
#include "infrastructure/extensions/plugin_manager.h"
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

HostContext::HostContext(PluginManager *manager, std::weak_ptr<Run> run, QString id, QString version,
                         QString directory, QString dataDirectory, QVariantMap config)
    : m_manager(manager), m_run(std::move(run)),
      m_info{std::move(id), std::move(version), std::move(directory), std::move(dataDirectory)},
      m_config(std::move(config)) {}

std::unique_ptr<sdk::ThreadAttribution> HostContext::attributeThread() {
    class Attribution final : public sdk::ThreadAttribution {
      public:
        explicit Attribution(const QString &id) : m_scope(id) {}

      private:
        crashguard::Scope m_scope;
    };
    return std::make_unique<Attribution>(m_info.id);
}

void HostContext::setConfig(const QVariantMap &config) {
    QMutexLocker lock(&m_mutex);
    m_config = config;
}

void HostContext::HostFacet::call(const QString &method, const sdk::Params &params, sdk::Reply done) {
    auto reply = m_context.forward(std::move(done));
    m_context.onRun([manager = m_context.m_manager, method, params, reply](const std::shared_ptr<Run> &run) {
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

sdk::RegistrationPtr HostContext::EventsFacet::on(const QString &event, sdk::EventHandler handler) {
    // Reserve the key now so the handle can remove a subscription installed later.
    const auto key = m_context.m_nextSubscription.fetch_add(1);
    m_context.onRun([key, event, handler, manager = m_context.m_manager](const std::shared_ptr<Run> &run) {
        if (run->state != ExtensionState::Stopping)
            manager->m_bus.subscribe(run->id, key, event, handler);
    });
    return std::make_unique<HostRegistration>(
        m_context.m_manager, m_context.m_run,
        [key, manager = m_context.m_manager](Run &run) { manager->m_bus.unsubscribe(run.id, key); });
}

void HostContext::EventsFacet::publish(const QString &event, const sdk::Params &data) {
    m_context.onRun([manager = m_context.m_manager, event, data](const std::shared_ptr<Run> &run) {
        if (!validExtensionId(event)) {
            manager->recordLog(run->id, "error", "Invalid event name: " + event);
            return;
        }
        const QVariantMap message{{"event", QStringLiteral("extension.%1.%2").arg(run->id, event)},
                                  {"extensionId", run->id},
                                  {"data", data}};
        manager->m_bus.broadcast(message);
        emit manager->eventReady(message);
    });
}

sdk::RegistrationPtr HostContext::ServicesFacet::add(const QString &id, sdk::Handler handler) {
    return m_context.add(
        "services", id, {},
        [id, handler, manager = m_context.m_manager](Run &run) {
            manager->m_bus.addService(run.id, id, handler);
        },
        [id, manager = m_context.m_manager](Run &run) { manager->m_bus.removeService(run.id, id); });
}

void HostContext::ServicesFacet::call(const QString &extension, const QString &service,
                                      const sdk::Params &params, sdk::Reply done) {
    auto reply = m_context.forward(std::move(done));
    m_context.onRun(
        [manager = m_context.m_manager, extension, service, params, reply](const std::shared_ptr<Run> &) {
            manager->m_bus.invoke(extension, service, params, reply);
        });
}

sdk::RegistrationPtr HostContext::CommandsFacet::add(const QString &id, sdk::Handler handler,
                                                     const sdk::CommandInfo &info) {
    sdk::Params descriptor{{"title", info.title}};
    if (!info.shortcut.isEmpty())
        descriptor.insert("shortcut", info.shortcut);

    return m_context.add(
        "commands", id, descriptor,
        [id, handler, manager = m_context.m_manager](Run &run) {
            manager->m_bus.addCommand(run.id, id, handler);
        },
        [id, manager = m_context.m_manager](Run &run) { manager->m_bus.removeCommand(run.id, id); });
}

sdk::RegistrationPtr HostContext::UiFacet::add(const QString &kind, const QString &id,
                                               const sdk::Params &descriptor) {
    if (!RegistrationScope::uiKinds().contains(kind)) {
        m_context.log().write(QtWarningMsg, "Unknown UI registration kind: " + kind);
        return nullptr;
    }
    return m_context.add(kind, id, descriptor, [](Run &) {}, [](Run &) {});
}

sdk::RegistrationPtr HostContext::MusicFacet::add(const QString &id, sdk::IMusicProvider *provider,
                                                  const sdk::MusicSourceInfo &info) {
    sdk::Params descriptor{{"name", info.name}, {"download", info.download}};
    if (!info.page.isEmpty())
        descriptor.insert("page", info.page);

    if (!provider)
        return nullptr;
    return m_context.add(
        "music", id, descriptor,
        [id, provider, manager = m_context.m_manager](Run &run) {
            manager->m_music.add(run.id, id, provider, run.registrations.descriptor("music", id));
        },
        [id, manager = m_context.m_manager](Run &run) { manager->m_music.remove(run.id, id); });
}

sdk::RegistrationPtr HostContext::LyricsFacet::add(const QString &id, sdk::ILyricsProvider *provider,
                                                   const sdk::LyricsSourceInfo &info) {
    const sdk::Params descriptor{{"name", info.name}};

    if (!provider)
        return nullptr;
    return m_context.add(
        "lyrics", id, descriptor,
        [id, provider, manager = m_context.m_manager](Run &run) {
            manager->m_lyrics.add(run.id, id, provider, run.registrations.descriptor("lyrics", id));
        },
        [id, manager = m_context.m_manager](Run &run) { manager->m_lyrics.remove(run.id, id); });
}

sdk::RegistrationPtr HostContext::ShuffleFacet::add(const QString &id, sdk::IShuffleProvider *provider,
                                                    const sdk::ShuffleInfo &info) {
    const sdk::Params descriptor{{"name", info.name}};

    if (!provider || descriptor.value("name").toString().trimmed().isEmpty()) {
        m_context.log().write(QtWarningMsg, "Shuffle strategy requires a provider and a name");
        return nullptr;
    }
    return m_context.add(
        "shuffle", id, descriptor,
        [id, provider, manager = m_context.m_manager](Run &run) {
            manager->m_shuffle.add(
                run.id, id, provider, run.registrations.descriptor("shuffle", id).value("name").toString());
        },
        [id, manager = m_context.m_manager](Run &run) { manager->m_shuffle.remove(run.id, id); });
}

sdk::PcmCapabilities HostContext::PcmFacet::capabilities() const {
    if (QThread::currentThread() != m_context.m_manager->thread() || !m_context.m_manager->m_pcm)
        return {false, {}, QStringLiteral("pcm_unavailable")};
    return m_context.m_manager->m_pcm->capabilities();
}

sdk::RegistrationPtr HostContext::PcmFacet::subscribe(sdk::PcmHandler handler) {
    if (QThread::currentThread() != m_context.m_manager->thread() || !handler || !m_context.m_manager->m_pcm)
        return nullptr;
    const auto run = m_context.m_run.lock();
    if (!run || run->state == ExtensionState::Stopping)
        return nullptr;
    const auto key = m_context.m_nextSubscription.fetch_add(1);
    auto active = std::make_shared<std::atomic<bool>>(true);
    auto registration = m_context.m_manager->m_pcm->subscribe(
        [manager = QPointer<PluginManager>(m_context.m_manager), weak = m_context.m_run,
         id = m_context.m_info.id, active, handler](const sdk::PcmBlock &block) {
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
        active, std::make_unique<HostRegistration>(m_context.m_manager, m_context.m_run,
                                                   [key](Run &alive) { alive.pcm.erase(key); }));
}

sdk::Params HostContext::ConfigFacet::get() const {
    QMutexLocker lock(&m_context.m_mutex);
    return m_context.m_config;
}

void HostContext::ConfigFacet::update(const sdk::Params &config, sdk::Done done) {
    auto reply = m_context.forward(std::move(done));
    m_context.onRun([manager = m_context.m_manager, config, reply](const std::shared_ptr<Run> &run) {
        const auto result = manager->setConfig(run->id, config);
        reply(result ? Result<void>() : Result<void>(result.error()));
    });
}

void HostContext::StorageFacet::get(const QString &key, std::function<void(Result<QVariant>)> done) {
    auto reply = m_context.forward(std::move(done));
    m_context.onRun([manager = m_context.m_manager, key, reply](const std::shared_ptr<Run> &run) {
        if (key.size() > 256)
            reply(failure("Invalid storage key"));
        else
            reply(manager->m_storage.get(run->id, key));
    });
}

void HostContext::StorageFacet::set(const QString &key, const QVariant &value, sdk::Done done) {
    auto reply = m_context.forward(std::move(done));
    m_context.onRun([manager = m_context.m_manager, key, value, reply](const std::shared_ptr<Run> &run) {
        reply(manager->m_storage.set(run->id, key, value));
    });
}

void HostContext::SecretsFacet::get(const QString &key, std::function<void(Result<QString>)> done) {
    auto reply = m_context.forward(std::move(done));
    m_context.secret("get", key, {}, [reply](Result<QVariantMap> result) {
        if (!result)
            reply(result.error());
        else
            reply(result.value().value("value").toString());
    });
}

void HostContext::SecretsFacet::set(const QString &key, const QString &value, sdk::Done done) {
    auto reply = m_context.forward(std::move(done));
    m_context.secret("set", key, value, [reply](Result<QVariantMap> result) {
        reply(result ? Result<void>() : Result<void>(result.error()));
    });
}

void HostContext::SecretsFacet::remove(const QString &key, sdk::Done done) {
    auto reply = m_context.forward(std::move(done));
    m_context.secret("delete", key, {}, [reply](Result<QVariantMap> result) {
        reply(result ? Result<void>() : Result<void>(result.error()));
    });
}

void HostContext::TasksFacet::report(const QString &task, const sdk::TaskProgress &progress) {
    sdk::Params data{{"state", progress.state}};
    if (progress.progress >= 0)
        data.insert("progress", progress.progress);
    if (!progress.message.isEmpty())
        data.insert("message", progress.message);
    data.insert("task", task);
    m_context.events().publish("task", data);
}

void HostContext::LoggerFacet::write(QtMsgType level, const QString &message) {
    const QString name = level == QtDebugMsg     ? "debug"
                         : level == QtInfoMsg    ? "info"
                         : level == QtWarningMsg ? "warn"
                                                 : "error";
    m_context.onRun([manager = m_context.m_manager, name, message](const std::shared_ptr<Run> &run) {
        manager->recordLog(run->id, name, message);
    });
}

void HostContext::onRun(std::function<void(const std::shared_ptr<Run> &)> work) const {
    m_manager->post([run = m_run, work] {
        if (const auto alive = run.lock())
            work(alive);
    });
}

template <class T>
std::function<void(Result<T>)> HostContext::forward(std::function<void(Result<T>)> done) const {
    return
        [manager = QPointer<PluginManager>(m_manager), run = m_run, id = m_info.id, done](Result<T> result) {
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
    return std::make_unique<HostRegistration>(m_manager, m_run, [kind, id, remove](Run &run) {
        run.registrations.remove(kind, id);
        remove(run);
    });
}

void HostContext::secret(const QString &operation, const QString &key, const QString &value,
                         IExtensionBackend::Completion done) {
    onRun([manager = m_manager, operation, key, value, done](const std::shared_ptr<Run> &run) {
        const auto reply = [run, done](Result<QVariantMap> result) { done(std::move(result)); };
        if (operation == "get")
            manager->m_vault.get(run->id, key, reply);
        else if (operation == "set")
            manager->m_vault.set(run->id, key, value, reply);
        else
            manager->m_vault.remove(run->id, key, reply);
    });
}

ExtensionRuntime::~ExtensionRuntime() = default;
} // namespace nekotune
