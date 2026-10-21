// Frozen SDK 2.0 ABI fixture. Do not update with the current SDK.
#pragma once
#include <nekotune/sdk/providers.h>
#include <QVariant>
#include <QtGlobal>
#include <memory>

namespace nekotune::sdk {
using Handler = std::function<void(const Params &params, Reply done)>;
using EventHandler = std::function<void(const Params &event)>;

/// Host-owned registration handle. dispose() or destruction removes the registration; both are
/// no-ops after the extension stopped, so plugins may keep handles until deactivate() returns.
class Registration {
  public:
    virtual ~Registration() = default;
    virtual void dispose() = 0;
};
using RegistrationPtr = std::unique_ptr<Registration>;

/// Runtime services for one activation. The context outlives activate() and is invalidated when
/// deactivate() returns; do not keep the reference beyond that point.
///
/// Every method may be called from any thread; the host marshals work to the backend thread.
/// Callbacks passed to the host run on the backend thread, at most once, and are dropped
/// without being called when the extension stops first.
class IExtensionContext {
  public:
    virtual ~IExtensionContext() = default;

    virtual QString id() const = 0;
    virtual QString version() const = 0;
    /// Installed program directory; treat as read-only.
    virtual QString directory() const = 0;
    /// Private writable data directory that survives updates.
    virtual QString dataDirectory() const = 0;
    virtual Params config() const = 0;
    /// Persist a new configuration; the change is also delivered as "extension.config_changed".
    virtual void updateConfig(const Params &config, std::function<void(Result<void>)> done) = 0;

    /// Invoke a host IPC method such as "player.pause" or "song.metadata".
    virtual void hostCall(const QString &method, const Params &params, Reply done) = 0;

    /// Services and commands must reply with an object; wrap scalars in {value}.
    virtual RegistrationPtr registerService(const QString &id, Handler handler) = 0;
    virtual void callService(const QString &extension, const QString &service, const Params &params,
                             Reply done) = 0;
    /// descriptor supports title and shortcut.
    virtual RegistrationPtr registerCommand(const QString &id, Handler handler, const Params &descriptor = {}) = 0;
    /// kind is pages, settings, slots, themes, menus or toolbars; sources are relative to directory().
    virtual RegistrationPtr registerUi(const QString &kind, const QString &id, const Params &descriptor) = 0;

    /// The provider stays owned by the plugin and must outlive its registration.
    /// descriptor supports name, download and page.
    virtual RegistrationPtr registerMusicProvider(const QString &id, IMusicProvider *provider,
                                                  const Params &descriptor) = 0;
    /// descriptor supports name.
    virtual RegistrationPtr registerLyricsProvider(const QString &id, ILyricsProvider *provider,
                                                   const Params &descriptor) = 0;

    /// Subscribe to host events ("player.track_changed", "*", ...). Extension events arrive as
    /// "extension.<id>.<event>".
    virtual RegistrationPtr on(const QString &event, EventHandler handler) = 0;
    /// Broadcast "extension.<id>.<event>" to the host and other extensions.
    virtual void publish(const QString &event, const Params &data = {}) = 0;

    /// JSON-compatible values only; the whole store is limited to 8 MiB.
    virtual void storageGet(const QString &key, std::function<void(Result<QVariant>)> done) = 0;
    virtual void storageSet(const QString &key, const QVariant &value, std::function<void(Result<void>)> done) = 0;
    /// Secrets live in the system keyring and are redacted from logs. An absent secret is a null QString.
    virtual void secretGet(const QString &key, std::function<void(Result<QString>)> done) = 0;
    virtual void secretSet(const QString &key, const QString &value, std::function<void(Result<void>)> done) = 0;
    virtual void secretDelete(const QString &key, std::function<void(Result<void>)> done) = 0;

    /// progress supports state, progress (0..1) and message.
    virtual void reportTask(const QString &task, const Params &progress) = 0;
    virtual void log(QtMsgType level, const QString &message) = 0;
};
} // namespace nekotune::sdk
