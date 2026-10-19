#include "infrastructure/playback/pcm_renderer.h"
#include "infrastructure/playback/pcm_dsp.h"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <span>
#include <utility>
extern "C" {
#include <libswresample/swresample.h>
}
namespace nekotune {
namespace {
struct Reader {
    std::shared_ptr<PcmDecoder> decoder;
    SwrContext *resampler = nullptr;
    int rate;
    qint64 nativeRead = 0, nativeOrigin = 0, produced = 0;
    bool drained = false, handedOff = false;
    bool gainInitialized = false;
    PcmGainEnvelope gain;
    std::optional<ReplayGainData> analysis;
    void updateGain(const ReplayGainSettings &settings) {
        auto data = decoder->info().replayGain;
        if (analysis) data = replaceReplayGainAnalysis(data, *analysis);
        gain.setGain(resolveReplayGain(settings, data).gainDb, rate, !gainInitialized);
        gainInitialized = true;
    }
    std::vector<float> buffer;
    size_t offset = 0;
    static constexpr size_t CapacitySamples = 12 * 1024 * 1024 / sizeof(float);
    explicit Reader(std::shared_ptr<PcmDecoder> source, int sampleRate) : decoder(std::move(source)), rate(sampleRate) {
        buffer.reserve(CapacitySamples);
    }
    ~Reader() { swr_free(&resampler); }
    size_t available() const { return (buffer.size() - offset) / 2; }
    qint64 expected() const {
        const int inputRate = decoder->info().sampleRate;
        if (!inputRate)
            return 0;
        return av_rescale_rnd(nativeOrigin + nativeRead, rate, inputRate, AV_ROUND_NEAR_INF) -
               av_rescale_rnd(nativeOrigin, rate, inputRate, AV_ROUND_NEAR_INF);
    }
    void append(std::span<const float> samples) {
        if (offset && (offset >= buffer.size() / 2 || buffer.size() + samples.size() > CapacitySamples)) {
            buffer.erase(buffer.begin(), buffer.begin() + std::ptrdiff_t(offset));
            offset = 0;
        }
        buffer.insert(buffer.end(), samples.begin(), samples.end());
        produced += qint64(samples.size() / 2);
    }
    void fill(size_t target, bool drain) {
        const auto info = decoder->info();
        if (!info.ready || !info.error.isEmpty() || drained || available() >= target)
            return;
        if (!resampler) {
            AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
            if (swr_alloc_set_opts2(&resampler, &stereo, AV_SAMPLE_FMT_FLT, rate, &stereo, AV_SAMPLE_FMT_FLT,
                                    info.sampleRate, 0, nullptr) < 0 ||
                swr_init(resampler) < 0) {
                drained = true;
                return;
            }
        }
        // Bound per-tick work even when a long fade window is requested.
        size_t budget = 16384;
        std::vector<float> input(4096 * 2);
        while (available() < target && budget) {
            const size_t needed = std::max<size_t>(1, (target - available()) * size_t(info.sampleRate) / size_t(rate));
            const size_t count = decoder->read(input.data(), std::min({size_t(4096), budget, needed}));
            if (!count)
                break;
            budget -= count;
            nativeRead += qint64(count);
            std::vector<float> output(size_t(swr_get_out_samples(resampler, int(count))) * 2);
            uint8_t *destination = reinterpret_cast<uint8_t *>(output.data());
            const uint8_t *source = reinterpret_cast<const uint8_t *>(input.data());
            const int converted = swr_convert(resampler, &destination, int(output.size() / 2), &source, int(count));
            if (converted < 0) {
                drained = true;
                return;
            }
            output.resize(size_t(converted) * 2);
            append(output);
        }
        if (drain && decoder->info().eof && !decoder->available()) {
            for (;;) {
                std::vector<float> output(size_t(std::max(4096, swr_get_out_samples(resampler, 0))) * 2);
                uint8_t *destination = reinterpret_cast<uint8_t *>(output.data());
                const int converted = swr_convert(resampler, &destination, int(output.size() / 2), nullptr, 0);
                if (converted <= 0)
                    break;
                output.resize(size_t(converted) * 2);
                append(output);
            }
            drained = true;
        }
    }
    const float *data() const { return buffer.data() + offset; }
    void consume(size_t frames) { offset += frames * 2; }
};
} // namespace
struct PcmRenderer::Impl {
    int rate;
    std::unique_ptr<Reader> current, next;
    PreloadRequest request;
    ReplayGainSettings replayGain;
    bool authorized = false, done = false, fading = false, awaitingPlan = true, followingFinal = false;
    qint64 frames = 0, fadeFrames = 0, faded = 0;
    std::vector<PcmBoundary> boundaries;
    explicit Impl(int sampleRate, std::shared_ptr<PcmDecoder> decoder)
        : rate(sampleRate), current(std::make_unique<Reader>(std::move(decoder), sampleRate)) {}
    void startBoundary(qint64 frame) {
        boundaries.push_back({frame, request.token, next->decoder->source, next->decoder->info(), {}, false});
    }
    void releaseBoundary(qint64 frame) {
        PcmBoundary event;
        event.frame = frame;
        event.releasedSource = current->decoder->source;
        boundaries.push_back(std::move(event));
    }
    bool sameRateGapless() const {
        return next && authorized && request.strategy == TransitionStrategy::Gapless && next->decoder->info().ready &&
               next->decoder->info().error.isEmpty() &&
               current->decoder->info().sampleRate == next->decoder->info().sampleRate;
    }
    void prepare(size_t requested) {
        const bool crossfade = next && authorized && request.strategy == TransitionStrategy::Crossfade;
        const size_t lookahead =
            std::min(Reader::CapacitySamples / 2 - 65536,
                     crossfade ? size_t((request.crossfadeSeconds * 2 + 1) * rate) : size_t(rate * 2));
        const bool continuous = sameRateGapless();
        current->fill(std::max(requested, lookahead), !continuous && !awaitingPlan);
        if (crossfade)
            next->fill(std::max(requested, lookahead), true);
        if (continuous && !current->handedOff && current->decoder->info().eof && !current->decoder->available()) {
            // swresample retains its filter history and fractional phase across
            // same-rate sources. The next input first completes the old tail.
            if (!next->resampler) {
                next->resampler = current->resampler;
                current->resampler = nullptr;
                next->nativeOrigin = current->nativeOrigin + current->nativeRead;
            }
            const size_t missing = size_t(std::max<qint64>(0, current->expected() - current->produced));
            next->fill(std::max(requested, missing + 4096), false);
            if (next->available() < missing && next->decoder->info().eof && !next->decoder->available())
                next->fill(missing, true); // Final sub-filter-length sources still need their drain.
            if (next->available() >= missing) {
                current->append(std::span(next->data(), missing * 2));
                next->consume(missing);
                next->produced -= qint64(missing);
                current->drained = true;
                current->handedOff = true;
            }
        }
        if (!fading && crossfade && current->drained && next->available()) {
            const qint64 oldTotal =
                av_rescale(current->decoder->info().decodedFrames, rate, current->decoder->info().sampleRate);
            const auto nextInfo = next->decoder->info();
            const qint64 newLimit = nextInfo.eof ? av_rescale(nextInfo.decodedFrames, rate, nextInfo.sampleRate) / 2
                                                 : qint64(next->available() / 2);
            const qint64 length = std::min<qint64>({qint64(request.crossfadeSeconds * rate), oldTotal / 2, newLimit});
            if (length > 1 && current->available() <= size_t(length) && next->available() >= current->available()) {
                fadeFrames = qint64(current->available());
                faded = 0;
                fading = true;
                startBoundary(frames + 1); // sin(0) is silent; identity follows the first nonzero incoming weight.
            }
        }
    }
};
PcmRenderer::PcmRenderer(int rate, std::shared_ptr<PcmDecoder> current)
    : d(std::make_unique<Impl>(rate, std::move(current))) {}
PcmRenderer::~PcmRenderer() = default;
void PcmRenderer::preload(std::shared_ptr<PcmDecoder> decoder, const PreloadRequest &request) {
    if (!canPreload())
        return;
    d->next = std::make_unique<Reader>(std::move(decoder), d->rate);
    d->request = request;
    d->authorized = false;
    d->awaitingPlan = true;
    d->followingFinal = false;
}
void PcmRenderer::finishAfterCurrent() {
    if (d->next)
        d->followingFinal = true;
    else
        d->awaitingPlan = false;
}
bool PcmRenderer::waitingForPlan() const { return d->awaitingPlan; }
void PcmRenderer::releasePlanWait() { d->awaitingPlan = false; }
qint64 PcmRenderer::bufferedFrames() const {
    const auto info = d->current->decoder->info();
    return qint64(d->current->available()) +
           (info.sampleRate > 0 ? av_rescale(qint64(d->current->decoder->available()), d->rate, info.sampleRate) : 0);
}
void PcmRenderer::authorize(quint64 token) {
    if (d->next && d->request.token == token) {
        d->authorized = true;
        d->awaitingPlan = false;
    }
}
bool PcmRenderer::canPreload() const { return !d->next && !d->done; }
bool PcmRenderer::hasScheduledTransition() const { return d->fading || (d->current && d->current->handedOff); }
void PcmRenderer::cancelNext() {
    if (hasScheduledTransition())
        return; // Output owner resets to its consumed position for this case.
    d->next.reset();
    d->authorized = false;
}
qint64 PcmRenderer::framesRendered() const { return d->frames; }
bool PcmRenderer::finished() const { return d->done; }
void PcmRenderer::setReplayGain(const ReplayGainSettings &settings) { d->replayGain = settings; }
void PcmRenderer::updateReplayGainData(const QUrl &source, const ReplayGainData &data) {
    for (auto *reader : {d->current.get(), d->next.get()})
        if (reader && reader->decoder->source == source) {
            reader->analysis = data;
            reader->decoder->updateReplayGainData(data);
        }
}
std::vector<PcmBoundary> PcmRenderer::takeBoundaries() { return std::exchange(d->boundaries, {}); }
std::vector<float> PcmRenderer::render(size_t requested) {
    std::vector<float> output;
    output.reserve(requested * 2);
    while (output.size() / 2 < requested && !d->done) {
        d->prepare(requested - output.size() / 2);
        auto &current = *d->current;
        if (!current.available()) {
            if (!current.drained)
                break;
            if (d->next && d->authorized && d->next->decoder->info().ready &&
                d->next->decoder->info().error.isEmpty()) {
                d->startBoundary(d->frames);
                d->releaseBoundary(d->frames);
                // Keep a continuous 20 ms gain envelope at a gapless boundary.
                d->next->gain = current.gain;
                d->next->gainInitialized = true;
                d->current = std::move(d->next);
                d->authorized = false;
                d->awaitingPlan = !d->followingFinal;
                d->followingFinal = false;
                continue;
            }
            PcmBoundary end;
            end.frame = d->frames;
            end.ended = true;
            d->boundaries.push_back(std::move(end));
            d->done = true;
            break;
        }
        size_t count = std::min(current.available(), requested - output.size() / 2);
        current.updateGain(d->replayGain);
        if (d->fading) {
            d->next->updateGain(d->replayGain);
            count = std::min({count, d->next->available(), size_t(d->fadeFrames - d->faded)});
            if (!count)
                break;
            const float *a = current.data(), *b = d->next->data();
            for (size_t i = 0; i < count; ++i) {
                const double phase = double(d->faded + qint64(i)) / double(d->fadeFrames - 1);
                const float out = float(std::cos(phase * std::numbers::pi / 2)) * current.gain.next();
                const float in = float(std::sin(phase * std::numbers::pi / 2)) * d->next->gain.next();
                output.push_back(a[i * 2] * out + b[i * 2] * in);
                output.push_back(a[i * 2 + 1] * out + b[i * 2 + 1] * in);
            }
            current.consume(count);
            d->next->consume(count);
            d->faded += qint64(count);
            d->frames += qint64(count);
            if (d->faded == d->fadeFrames) {
                d->releaseBoundary(d->frames);
                d->current = std::move(d->next);
                d->authorized = false;
                d->awaitingPlan = !d->followingFinal;
                d->followingFinal = false;
                d->fading = false;
            }
        } else {
            // Do not step past the possible fade boundary within a large render block.
            if (d->next && d->authorized && d->request.strategy == TransitionStrategy::Crossfade && current.drained) {
                const auto oldInfo = current.decoder->info(), newInfo = d->next->decoder->info();
                const qint64 newLimit = newInfo.eof && newInfo.sampleRate > 0
                                            ? av_rescale(newInfo.decodedFrames, d->rate, newInfo.sampleRate) / 2
                                            : qint64(d->next->available() / 2);
                const qint64 fadeLimit =
                    std::min<qint64>({qint64(d->request.crossfadeSeconds * d->rate),
                                      av_rescale(oldInfo.decodedFrames, d->rate, oldInfo.sampleRate) / 2, newLimit});
                if (fadeLimit > 1 && current.available() > size_t(fadeLimit))
                    count = std::min(count, current.available() - size_t(fadeLimit));
            }
            for (size_t i = 0; i < count; ++i) {
                const float gain = current.gain.next();
                output.push_back(current.data()[i * 2] * gain);
                output.push_back(current.data()[i * 2 + 1] * gain);
            }
            current.consume(count);
            d->frames += qint64(count);
        }
    }
    return output;
}
} // namespace nekotune
