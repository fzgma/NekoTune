#pragma once
#include "domain/extensions/extension_backend.h"
#include "infrastructure/extensions/capabilities/capability_host.h"
#include "infrastructure/extensions/capabilities/music_sources.h"
#include "infrastructure/extensions/capabilities/lyrics_sources.h"
#include "infrastructure/extensions/capabilities/shuffle_strategies.h"
#include "infrastructure/extensions/capabilities/service_bus.h"
#include "infrastructure/extensions/capabilities/extension_storage.h"
#include "infrastructure/extensions/capabilities/extension_secrets.h"
#include "infrastructure/extensions/capabilities/extension_log.h"
#include "infrastructure/extensions/package/extension_manifest.h"
#include "infrastructure/extensions/package/extension_registry.h"
#include "infrastructure/extensions/package/package_store.h"
#include "infrastructure/extensions/runtime/extension_state.h"
#include "infrastructure/extensions/runtime/call_guard.h"
#include "infrastructure/extensions/runtime/dependency_graph.h"
#include "infrastructure/extensions/runtime/native_loader.h"
#include "infrastructure/extensions/runtime/session_marker.h"
#include <QHash>
#include <QSet>
#include <QStringList>
#include <deque>
#include <memory>

namespace nekotune {
class IPcmSource;
class HostContext;
struct ExtensionRuntime;
}

namespace nekotune {
/// In-process native extension runtime. Loads trusted apiVersion 3 libraries with QPluginLoader
/// and serves the public extension request/snapshot protocol. Business services use the
/// capability catalogues directly.
///
/// Everything runs on the owning (backend) thread. Lifecycle mutations are serialized and always
/// start from the event loop, never from inside an extension callback. Libraries stay mapped until
/// the process exits: re-enabling reuses the loaded instance and development reloads load a copy.
class PluginManager final : public IExtensionBackend, private CapabilityHost {
    Q_OBJECT
  public:
    explicit PluginManager(QObject *parent = nullptr);
    ~PluginManager() override;
    void start(HostCall hostCall);
    void setPcmService(IPcmSource *service) { m_pcm = service; }
    void shutdown();
    void request(const QString &method, const QVariantMap &params, Completion done) override;
    QVariantMap snapshot() const override;
    void publish(const QVariantMap &event) override;
    IMusicSourceCatalog &musicSources() { return m_music; }
    ILyricsSourceCatalog &lyricsSources() { return m_lyrics; }
    ShuffleStrategies &shuffle() { return m_shuffle; }

  private:
    friend class HostContext;
    friend class HostRegistration;
    using Run = ExtensionRuntime;
    struct Entry : ExtensionRecord {
        ExtensionManifest manifest;
        ExtensionState state = ExtensionState::Disabled;
        QString error;
    };
    using Done = std::function<void(Result<void>)>;
    using Mutation = std::function<void(Completion)>;

    void post(std::function<void()> work);
    void notifyChanged();
    void reportError(const QString &message);
    QObject *owner() override { return this; }
    QStringList extensionOrder() const override { return m_order; }
    bool running(const QString &id) const override;
    PendingCalls *pendingCalls(const QString &id) override;
    bool guarded(const QString &id, const std::function<void()> &call) override {
        return guarded(id, [&call] { call(); });
    }
    QString dataDirectory(const QString &id) const override { return m_packages.dataDirectory(id); }
    void recordLog(const QString &id, const QString &level, const QString &message) override;
    QVariantMap publicEntry(const QString &id) const;
    void load();
    Result<void> save();
    void enqueue(Mutation mutation, Completion done);
    void runMutations();
    void startup(int index, QStringList order, Done finished);

    DependencyNodes dependencyNodes() const;
    Result<QStringList> dependencyOrder(const QString &id) const;
    QStringList dependents(const QString &id) const;
    void enable(const QString &id, bool trusted, Done done);
    void startChain(QStringList order, int index, Done done);
    void startOne(const QString &id, Done done);
    void activated(const std::shared_ptr<Run> &run, Result<void> result);
    void stop(const QString &id);
    void fault(const QString &id, const QString &message);
    void disable(const QString &id);
    void reload(const QString &id, Done done);
    void install(const QVariantMap &params, Completion done);
    void uninstall(const QVariantMap &params, Completion done);
    Result<QVariantMap> select(const QVariantMap &params);
    Result<void> validateSelection(const QString &slot, const QString &contribution) const;
    Result<QVariantMap> exportTheme(const QVariantMap &params);
    void cleanUnusedPackages();
    Result<QVariantMap> setConfig(const QString &id, const QVariant &config);
    QVariantMap readConfig(const QString &id) const;
    Result<void> addRegistration(const std::shared_ptr<Run> &run, const QString &kind, const QString &id,
                                 QVariantMap descriptor);
    void reloading(const QStringList &ids, bool active);
    void watchDevelopment(const std::shared_ptr<Run> &run);

    /// Call into extension code with crash attribution; exceptions fault the extension.
    template <class F> bool guarded(const QString &id, F &&call);

    PackageStore m_packages;
    SessionMarker m_session{m_packages.root() + "/session-active"};
    NativeLoader m_loader{this, m_packages};
    MusicSources m_music{*this};
    LyricsSources m_lyrics{*this};
    ShuffleStrategies m_shuffle{*this};
    ServiceBus m_bus{*this};
    ExtensionStorage m_storage{m_packages};
    SecretVault m_vault{m_packages};
    ExtensionLog m_log{m_vault};
    ExtensionRegistry m_registry;
    QHash<QString, Entry> m_entries;
    QStringList m_order;
    QHash<QString, std::shared_ptr<Run>> m_runs;
    /// Extensions whose native QML modules were loaded this session, and every directory holding
    /// code that is mapped into this process; neither may be replaced or deleted before restart.
    QSet<QString> m_nativeActivated, m_retainedDirectories;
    QVariantMap m_selections;
    QStringList m_developmentDirectories;
    QVariantMap m_registryExtra;
    std::deque<std::pair<Mutation, Completion>> m_mutations;
    IPcmSource *m_pcm = nullptr;
    HostCall m_hostCall;
    quint64 m_generation = 0;
    bool m_mutating = false;
    bool m_catalogueReady = false;
    bool m_startupComplete = false;
    bool m_closing = false;
    bool m_changePending = false;
    bool m_safeMode = false;
    QString m_safeModeReason;
};

template <class F> bool PluginManager::guarded(const QString &id, F &&call) {
    const auto outcome = runAttributed(id, std::forward<F>(call));
    if (outcome.hung) {
        crashguard::clearSentinel();
        post([this, id, ms = outcome.elapsedMs] {
            fault(id, QStringLiteral("Extension blocked the backend for %1 ms").arg(ms));
        });
        return false;
    }
    if (outcome.elapsedMs > watchdog::Limits{}.warnMs)
        post([this, id, ms = outcome.elapsedMs] {
            recordLog(id, "warn", QStringLiteral("Extension call blocked the backend for %1 ms").arg(ms));
        });
    if (!outcome.error)
        return true;
    // Fault from the event loop; the caller may still be iterating the run's registrations.
    post([this, id, message = *outcome.error] { fault(id, message); });
    return false;
}

} // namespace nekotune
