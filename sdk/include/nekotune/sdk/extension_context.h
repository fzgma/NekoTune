#pragma once
#include <nekotune/sdk/pcm_provider.h>
#include <nekotune/sdk/providers.h>
#include <nekotune/sdk/shuffle_provider.h>
#include <nekotune/sdk/types.h>
#include <QVariant>
#include <QtGlobal>

namespace nekotune::sdk {
struct ExtensionInfo {
    QString id;
    QString version;
    /// Installed program directory; treat as read-only.
    QString directory;
    /// Private writable data directory that survives updates.
    QString dataDirectory;
};
struct CommandInfo {
    QString title;
    QString shortcut;
};
struct TaskProgress {
    QString state;
    /// 0..1; negative when unknown.
    double progress = -1;
    QString message;
};

class Host {
  public:
    virtual ~Host() = default;
    /// Invoke a host IPC method such as "player.pause". Lifecycle, trust, appearance and
    /// irreversible library methods fail with ErrorCode::PermissionDenied.
    virtual void call(const QString &method, const Params &params, Reply done) = 0;
};
class Events {
  public:
    virtual ~Events() = default;
    /// "player.track_changed", "*", ...; extension events arrive as "extension.<id>.<event>".
    virtual RegistrationPtr on(const QString &event, EventHandler handler) = 0;
    /// Broadcast "extension.<id>.<event>" to the host and other extensions.
    virtual void publish(const QString &event, const Params &data = {}) = 0;
};
class Services {
  public:
    virtual ~Services() = default;
    virtual RegistrationPtr add(const QString &id, Handler handler) = 0;
    virtual void call(const QString &extension, const QString &service, const Params &params, Reply done) = 0;
};
class Commands {
  public:
    virtual ~Commands() = default;
    virtual RegistrationPtr add(const QString &id, Handler handler, const CommandInfo &info = {}) = 0;
};
class Ui {
  public:
    virtual ~Ui() = default;
    /// kind is pages, settings, slots, themes, menus or toolbars; fields match the manifest and
    /// sources are relative to the extension directory.
    virtual RegistrationPtr add(const QString &kind, const QString &id, const Params &descriptor) = 0;
};
class Music {
  public:
    virtual ~Music() = default;
    /// The provider stays owned by the plugin and must outlive its registration.
    virtual RegistrationPtr add(const QString &id, IMusicProvider *provider, const MusicSourceInfo &info) = 0;
};
class Lyrics {
  public:
    virtual ~Lyrics() = default;
    virtual RegistrationPtr add(const QString &id, ILyricsProvider *provider, const LyricsSourceInfo &info) = 0;
};
class Shuffle {
  public:
    virtual ~Shuffle() = default;
    /// info.name must not be empty. The provider must outlive its registration.
    virtual RegistrationPtr add(const QString &id, IShuffleProvider *provider, const ShuffleInfo &info) = 0;
};
class Pcm {
  public:
    virtual ~Pcm() = default;
    virtual PcmCapabilities capabilities() const = 0;
    /// Null when unsupported. Releasing the handle stops callbacks immediately.
    virtual RegistrationPtr subscribe(PcmHandler handler) = 0;
};
class Config {
  public:
    virtual ~Config() = default;
    virtual Params get() const = 0;
    /// Persist a new configuration; the change is also delivered as "extension.config_changed".
    virtual void update(const Params &config, Done done) = 0;
};
class Storage {
  public:
    virtual ~Storage() = default;
    /// JSON-compatible values only; the whole store is limited to 8 MiB.
    virtual void get(const QString &key, std::function<void(Result<QVariant>)> done) = 0;
    virtual void set(const QString &key, const QVariant &value, Done done) = 0;
};
class Secrets {
  public:
    virtual ~Secrets() = default;
    /// System keyring, redacted from logs. An absent secret is a null QString.
    virtual void get(const QString &key, std::function<void(Result<QString>)> done) = 0;
    virtual void set(const QString &key, const QString &value, Done done) = 0;
    virtual void remove(const QString &key, Done done) = 0;
};
class Tasks {
  public:
    virtual ~Tasks() = default;
    virtual void report(const QString &task, const TaskProgress &progress) = 0;
};
class Logger {
  public:
    virtual ~Logger() = default;
    virtual void write(QtMsgType level, const QString &message) = 0;
};

/// While alive, a crash on the creating thread is attributed to this extension. Create it at the
/// start of work on a thread the extension owns (std::thread, QThread, QtConcurrent) and destroy
/// it on the same thread.
class ThreadAttribution {
  public:
    virtual ~ThreadAttribution() = default;
};

/// Runtime services for one activation. The context outlives activate() and is invalidated when
/// deactivate() returns; do not keep references beyond that point.
///
/// Every method may be called from any thread; the host marshals work to the backend thread.
/// Callbacks passed to the host run on the backend thread, at most once, and are dropped
/// without being called when the extension stops first.
class ExtensionContext {
  public:
    virtual ~ExtensionContext() = default;
    virtual const ExtensionInfo &info() const = 0;
    virtual Host &host() = 0;
    virtual Events &events() = 0;
    virtual Services &services() = 0;
    virtual Commands &commands() = 0;
    virtual Ui &ui() = 0;
    virtual Music &music() = 0;
    virtual Lyrics &lyrics() = 0;
    virtual Shuffle &shuffle() = 0;
    virtual Pcm &pcm() = 0;
    virtual Config &config() = 0;
    virtual Storage &storage() = 0;
    virtual Secrets &secrets() = 0;
    virtual Tasks &tasks() = 0;
    virtual Logger &log() = 0;
    /// Mark the calling thread as running this extension. Host callbacks are already attributed.
    virtual std::unique_ptr<ThreadAttribution> attributeThread() = 0;
};
} // namespace nekotune::sdk
