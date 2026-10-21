#pragma once
#include <nekotune/sdk/extension_context.h>
#include <QVector>

namespace nekotune::sdk {
/// queueId identifies an occurrence; songId may be shared by several occurrences.
struct ShuffleTrack {
    int queueId = 0;
    int songId = 0;
    QString title, artist, album;
    qint64 durationMs = 0;
    qint64 playCount = 0;
    QString lastPlayedAt;
};

struct ShuffleRequest {
    QVector<ShuffleTrack> queue;
    int currentId = 0;
    quint32 seed = 0;
    QVariantMap state;
};

struct ShuffleSelection {
    int queueId = 0;
    QVariantMap state;
};

/// Called synchronously on the backend thread. Return quickly; never wait for hostCall or I/O.
/// select must be deterministic for the request and must not mutate provider-owned progress or
/// randomness: observations and preloads may repeat/discard calls. Use request.seed and return
/// JSON-compatible state instead. The host commits state only after saving the selected item.
class IShuffleProvider {
  public:
    virtual ~IShuffleProvider() = default;
    virtual Result<ShuffleSelection> select(const ShuffleRequest &request) = 0;
};

/// Optional SDK 2.1 capability. Kept separate to preserve the SDK 2.0 context ABI and plugin IID.
class IShuffleExtensionContext {
  public:
    virtual ~IShuffleExtensionContext() = default;
    /// Same threading/handle lifetime contract as IExtensionContext registrations.
    /// descriptor requires a nonempty name. The provider must outlive its registration.
    virtual RegistrationPtr registerShuffleStrategy(const QString &id, IShuffleProvider *provider,
                                                     const Params &descriptor) = 0;
};

/// Null on older hosts. Check before registering and explain the required host capability.
inline IShuffleExtensionContext *shuffleContext(IExtensionContext &context) {
    return dynamic_cast<IShuffleExtensionContext *>(&context);
}
} // namespace nekotune::sdk
