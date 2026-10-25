#include "infrastructure/playback/pcm_decoder.h"
#include "infrastructure/library/audio_duration.h"
#include "infrastructure/library/loudness_analysis.h"
#include <QFile>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <memory>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/intreadwrite.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
}
namespace nekotune {
namespace {
qint64 nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}
QString avError(int code) {
    char buffer[AV_ERROR_MAX_STRING_SIZE]{};
    av_strerror(code, buffer, sizeof(buffer));
    return QString::fromUtf8(buffer);
}
QString tag(AVDictionary *tags, const char *name) {
    auto *entry = av_dict_get(tags, name, nullptr, 0);
    return entry ? QString::fromUtf8(entry->value) : QString{};
}
} // namespace
PcmDecoder::PcmDecoder(QUrl url, qint64 seekMs, size_t capacityBytes)
    : source(std::move(url)),
      m_buffer(std::max<size_t>((capacityBytes ? capacityBytes : (source.isLocalFile() ? 3 : 8) * 1024 * 1024) /
                                   sizeof(float), 8192) & ~size_t(1)) {
    static std::once_flag networkInitialization;
    std::call_once(networkInitialization, [] { avformat_network_init(); });
    m_thread = std::thread([this, seekMs] { decode(seekMs); });
}
PcmDecoder::~PcmDecoder() {
    cancel();
    if (m_thread.joinable())
        m_thread.join();
}
void PcmDecoder::cancel() {
    {
        std::lock_guard lock(m_mutex);
        m_cancelled = true;
    }
    m_space.notify_all();
}
int PcmDecoder::interrupted(void *opaque) {
    auto *self = static_cast<PcmDecoder *>(opaque);
    return self->m_cancelled || (self->m_deadline.load() && nowMs() >= self->m_deadline.load());
}
DecodedAudioInfo PcmDecoder::info() const {
    std::lock_guard lock(m_mutex);
    return m_info;
}
void PcmDecoder::updateReplayGainData(const ReplayGainData &analysis) {
    std::lock_guard lock(m_mutex);
    m_analysis = analysis;
    m_info.replayGain = replaceReplayGainAnalysis(m_info.replayGain, analysis);
}
size_t PcmDecoder::available() const {
    std::lock_guard lock(m_mutex);
    return m_samples / 2;
}
size_t PcmDecoder::read(float *out, size_t frames) {
    std::unique_lock lock(m_mutex);
    const size_t count = std::min(frames, m_samples / 2) * 2;
    if (!count)
        return 0;
    const size_t first = std::min(count, m_buffer.size() - m_offset);
    std::memcpy(out, m_buffer.data() + m_offset, first * sizeof(float));
    if (first < count)
        std::memcpy(out + first, m_buffer.data(), (count - first) * sizeof(float));
    m_offset = (m_offset + count) % m_buffer.size();
    m_samples -= count;
    lock.unlock();
    m_space.notify_one();
    return count / 2;
}
bool PcmDecoder::append(std::span<const float> samples) {
    std::unique_lock lock(m_mutex);
    while (!samples.empty()) {
        m_space.wait(lock, [&] { return m_cancelled || m_samples < m_buffer.size(); });
        if (m_cancelled)
            return false;
        const size_t offset = (m_offset + m_samples) % m_buffer.size();
        const size_t count = std::min({samples.size(), m_buffer.size() - m_samples, m_buffer.size() - offset});
        std::memcpy(m_buffer.data() + offset, samples.data(), count * sizeof(float));
        m_samples += count;
        m_info.decodedFrames += qint64(count / 2);
        samples = samples.subspan(count);
    }
    return true;
}
void PcmDecoder::decode(qint64 seekMs) {
    AVFormatContext *format = avformat_alloc_context();
    AVCodecContext *codec = nullptr;
    SwrContext *converter = nullptr;
    AVPacket *packet = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    auto cleanup = [&] {
        swr_free(&converter);
        avcodec_free_context(&codec);
        avformat_close_input(&format);
        av_packet_free(&packet);
        av_frame_free(&frame);
    };
    auto fail = [&](int code, bool eligible = false) {
        std::lock_guard lock(m_mutex);
        m_info.error = avError(code);
        m_info.fallbackEligible = eligible;
        m_info.eof = true;
    };
    if (!format || !packet || !frame) {
        fail(AVERROR(ENOMEM));
        cleanup();
        return;
    }
    format->interrupt_callback = {&PcmDecoder::interrupted, this};
    AVDictionary *options = nullptr;
    // Local proxy URLs already carry authentication and refresh upstream URLs.
    av_dict_set(&options, "rw_timeout", "15000000", 0);
    if (source.isLocalFile()) av_dict_set(&options, "protocol_whitelist", "file", 0);
#ifdef Q_OS_WIN
    const QByteArray filename = source.isLocalFile() ? source.toLocalFile().toUtf8() : source.toEncoded();
#else
    const QByteArray filename = source.isLocalFile() ? QFile::encodeName(source.toLocalFile()) : source.toEncoded();
#endif
    m_deadline = nowMs() + 15000;
    int result = avformat_open_input(&format, filename.constData(), nullptr, &options);
    av_dict_free(&options);
    if (result < 0) {
        fail(result, result == AVERROR_INVALIDDATA || result == AVERROR_DEMUXER_NOT_FOUND);
        cleanup();
        return;
    }
    result = avformat_find_stream_info(format, nullptr);
    if (result < 0) {
        fail(result, result == AVERROR_INVALIDDATA);
        cleanup();
        return;
    }
    const AVCodec *implementation = nullptr;
    const int streamIndex = av_find_best_stream(format, AVMEDIA_TYPE_AUDIO, -1, -1, &implementation, 0);
    if (streamIndex < 0) {
        fail(streamIndex, true);
        cleanup();
        return;
    }
    auto *stream = format->streams[streamIndex];
    codec = avcodec_alloc_context3(implementation);
    if (!codec) {
        fail(AVERROR(ENOMEM));
        cleanup();
        return;
    }
    result = avcodec_parameters_to_context(codec, stream->codecpar);
    // Export skip/discard side data and trim exactly once before resampling.
    codec->flags2 |= AV_CODEC_FLAG2_SKIP_MANUAL;
    codec->thread_count = 1;
    if (result >= 0)
        result = avcodec_open2(codec, implementation, nullptr);
    if (result < 0) {
        fail(result, true);
        cleanup();
        return;
    }
    if (codec->sample_rate <= 0 || codec->ch_layout.nb_channels <= 0) {
        fail(AVERROR_INVALIDDATA, true);
        cleanup();
        return;
    }
    AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
    result = swr_alloc_set_opts2(&converter, &stereo, AV_SAMPLE_FMT_FLT, codec->sample_rate, &codec->ch_layout,
                                 codec->sample_fmt, codec->sample_rate, 0, nullptr);
    if (result >= 0)
        result = swr_init(converter);
    if (result < 0) {
        fail(result, true);
        cleanup();
        return;
    }
    const qint64 start = stream->start_time == AV_NOPTS_VALUE ? 0 : stream->start_time;
    if (seekMs > 0) {
        const auto target = start + av_rescale_q(seekMs, AVRational{1, 1000}, stream->time_base);
        // Some demuxers round a seek to the following AAC packet. Decode a
        // bounded preroll interval, then discard by sample timestamp below.
        const auto preroll = av_rescale_q(1000, AVRational{1, 1000}, stream->time_base);
        const auto anchor = std::max<qint64>(start, target - preroll);
        result = avformat_seek_file(format, streamIndex, INT64_MIN, anchor, anchor, AVSEEK_FLAG_BACKWARD);
        if (result < 0) {
            fail(result);
            cleanup();
            return;
        }
        avcodec_flush_buffers(codec);
    }
    const auto gainData = withReplayGainFallback(readReplayGainTags(format->metadata, stream->metadata),
        source.isLocalFile() ? readLoudnessCache(source.toLocalFile()) : ReplayGainData{});
    {
        std::lock_guard lock(m_mutex);
        m_info.sampleRate = codec->sample_rate;
        m_info.replayGain = m_analysis ? replaceReplayGainAnalysis(gainData, *m_analysis) : gainData;
        m_info.durationMs = stream->duration != AV_NOPTS_VALUE
                                ? av_rescale_q(stream->duration, stream->time_base, AVRational{1, 1000})
                                : (format->duration == AV_NOPTS_VALUE ? 0 : format->duration / 1000);
        m_info.seekable = format->pb && (format->pb->seekable & AVIO_SEEKABLE_NORMAL);
        m_info.metadata = {tag(format->metadata, "title"), tag(format->metadata, "artist"),
                           tag(format->metadata, "album")};
        if (m_info.metadata.artist.isEmpty())
            m_info.metadata.artist = tag(format->metadata, "album_artist");
        if (m_info.metadata.title.isEmpty())
            m_info.metadata.title = tag(stream->metadata, "title");
        m_info.ready = true;
    }
    bool flushing = false;
    qint64 fallbackPosition = 0;
    std::vector<const uint8_t *> inputs(size_t(codec->ch_layout.nb_channels));
    std::vector<float> samples;
    while (!m_cancelled) {
        m_deadline = nowMs() + 15000;
        result = avcodec_receive_frame(codec, frame);
        if (result == AVERROR_EOF)
            break;
        if (result == AVERROR(EAGAIN)) {
            if (flushing) {
                fail(AVERROR_INVALIDDATA);
                break;
            }
            for (;;) {
                result = av_read_frame(format, packet);
                if (result < 0 || packet->stream_index == streamIndex)
                    break;
                av_packet_unref(packet);
            }
            if (result == AVERROR_EOF) {
                result = avcodec_send_packet(codec, nullptr);
                flushing = true;
            } else if (result >= 0) {
                result = avcodec_send_packet(codec, packet);
                av_packet_unref(packet);
            }
            if (result < 0 && result != AVERROR_EOF) {
                fail(result);
                break;
            }
            continue;
        }
        if (result < 0) {
            fail(result);
            break;
        }
        if (frame->sample_rate != codec->sample_rate || frame->format != codec->sample_fmt ||
            av_channel_layout_compare(&frame->ch_layout, &codec->ch_layout)) {
            fail(AVERROR_INPUT_CHANGED);
            break;
        }
        int skip = 0, discard = 0;
        if (auto *side = av_frame_get_side_data(frame, AV_FRAME_DATA_SKIP_SAMPLES); side && side->size >= 8) {
            skip = int(std::min<uint32_t>(AV_RL32(side->data), uint32_t(frame->nb_samples)));
            discard = int(std::min<uint32_t>(AV_RL32(side->data + 4), uint32_t(frame->nb_samples - skip)));
        }
        const qint64 pts = frame->best_effort_timestamp;
        const qint64 frameStart = pts == AV_NOPTS_VALUE
                                      ? fallbackPosition
                                      : av_rescale_q(pts - start, stream->time_base, AVRational{1, codec->sample_rate});
        const qint64 targetFrame = av_rescale_q(seekMs, AVRational{1, 1000}, AVRational{1, codec->sample_rate});
        if (seekMs > 0)
            skip = int(std::min<qint64>(frame->nb_samples - discard, std::max<qint64>(skip, targetFrame - frameStart)));
        fallbackPosition = frameStart + frame->nb_samples;
        // MP4 edit-list duration is authoritative for AAC's final partial
        // packet; unlike LAME/Opus it may not export end-discard side data.
        if (codec->codec_id == AV_CODEC_ID_AAC && QByteArray(format->iformat->name).contains("mov") &&
            stream->duration != AV_NOPTS_VALUE) {
            const auto end = av_rescale_q(stream->duration, stream->time_base, AVRational{1, codec->sample_rate});
            discard = int(std::min<qint64>(frame->nb_samples - skip,
                                           std::max<qint64>(discard, frameStart + frame->nb_samples - end)));
        }
        const int count = frame->nb_samples - skip - discard;
        if (count > 0) {
            const int planar = av_sample_fmt_is_planar(codec->sample_fmt);
            const int stride = av_get_bytes_per_sample(codec->sample_fmt) * (planar ? 1 : codec->ch_layout.nb_channels);
            for (size_t i = 0; i < (planar ? inputs.size() : 1); ++i)
                inputs[i] = frame->extended_data[i] + skip * stride;
            samples.resize(size_t(swr_get_out_samples(converter, count)) * 2);
            uint8_t *out = reinterpret_cast<uint8_t *>(samples.data());
            result = swr_convert(converter, &out, int(samples.size() / 2), inputs.data(), count);
            if (result < 0) {
                fail(result);
                break;
            }
            samples.resize(size_t(result) * 2);
            if (std::any_of(samples.begin(), samples.end(), [](float value) { return !std::isfinite(value); })) {
                fail(AVERROR_INVALIDDATA);
                break;
            }
            m_deadline = 0; // Backpressure is not an input timeout.
            if (!append(samples))
                break;
        }
        av_frame_unref(frame);
    }
    if (!m_cancelled) {
        std::lock_guard lock(m_mutex);
        m_info.eof = true;
        if (m_info.error.isEmpty())
            m_info.durationMs =
                seekMs + av_rescale_q(m_info.decodedFrames, AVRational{1, codec->sample_rate}, AVRational{1, 1000});
    }
    cleanup();
}
} // namespace nekotune
