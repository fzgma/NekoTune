#pragma once
#include "infrastructure/extensions/runtime/extension_runtime.h"
#include <nekotune/sdk/extension_context.h>
#include <QMutex>
#include <atomic>

namespace nekotune {
class PluginManager;

class HostContext final : public sdk::ExtensionContext {
    using Run = ExtensionRuntime;

  public:
    HostContext(PluginManager *manager, std::weak_ptr<Run> run, QString id, QString version,
                QString directory, QString dataDirectory, QVariantMap config);
    const sdk::ExtensionInfo &info() const override { return m_info; }
    void setConfig(const QVariantMap &config);
    sdk::Host &host() override { return m_host; }
    sdk::Events &events() override { return m_events; }
    sdk::Services &services() override { return m_services; }
    sdk::Commands &commands() override { return m_commands; }
    sdk::Ui &ui() override { return m_ui; }
    sdk::Music &music() override { return m_music; }
    sdk::Lyrics &lyrics() override { return m_lyrics; }
    sdk::Shuffle &shuffle() override { return m_shuffle; }
    sdk::Pcm &pcm() override { return m_pcm; }
    sdk::Config &config() override { return m_configFacet; }
    sdk::Storage &storage() override { return m_storage; }
    sdk::Secrets &secrets() override { return m_secrets; }
    sdk::Tasks &tasks() override { return m_tasks; }
    sdk::Logger &log() override { return m_log; }
    std::unique_ptr<sdk::ThreadAttribution> attributeThread() override;

  private:
    class HostFacet final : public sdk::Host {
      public:
        explicit HostFacet(HostContext &context) : m_context(context) {}
        void call(const QString &method, const sdk::Params &params, sdk::Reply done) override;

      private:
        HostContext &m_context;
    };
    class EventsFacet final : public sdk::Events {
      public:
        explicit EventsFacet(HostContext &context) : m_context(context) {}
        sdk::RegistrationPtr on(const QString &event, sdk::EventHandler handler) override;
        void publish(const QString &event, const sdk::Params &data) override;

      private:
        HostContext &m_context;
    };
    class ServicesFacet final : public sdk::Services {
      public:
        explicit ServicesFacet(HostContext &context) : m_context(context) {}
        sdk::RegistrationPtr add(const QString &id, sdk::Handler handler) override;
        void call(const QString &extension, const QString &service, const sdk::Params &params,
                  sdk::Reply done) override;

      private:
        HostContext &m_context;
    };
    class CommandsFacet final : public sdk::Commands {
      public:
        explicit CommandsFacet(HostContext &context) : m_context(context) {}
        sdk::RegistrationPtr add(const QString &id, sdk::Handler handler,
                                 const sdk::CommandInfo &info) override;

      private:
        HostContext &m_context;
    };
    class UiFacet final : public sdk::Ui {
      public:
        explicit UiFacet(HostContext &context) : m_context(context) {}
        sdk::RegistrationPtr add(const QString &kind, const QString &id,
                                 const sdk::Params &descriptor) override;

      private:
        HostContext &m_context;
    };
    class MusicFacet final : public sdk::Music {
      public:
        explicit MusicFacet(HostContext &context) : m_context(context) {}
        sdk::RegistrationPtr add(const QString &id, sdk::IMusicProvider *provider,
                                 const sdk::MusicSourceInfo &info) override;

      private:
        HostContext &m_context;
    };
    class LyricsFacet final : public sdk::Lyrics {
      public:
        explicit LyricsFacet(HostContext &context) : m_context(context) {}
        sdk::RegistrationPtr add(const QString &id, sdk::ILyricsProvider *provider,
                                 const sdk::LyricsSourceInfo &info) override;

      private:
        HostContext &m_context;
    };
    class ShuffleFacet final : public sdk::Shuffle {
      public:
        explicit ShuffleFacet(HostContext &context) : m_context(context) {}
        sdk::RegistrationPtr add(const QString &id, sdk::IShuffleProvider *provider,
                                 const sdk::ShuffleInfo &info) override;

      private:
        HostContext &m_context;
    };
    class PcmFacet final : public sdk::Pcm {
      public:
        explicit PcmFacet(HostContext &context) : m_context(context) {}
        sdk::PcmCapabilities capabilities() const override;
        sdk::RegistrationPtr subscribe(sdk::PcmHandler handler) override;

      private:
        HostContext &m_context;
    };
    class ConfigFacet final : public sdk::Config {
      public:
        explicit ConfigFacet(HostContext &context) : m_context(context) {}
        sdk::Params get() const override;
        void update(const sdk::Params &config, sdk::Done done) override;

      private:
        HostContext &m_context;
    };
    class StorageFacet final : public sdk::Storage {
      public:
        explicit StorageFacet(HostContext &context) : m_context(context) {}
        void get(const QString &key, std::function<void(Result<QVariant>)> done) override;
        void set(const QString &key, const QVariant &value, sdk::Done done) override;

      private:
        HostContext &m_context;
    };
    class SecretsFacet final : public sdk::Secrets {
      public:
        explicit SecretsFacet(HostContext &context) : m_context(context) {}
        void get(const QString &key, std::function<void(Result<QString>)> done) override;
        void set(const QString &key, const QString &value, sdk::Done done) override;
        void remove(const QString &key, sdk::Done done) override;

      private:
        HostContext &m_context;
    };
    class TasksFacet final : public sdk::Tasks {
      public:
        explicit TasksFacet(HostContext &context) : m_context(context) {}
        void report(const QString &task, const sdk::TaskProgress &progress) override;

      private:
        HostContext &m_context;
    };
    class LoggerFacet final : public sdk::Logger {
      public:
        explicit LoggerFacet(HostContext &context) : m_context(context) {}
        void write(QtMsgType level, const QString &message) override;

      private:
        HostContext &m_context;
    };

    void onRun(std::function<void(const std::shared_ptr<Run> &)> work) const;
    /// Return a host-side completion that calls the plugin's callback on the backend thread,
    /// or drops it when the run has stopped.
    template <class T> std::function<void(Result<T>)> forward(std::function<void(Result<T>)> done) const;
    sdk::RegistrationPtr add(const QString &kind, const QString &id, const QVariantMap &descriptor,
                             std::function<void(Run &)> install, std::function<void(Run &)> remove);
    void secret(const QString &operation, const QString &key, const QString &value,
                IExtensionBackend::Completion done);

    PluginManager *m_manager;
    std::weak_ptr<Run> m_run;
    const sdk::ExtensionInfo m_info;
    mutable QMutex m_mutex;
    QVariantMap m_config;
    std::atomic<quint64> m_nextSubscription{1'000'000};
    HostFacet m_host{*this};
    EventsFacet m_events{*this};
    ServicesFacet m_services{*this};
    CommandsFacet m_commands{*this};
    UiFacet m_ui{*this};
    MusicFacet m_music{*this};
    LyricsFacet m_lyrics{*this};
    ShuffleFacet m_shuffle{*this};
    PcmFacet m_pcm{*this};
    ConfigFacet m_configFacet{*this};
    StorageFacet m_storage{*this};
    SecretsFacet m_secrets{*this};
    TasksFacet m_tasks{*this};
    LoggerFacet m_log{*this};
};
} // namespace nekotune
