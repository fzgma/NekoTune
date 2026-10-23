#pragma once
#include "domain/extensions/extension_backend.h"
#include "domain/playback/shuffle_strategy_descriptor.h"
#include <nekotune/sdk/shuffle_provider.h>
#include "infrastructure/extensions/package/extension_manifest.h"
#include "infrastructure/extensions/package/extension_registry.h"
#include "infrastructure/extensions/package/package_store.h"
#include "infrastructure/extensions/runtime/extension_state.h"
#include "infrastructure/extensions/runtime/call_guard.h"
#include "infrastructure/extensions/runtime/dependency_graph.h"
#include "infrastructure/extensions/runtime/native_loader.h"
#include <QHash>
#include <QSet>
#include <QStringList>
#include <deque>
#include <memory>

namespace nekotune {
class AudioProxy;
class IPcmSource;
class HostContext;
struct ExtensionRuntime;
}

namespace nekotune {
/// In-process native extension runtime. Loads trusted apiVersion 2 libraries with QPluginLoader
/// and serves the same request/snapshot protocol the Node supervisor provided, so
/// ExtensionService, MusicService and the frontend stay unchanged.
///
/// Everything runs on the owning (backend) thread. Lifecycle mutations are serialized and always
/// start from the event loop, never from inside an extension callback. Libraries stay mapped until
/// the process exits: re-enabling reuses the loaded instance and development reloads load a copy.
class PluginManager final : public IExtensionBackend {
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
    QList<ShuffleStrategyDescriptor> shuffleStrategies() const;
    Result<sdk::ShuffleSelection> selectShuffle(const QString &id, quint64 generation,
                                                const sdk::ShuffleRequest &request);
  signals:
    void shuffleCatalogueChanged();

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
    void recordLog(const QString &id, const QString &level, const QString &message);
    QString redact(QString message) const;
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
    void invoke(const QString &id, QString service, const QVariantMap &params, Completion done);
    void providerCall(const QVariantMap &params, Completion done);
    void resolveAudio(const QVariantMap &params, Completion done);
    Result<void> addRegistration(const std::shared_ptr<Run> &run, const QString &kind, const QString &id,
                                 QVariantMap descriptor);
    void deliver(const std::shared_ptr<Run> &run, const QVariantMap &event);
    void broadcast(const QVariantMap &event);
    void reloading(const QStringList &ids, bool active);
    void watchDevelopment(const std::shared_ptr<Run> &run);

    /// Call into extension code with crash attribution; exceptions fault the extension.
    template <class F> bool guarded(const QString &id, F &&call);

    PackageStore m_packages;
    NativeLoader m_loader{this, m_packages};
    ExtensionRegistry m_registry;
    QHash<QString, Entry> m_entries;
    QStringList m_order;
    QHash<QString, std::shared_ptr<Run>> m_runs;
    QHash<QString, QList<QVariantMap>> m_logs;
    QSet<QString> m_secrets;
    /// Extensions whose native QML modules were loaded this session, and every directory holding
    /// code that is mapped into this process; neither may be replaced or deleted before restart.
    QSet<QString> m_nativeActivated, m_retainedDirectories;
    QVariantMap m_selections;
    QStringList m_developmentDirectories;
    QVariantMap m_registryExtra;
    std::deque<std::pair<Mutation, Completion>> m_mutations;
    IPcmSource *m_pcm = nullptr;
    HostCall m_hostCall;
    std::unique_ptr<AudioProxy> m_audio;
    quint64 m_generation = 0;
    bool m_mutating = false;
    bool m_catalogueReady = false;
    bool m_startupComplete = false;
    bool m_closing = false;
    bool m_changePending = false;
    bool m_safeMode = false;
};

template <class F> bool PluginManager::guarded(const QString &id, F &&call) {
    const auto error = runAttributed(id, std::forward<F>(call));
    if (!error)
        return true;
    // Fault from the event loop; the caller may still be iterating the run's registrations.
    post([this, id, message = *error] { fault(id, message); });
    return false;
}

} // namespace nekotune
