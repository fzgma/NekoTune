#pragma once
#include "infrastructure/extensions/runtime/extension_runtime.h"
#include <nekotune/sdk/extension_context.h>
#include <nekotune/sdk/pcm_provider.h>
#include <QMutex>
#include <atomic>

namespace nekotune {
class PluginManager;

class HostContext final : public sdk::IExtensionContext,
                          public sdk::IShuffleExtensionContext,
                          public sdk::IPcmExtensionContext {
    using Run = ExtensionRuntime;

  public:
    HostContext(PluginManager *manager, std::weak_ptr<Run> run, QString id, QString version, QString directory,
                QString dataDirectory, QVariantMap config);

    QString id() const override;
    QString version() const override;
    QString directory() const override;
    QString dataDirectory() const override;
    sdk::Params config() const override;
    void setConfig(const QVariantMap &config);
    void updateConfig(const sdk::Params &config, std::function<void(Result<void>)> done) override;
    void hostCall(const QString &method, const sdk::Params &params, sdk::Reply done) override;
    sdk::RegistrationPtr registerService(const QString &id, sdk::Handler handler) override;
    void callService(const QString &extension, const QString &service, const sdk::Params &params,
                     sdk::Reply done) override;
    sdk::RegistrationPtr registerCommand(const QString &id, sdk::Handler handler,
                                         const sdk::Params &descriptor) override;
    sdk::RegistrationPtr registerUi(const QString &kind, const QString &id, const sdk::Params &descriptor) override;
    sdk::RegistrationPtr registerMusicProvider(const QString &id, sdk::IMusicProvider *provider,
                                               const sdk::Params &descriptor) override;
    sdk::RegistrationPtr registerLyricsProvider(const QString &id, sdk::ILyricsProvider *provider,
                                                const sdk::Params &descriptor) override;
    sdk::RegistrationPtr on(const QString &event, sdk::EventHandler handler) override;
    sdk::PcmCapabilities pcmCapabilities() const override;
    sdk::RegistrationPtr subscribePcm(sdk::PcmHandler handler) override;
    sdk::RegistrationPtr registerShuffleStrategy(const QString &id, sdk::IShuffleProvider *provider,
                                                 const sdk::Params &descriptor) override;
    void publish(const QString &event, const sdk::Params &data) override;
    void storageGet(const QString &key, std::function<void(Result<QVariant>)> done) override;
    void storageSet(const QString &key, const QVariant &value, std::function<void(Result<void>)> done) override;
    void secretGet(const QString &key, std::function<void(Result<QString>)> done) override;
    void secretSet(const QString &key, const QString &value, std::function<void(Result<void>)> done) override;
    void secretDelete(const QString &key, std::function<void(Result<void>)> done) override;
    void reportTask(const QString &task, const sdk::Params &progress) override;
    void log(QtMsgType level, const QString &message) override;

  private:
    void onRun(std::function<void(const std::shared_ptr<Run> &)> work) const;
    /// Return a host-side completion that calls the plugin's callback on the backend thread,
    /// or drops it when the run has stopped.
    template <class T> std::function<void(Result<T>)> forward(std::function<void(Result<T>)> done) const;
    sdk::RegistrationPtr add(const QString &kind, const QString &id, const QVariantMap &descriptor,
                             std::function<void(Run &)> install, std::function<void(Run &)> remove);
    void secret(const QString &operation, const QString &key, const QString &value, IExtensionBackend::Completion done);

    PluginManager *m_manager;
    std::weak_ptr<Run> m_run;
    const QString m_id, m_version, m_directory, m_dataDirectory;
    mutable QMutex m_mutex;
    QVariantMap m_config;
    std::atomic<quint64> m_nextSubscription{1'000'000};
};
} // namespace nekotune
