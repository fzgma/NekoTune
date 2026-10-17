#include "infrastructure/playback/ffmpeg_playback_backend.h"
#include "infrastructure/playback/pcm_decoder.h"
#include "infrastructure/playback/pcm_dsp.h"
#include "infrastructure/playback/pcm_renderer.h"
#include <QAudioSink>
#include "infrastructure/playback/pcm_output_clock.h"
#include <QIODevice>
#include <QPointer>
#include <QTimer>
#include <algorithm>
#include <deque>
#include <optional>
extern "C" {
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
}
namespace nekotune {
class PcmOutputWorker final : public QObject {
  public:
    explicit PcmOutputWorker(FFmpegPlaybackBackend *backend) : owner(backend) {
        timer = new QTimer(this);
        timer->setInterval(5);
        timer->setTimerType(Qt::PreciseTimer);
        connect(timer, &QTimer::timeout, this, [this] { tick(); });
    }
    ~PcmOutputWorker() override { clear({}); }
    template <class F> void post(F callback, bool checkGeneration = true) {
        const auto generation = epoch;
        QPointer<FFmpegPlaybackBackend> guard = owner;
        QMetaObject::invokeMethod(
            owner,
            [guard, generation, checkGeneration, callback = std::move(callback)] {
                if (guard && (!checkGeneration || guard->m_generation == generation))
                    callback(*guard);
            },
            Qt::QueuedConnection);
    }
    void clear(const QUrl &keep) {
        if (loaded)
            loaded->cancel();
        if (nextDecoder)
            nextDecoder->cancel();
        renderer.reset();
        loaded.reset();
        nextDecoder.reset();
        dsp.reset();
        if (sink)
            sink->reset();
        sink.reset();
        io = nullptr;
        swr_free(&outputConverter);
        for (const auto &url : sources)
            if (url != keep)
                post([url](auto &backend) { emit backend.sourceReleased(url); }, false);
        sources.clear();
        if (!keep.isEmpty())
            sources.push_back(keep);
        clock.reset();
        pending.clear();
        pendingOffset = 0;
        writtenBytes = 0;
        lastPositionNotification = -1;
        consumed = 0;
        publicStart = 0;
        reportedReady = false;
        reportedNext = false;
        reportedNextFailure = false;
        started = false;
        drainedDsp = false;
        failed = false;
        deferred.reset();
        nextRequest = {};
    }
    void reset(const QUrl &url, qint64 seekMs, quint64 generation, bool resume, bool confirmSeek = false) {
        clear(url);
        epoch = generation;
        publicSource = url;
        publicBaseMs = seekMs;
        playing = resume;
        seeking = confirmSeek;
        if (url.isEmpty()) {
            timer->stop();
            return;
        }
        loaded = std::make_shared<PcmDecoder>(url, seekMs, 15 * 1024 * 1024);
        if (rate > 0)
            createRenderer();
        timer->start();
    }
    void createRenderer() {
        renderer = std::make_unique<PcmRenderer>(rate, loaded);
        dsp = std::make_unique<PcmDsp>(rate);
        dsp->setSettings(equalizer);
    }
    qint64 position() const {
        return publicBaseMs + (rate > 0 ? std::max<qint64>(0, consumed - publicStart) * 1000 / rate : 0);
    }
    void selectDevice(const QAudioDevice &value) {
        if (device == value)
            return;
        const qint64 mediaPosition = position();
        const auto url = publicSource;
        const bool resume = playing;
        clear(url);
        device = value;
        rate = 0;
        if (!device.isNull()) {
            format = device.preferredFormat();
            QAudioFormat preferred;
            preferred.setSampleRate(48000);
            preferred.setChannelCount(2);
            preferred.setSampleFormat(QAudioFormat::Float);
            if (device.isFormatSupported(preferred))
                format = preferred;
            rate = format.sampleRate();
        }
        post(
            [sampleRate = rate](auto &backend) {
                backend.m_sampleRate = sampleRate;
                emit backend.backendChanged();
            },
            false);
        if (!url.isEmpty())
            reset(url, mediaPosition, epoch, resume);
    }
    void preload(const PreloadRequest &request) {
        if (!renderer || !renderer->canPreload()) {
            if (deferred && deferred->source != request.source) {
                const auto url = deferred->source;
                sources.erase(std::remove(sources.begin(), sources.end(), url), sources.end());
                post([url](auto &backend) { emit backend.sourceReleased(url); }, false);
            }
            if (std::find(sources.begin(), sources.end(), request.source) == sources.end())
                sources.push_back(request.source);
            deferred = request;
            return;
        }
        nextDecoder = std::make_shared<PcmDecoder>(request.source, 0, 15 * 1024 * 1024);
        nextRequest = request;
        reportedNext = false;
        reportedNextFailure = false;
        if (std::find(sources.begin(), sources.end(), request.source) == sources.end())
            sources.push_back(request.source);
        renderer->preload(nextDecoder, request);
    }
    void cancelNext() {
        if (deferred) {
            const auto url = deferred->source;
            deferred.reset();
            sources.erase(std::remove(sources.begin(), sources.end(), url), sources.end());
            post([url](auto &backend) { emit backend.sourceReleased(url); }, false);
        }
        if (!renderer || !nextDecoder)
            return;
        const bool scheduled = renderer->hasScheduledTransition() || clock.transitionPending();
        if (scheduled) {
            reset(publicSource, position(), epoch, playing);
            return;
        }
        if (nextDecoder) {
            const auto url = nextDecoder->source;
            nextDecoder->cancel();
            renderer->cancelNext();
            nextDecoder.reset();
            sources.erase(std::remove(sources.begin(), sources.end(), url), sources.end());
            post([url](auto &backend) { emit backend.sourceReleased(url); }, false);
        }
        nextRequest = {};
        reportedNext = false;
    }
    void setPlaying(bool value) {
        playing = value;
        if (sink) {
            if (value)
                sink->resume();
            else
                sink->suspend();
        }
        if (!value) {
            pollClock();
            post([](auto &backend) { emit backend.stateChanged(PlayerState::Paused); });
        }
        if (value && !publicSource.isEmpty()) {
            timer->start();
            if (started)
                post([](auto &backend) { emit backend.stateChanged(PlayerState::Playing); });
        }
    }
    void pollClock() {
        if (!sink || rate <= 0)
            return;
        consumed = std::max(consumed,
                            std::min(writtenBytes / format.bytesPerFrame(), sink->processedUSecs() * rate / 1000000));
        if (consumed > 0 && !started && playing) {
            started = true;
            post([](auto &backend) { emit backend.stateChanged(PlayerState::Playing); });
        }
        for (auto &event : clock.advance(consumed)) {
            if (event.token) {
                publicSource = event.source;
                publicBaseMs = 0;
                publicStart = event.frame;
                loaded = nextDecoder;
                nextDecoder.reset();
                nextRequest = {};
                reportedNext = false;
                post([event](auto &backend) {
                    backend.m_source = event.source;
                    backend.m_metadata = event.info.metadata;
                    backend.m_duration = event.info.durationMs;
                    backend.m_seekable = event.info.seekable;
                    backend.m_position = 0;
                    emit backend.nextTrackStarted(event.token);
                    emit backend.durationChanged(backend.m_duration);
                    emit backend.metadataChanged();
                    emit backend.seekableChanged();
                });
            }
            if (!event.releasedSource.isEmpty()) {
                const auto url = event.releasedSource;
                sources.erase(std::remove(sources.begin(), sources.end(), url), sources.end());
                post([url](auto &backend) { emit backend.sourceReleased(url); }, false);
            }
            if (event.ended) {
                playing = false;
                post([](auto &backend) { emit backend.ended(); });
            }
        }
        if (seeking && (consumed > 0 || !playing) && reportedReady &&
            (loaded->info().decodedFrames > 0 || loaded->info().eof)) {
            seeking = false;
            post([value = position()](auto &backend) {
                backend.m_position = value;
                emit backend.seeked(value);
            });
        }
        const auto value = position();
        if (value != lastPositionNotification &&
            (lastPositionNotification < 0 || !playing || std::abs(value - lastPositionNotification) >= 20)) {
            lastPositionNotification = value;
            post([value](auto &backend) {
                if (backend.m_position != value) {
                    backend.m_position = value;
                    emit backend.positionChanged(value);
                }
            });
        }
    }
    bool openSink() {
        sink = std::make_unique<QAudioSink>(device, format);
        sink->setBufferSize(format.bytesForDuration(80000));
        sink->setVolume(float(volume));
        io = sink->start();
        AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO, target{};
        av_channel_layout_default(&target, format.channelCount());
        AVSampleFormat sampleFormat = AV_SAMPLE_FMT_FLT;
        if (format.sampleFormat() == QAudioFormat::Int16)
            sampleFormat = AV_SAMPLE_FMT_S16;
        else if (format.sampleFormat() == QAudioFormat::Int32)
            sampleFormat = AV_SAMPLE_FMT_S32;
        else if (format.sampleFormat() == QAudioFormat::UInt8)
            sampleFormat = AV_SAMPLE_FMT_U8;
        int status = swr_alloc_set_opts2(&outputConverter, &target, sampleFormat, rate, &stereo, AV_SAMPLE_FMT_FLT,
                                         rate, 0, nullptr);
        av_channel_layout_uninit(&target);
        if (status >= 0 && format.channelCount() == 1) {
            // Downmix after the limiter must not introduce a new +3 dB peak.
            const double monoMatrix[]{.5, .5};
            status = swr_set_matrix(outputConverter, monoMatrix, 2);
        }
        if (status >= 0 && sampleFormat == AV_SAMPLE_FMT_S16)
            av_opt_set_int(outputConverter, "dither_method", SWR_DITHER_TRIANGULAR, 0);
        if (status >= 0)
            status = swr_init(outputConverter);
        return io && status >= 0 && sink->error() == QAudio::NoError;
    }
    void outputError(const QString &message) {
        playing = false;
        failed = true;
        if (sink)
            sink->reset();
        post([message](auto &backend) {
            backend.suspendAudioOutput();
            emit backend.audioOutputError(message);
        });
    }
    void appendOutput(const std::vector<float> &samples) {
        if (samples.empty())
            return;
        const int frames = int(samples.size() / 2);
        QByteArray bytes(int(swr_get_out_samples(outputConverter, frames) * format.bytesPerFrame()), Qt::Uninitialized);
        uint8_t *out = reinterpret_cast<uint8_t *>(bytes.data());
        const uint8_t *in = reinterpret_cast<const uint8_t *>(samples.data());
        const int count = swr_convert(outputConverter, &out, int(bytes.size() / format.bytesPerFrame()), &in, frames);
        if (count < 0) {
            outputError("Unable to convert output PCM");
            return;
        }
        bytes.resize(count * format.bytesPerFrame());
        pending.append(bytes);
    }
    void tick() {
        if (!loaded || failed)
            return;
        // Audible boundaries precede errors discovered in the same timer tick.
        // In particular, an incoming stream that fails after it has started is
        // a current-track failure rather than a speculative preload failure.
        if (sink) pollClock();
        const auto info = loaded->info();
        if (!info.error.isEmpty()) {
            failed = true;
            playing = false;
            if (sink)
                sink->reset();
            post([info, eligible = consumed == 0 && info.fallbackEligible](auto &backend) {
                emit backend.decoderFailure(info.error, eligible);
            });
            return;
        }
        if (info.ready && !reportedReady) {
            reportedReady = true;
            post([info](auto &backend) {
                backend.m_metadata = info.metadata;
                backend.m_duration = info.durationMs;
                backend.m_seekable = info.seekable;
                emit backend.metadataChanged();
                emit backend.durationChanged(info.durationMs);
                emit backend.seekableChanged();
            });
            return; // Let the application issue its initial preload/final-track decision.
        }
        if (nextDecoder) {
            const auto nextInfo = nextDecoder->info();
            if ((!nextInfo.error.isEmpty() && !reportedNextFailure) ||
                (!reportedNext && nextInfo.eof && !nextDecoder->available())) {
                reportedNext = true;
                reportedNextFailure = true;
                post([token = nextRequest.token, message = nextInfo.error.isEmpty() ? QString("Empty audio source")
                                                                                    : nextInfo.error](auto &backend) {
                    emit backend.nextFailed(token, message);
                });
            } else if (!reportedNext && nextInfo.ready &&
                       nextDecoder->available() >= size_t(std::min(nextInfo.sampleRate / 4, 4096))) {
                reportedNext = true;
                post([token = nextRequest.token](auto &backend) { emit backend.nextReady(token); });
            } else if (!reportedNext && nextInfo.ready && nextInfo.eof && nextDecoder->available()) {
                reportedNext = true;
                post([token = nextRequest.token](auto &backend) { emit backend.nextReady(token); });
            }
        }
        if (!playing || !renderer || !info.ready || device.isNull()) {
            if (seeking && reportedReady && !playing && (info.decodedFrames > 0 || info.eof)) {
                seeking = false;
                post([value = publicBaseMs](auto &backend) {
                    backend.m_position = value;
                    emit backend.seeked(value);
                });
            }
            return;
        }
        if (!sink && !openSink()) {
            outputError("Unable to open audio output");
            return;
        }
        if (sink->error() != QAudio::NoError && sink->error() != QAudio::UnderrunError) {
            outputError("Audio output failed");
            return;
        }
        pollClock();
        if (!playing)
            return;
        int budget = 8192;
        while (budget > 0 && sink->bytesFree() >= format.bytesPerFrame()) {
            if (pendingOffset >= pending.size()) {
                pending.clear();
                pendingOffset = 0;
                // Give the application the buffered output interval to decide
                // the following track, preserving resampler history for short
                // tracks. An unresolved source falls back before underrun.
                if (renderer->waitingForPlan() && !clock.transitionPending() &&
                    writtenBytes / format.bytesPerFrame() - consumed + renderer->bufferedFrames() <= rate / 100)
                    renderer->releasePlanWait();
                auto pcm = renderer->render(512);
                budget -= 512;
                clock.append(renderer->takeBoundaries());
                appendOutput(dsp->process(pcm));
                if (renderer->finished() && !drainedDsp) {
                    drainedDsp = true;
                    appendOutput(dsp->finish());
                }
                if (!dsp->error().isEmpty()) {
                    outputError(dsp->error());
                    return;
                }
                if (pending.isEmpty())
                    break;
            }
            const qint64 bytes = std::min<qint64>(sink->bytesFree(), pending.size() - pendingOffset);
            const qint64 written = io->write(pending.constData() + pendingOffset, bytes);
            if (written < 0) {
                outputError("Audio output write failed");
                return;
            }
            if (!written)
                break;
            pendingOffset += written;
            writtenBytes += written;
        }
        if (deferred && renderer->canPreload() && !nextDecoder) {
            auto request = *deferred;
            deferred.reset();
            preload(request);
        }
        pollClock();
    }
    QPointer<FFmpegPlaybackBackend> owner;
    quint64 epoch = 0;
    QTimer *timer;
    QAudioDevice device;
    QAudioFormat format;
    int rate = 0;
    double volume = .8;
    bool playing = false, seeking = false, reportedReady = false, reportedNext = false, reportedNextFailure = false,
         started = false, drainedDsp = false, failed = false;
    QUrl publicSource;
    qint64 publicBaseMs = 0, publicStart = 0, consumed = 0, writtenBytes = 0, lastPositionNotification = -1;
    EqualizerSettings equalizer;
    std::unique_ptr<QAudioSink> sink;
    QIODevice *io = nullptr;
    SwrContext *outputConverter = nullptr;
    std::shared_ptr<PcmDecoder> loaded, nextDecoder;
    std::unique_ptr<PcmRenderer> renderer;
    std::unique_ptr<PcmDsp> dsp;
    PreloadRequest nextRequest;
    std::optional<PreloadRequest> deferred;
    std::vector<QUrl> sources;
    PcmOutputClock clock;
    QByteArray pending;
    qint64 pendingOffset = 0;
};
FFmpegPlaybackBackend::FFmpegPlaybackBackend() {
    m_outputs.setParent(this);
    m_worker = new PcmOutputWorker(this);
    m_worker->moveToThread(&m_thread);
    m_thread.setObjectName("PCM audio output");
    m_thread.start();
    connect(&m_outputs, &AudioOutputDevices::changed, this, &IPlaybackBackend::audioOutputsChanged);
    connect(&m_outputs, &AudioOutputDevices::deviceChanged, this, [this] {
        const auto device = m_outputs.device();
        dispatch([device](auto &worker) { worker.selectDevice(device); });
    });
    connect(&m_outputs, &AudioOutputDevices::availabilityChanged, this, [this](bool available) {
        emit audioOutputAvailabilityChanged(available);
        if (!available)
            pause();
    });
}
FFmpegPlaybackBackend::~FFmpegPlaybackBackend() {
    ++m_generation;
    QMetaObject::invokeMethod(m_worker, [worker = m_worker] { delete worker; }, Qt::BlockingQueuedConnection);
    m_thread.quit();
    m_thread.wait();
}
void FFmpegPlaybackBackend::dispatch(std::function<void(PcmOutputWorker &)> operation) {
    QMetaObject::invokeMethod(
        m_worker, [worker = m_worker, operation = std::move(operation)] { operation(*worker); }, Qt::QueuedConnection);
}
void FFmpegPlaybackBackend::setSource(const QUrl &url) {
    m_source = url;
    m_position = 0;
    m_duration = 0;
    m_metadata = {};
    m_seekable = false;
    const auto generation = ++m_generation;
    dispatch([url, generation](auto &worker) { worker.reset(url, 0, generation, false); });
    emit seekableChanged();
}
void FFmpegPlaybackBackend::play() {
    if (!audioOutputAvailable()) {
        suspendAudioOutput();
        return;
    }
    emit stateChanged(PlayerState::Loading);
    const auto url = m_source;
    const auto generation = m_generation;
    dispatch([url, generation](auto &worker) {
        if (!worker.loaded && !url.isEmpty())
            worker.reset(url, 0, generation, true);
        else
            worker.setPlaying(true);
    });
}
void FFmpegPlaybackBackend::pause() {
    dispatch([](auto &worker) { worker.setPlaying(false); });
}
void FFmpegPlaybackBackend::stop() {
    m_position = 0;
    const auto generation = ++m_generation;
    const auto url = m_source;
    dispatch([url, generation](auto &worker) {
        worker.timer->stop();
        worker.clear(url);
        worker.epoch = generation;
        worker.publicSource = url;
        worker.publicBaseMs = 0;
        worker.playing = false;
    });
    emit positionChanged(0);
    emit stateChanged(PlayerState::Stopped);
}
void FFmpegPlaybackBackend::seek(qint64 position) {
    if (!m_seekable)
        return;
    const auto generation = ++m_generation;
    const auto url = m_source;
    const auto target = std::clamp<qint64>(position, 0, std::max<qint64>(0, m_duration));
    dispatch([url, generation, target](auto &worker) { worker.reset(url, target, generation, worker.playing, true); });
}
void FFmpegPlaybackBackend::setVolume(double volume) {
    m_volume = volume;
    dispatch([volume](auto &worker) {
        worker.volume = volume;
        if (worker.sink)
            worker.sink->setVolume(float(volume));
    });
}
void FFmpegPlaybackBackend::suspendAudioOutput() {
    m_outputs.suspend();
    pause();
}
void FFmpegPlaybackBackend::shutdown() {
    ++m_generation;
    m_source = QUrl{};
    QMetaObject::invokeMethod(
        m_worker,
        [worker = m_worker] {
            worker->timer->stop();
            worker->playing = false;
            worker->clear({});
            worker->publicSource = QUrl{};
        },
        Qt::BlockingQueuedConnection);
}
void FFmpegPlaybackBackend::preloadNext(const PreloadRequest &request) {
    dispatch([request](auto &worker) { worker.preload(request); });
}
void FFmpegPlaybackBackend::cancelPreload() {
    dispatch([](auto &worker) { worker.cancelNext(); });
}
void FFmpegPlaybackBackend::authorizeTransition(quint64 token) {
    dispatch([token](auto &worker) {
        if (worker.renderer)
            worker.renderer->authorize(token);
    });
}
void FFmpegPlaybackBackend::finishAfterCurrent() {
    dispatch([](auto &worker) {
        if (worker.renderer)
            worker.renderer->finishAfterCurrent();
    });
}
void FFmpegPlaybackBackend::setEqualizer(const EqualizerSettings &settings) {
    dispatch([settings](auto &worker) {
        worker.equalizer = settings;
        if (worker.dsp)
            worker.dsp->setSettings(settings);
    });
}
} // namespace nekotune
