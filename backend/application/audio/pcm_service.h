#pragma once
#include "domain/audio/pcm_source.h"
#include "domain/playback/playback_backend.h"
#include <QHash>
#include <QTimer>
#include <atomic>
#include <memory>

namespace nekotune {
// Backend-thread service. Owns subscriptions, never runs extension code on the audio thread.
class PcmService final : public QObject, public IPcmSource {
    Q_OBJECT
  public:
    explicit PcmService(IPlaybackBackend &backend);
    ~PcmService() override;
    sdk::PcmCapabilities capabilities() const override;
    sdk::RegistrationPtr subscribe(sdk::PcmHandler handler) override;
    int subscriberCount() const { return int(handlers.size()); }

  private:
    void reconcile();
    void drain();
    IPlaybackBackend &audio;
    QTimer timer;
    quint64 nextId = 0;
    struct Subscriber {
        std::atomic<bool> active{true};
        sdk::PcmHandler handler;
    };
    QHash<quint64, std::shared_ptr<Subscriber>> handlers;
};
} // namespace nekotune
