#include "application/audio/pcm_service.h"
#include <QPointer>
#include <QThread>
#include <atomic>
#include <utility>
namespace nekotune {
namespace {
class PcmRegistration final : public sdk::Registration {
  public:
    explicit PcmRegistration(std::function<void()> stop) : stop(std::move(stop)) {}
    ~PcmRegistration() override { dispose(); }
    void dispose() override {
        if (!disposed.exchange(true))
            stop();
    }

  private:
    std::function<void()> stop;
    std::atomic<bool> disposed{false};
};
} // namespace
PcmService::PcmService(IPlaybackBackend &backend) : audio(backend) {
    timer.setInterval(10);
    connect(&timer, &QTimer::timeout, this, &PcmService::drain);
    connect(&audio, &IPlaybackBackend::backendChanged, this, &PcmService::reconcile);
    connect(&audio, &IPlaybackBackend::audioOutputAvailabilityChanged, this, &PcmService::reconcile);
}
PcmService::~PcmService() {
    timer.stop();
    for (const auto &subscriber : handlers)
        subscriber->active = false;
    audio.setPcmCaptureEnabled(false);
}
sdk::PcmCapabilities PcmService::capabilities() const {
    return {audio.supportsPcmCapture() && audio.audioOutputAvailable(), audio.backendName(),
            !audio.supportsPcmCapture()     ? QStringLiteral("playback_backend_unsupported")
            : !audio.audioOutputAvailable() ? QStringLiteral("audio_output_unavailable")
                                            : QString()};
}
sdk::RegistrationPtr PcmService::subscribe(sdk::PcmHandler handler) {
    if (!handler || !capabilities().available)
        return nullptr;
    const auto id = ++nextId;
    auto subscriber = std::make_shared<Subscriber>();
    subscriber->handler = std::move(handler);
    handlers.insert(id, subscriber);
    reconcile();
    return std::make_unique<PcmRegistration>([guard = QPointer<PcmService>(this), id, subscriber] {
        subscriber->active = false;
        if (!guard)
            return;
        auto remove = [guard, id] {
            if (guard) {
                guard->handlers.remove(id);
                guard->reconcile();
            }
        };
        if (QThread::currentThread() == guard->thread())
            remove();
        else
            QMetaObject::invokeMethod(guard, std::move(remove), Qt::QueuedConnection);
    });
}
void PcmService::reconcile() {
    const bool enabled = !handlers.isEmpty() && capabilities().available;
    audio.setPcmCaptureEnabled(enabled);
    if (enabled)
        timer.start();
    else {
        timer.stop();
        audio.takePcmBlocks();
    }
}
void PcmService::drain() {
    const auto blocks = audio.takePcmBlocks();
    const auto subscribers = handlers.values();
    for (const auto &block : blocks)
        for (const auto &subscriber : subscribers)
            if (subscriber->active)
                subscriber->handler(block);
}
} // namespace nekotune
