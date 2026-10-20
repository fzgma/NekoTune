#pragma once
#include "domain/extensions/extension_backend.h"
#include "infrastructure/extensions/extension_manifest.h"
#include <QHash>
#include <QSet>
#include <QStringList>
#include <deque>
#include <memory>

class QPluginLoader;

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
    void shutdown();
    void request(const QString &method, const QVariantMap &params, Completion done) override;
    QVariantMap snapshot() const override;
    void publish(const QVariantMap &event) override;

  private:
    class Context;
    class Handle;
    struct Run;
    struct Entry {
        QString directory;
        bool development = false;
        bool enabled = false;
        bool trusted = false;
        QVariantMap previous;
        ExtensionManifest manifest;
        QString state = "disabled";
        QString error;
    };
    using Done = std::function<void(Result<void>)>;
    using Mutation = std::function<void(Completion)>;

    QString root() const;
    QString dataDirectory(const QString &id) const;
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
    Result<QString> loadablePath(const Entry &entry);

    /// Call into extension code with crash attribution; exceptions fault the extension.
    template <class F> bool guarded(const QString &id, F &&call);
    /// Wrap a completion so it may be called once from any thread, fails on timeout and fails
    /// when the run stops first.
    Completion pending(const std::shared_ptr<Run> &run, Completion done, int timeoutMs);

    QHash<QString, Entry> m_entries;
    QStringList m_order;
    QHash<QString, std::shared_ptr<Run>> m_runs;
    QHash<QString, QPluginLoader *> m_loaders;
    QHash<QString, QList<QVariantMap>> m_logs;
    QSet<QString> m_secrets;
    QVariantMap m_selections;
    QStringList m_developmentDirectories;
    QVariantMap m_extraState;
    std::deque<std::pair<Mutation, Completion>> m_mutations;
    HostCall m_hostCall;
    quint64 m_generation = 0;
    bool m_mutating = false;
    bool m_catalogueReady = false;
    bool m_startupComplete = false;
    bool m_closing = false;
    bool m_changePending = false;
    bool m_safeMode = false;
};
} // namespace nekotune
