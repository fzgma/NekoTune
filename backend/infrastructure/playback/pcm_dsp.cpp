#include "infrastructure/playback/pcm_dsp.h"
#include <algorithm>
#include <cmath>
#include <cstring>
extern "C" {
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
}
namespace nekotune {
struct PcmDsp::Impl {
    AVFilterGraph *graph = avfilter_graph_alloc();
    AVFilterContext *source = nullptr, *sink = nullptr, *volume = nullptr;
    std::array<AVFilterContext *, 10> bands{};
    EqualizerSettings target;
    std::array<double, 10> gains{};
    double preamp = 0;
    int rate;
    int rampRemaining = 0;
    int64_t pts = 0;
    QString problem;
    explicit Impl(int sampleRate) : rate(sampleRate) {
        if (!graph) {
            problem = "Unable to allocate DSP graph";
            return;
        }
        graph->nb_threads = 1;
        const auto args =
            QString("sample_rate=%1:sample_fmt=flt:channel_layout=stereo:time_base=1/%1").arg(rate).toUtf8();
        if (avfilter_graph_create_filter(&source, avfilter_get_by_name("abuffer"), "input", args.constData(), nullptr,
                                         graph) < 0) {
            problem = "Unable to create DSP input";
            return;
        }
        AVFilterContext *last = source;
        auto add = [&](AVFilterContext **context, const char *filter, const QByteArray &name,
                       const QByteArray &options) {
            if (!problem.isEmpty())
                return;
            const auto *implementation = avfilter_get_by_name(filter);
            if (!implementation ||
                avfilter_graph_create_filter(context, implementation, name.constData(), options.constData(), nullptr,
                                             graph) < 0 ||
                avfilter_link(last, 0, *context, 0) < 0) {
                problem = QString("Unable to create DSP filter: %1").arg(QString::fromUtf8(filter));
                return;
            }
            last = *context;
        };
        add(&volume, "volume", "preamp", "volume=1:precision=float");
        for (size_t i = 0; i < bands.size(); ++i) {
            if (EqualizerFrequencies[i] * 2 >= rate)
                continue;
            add(&bands[i], "equalizer", "eq" + QByteArray::number(i),
                "f=" + QByteArray::number(EqualizerFrequencies[i]) + ":t=o:w=1:g=0:r=f32");
        }
        AVFilterContext *limiter = nullptr;
        add(&limiter, "alimiter", "limiter", "limit=1:level=false:attack=5:release=50:latency=true");
        AVFilterContext *outputFormat = nullptr;
        add(&outputFormat, "aformat", "float_output", "sample_fmts=flt:channel_layouts=stereo");
        add(&sink, "abuffersink", "output", {});
        if (problem.isEmpty() && avfilter_graph_config(graph, nullptr) < 0)
            problem = "Unable to configure DSP graph";
    }
    ~Impl() { avfilter_graph_free(&graph); }
    void command(AVFilterContext *filter, const char *key, double value) {
        if (!filter || !problem.isEmpty())
            return;
        const auto argument = QByteArray::number(value, 'g', 12);
        if (avfilter_process_command(filter, key, argument.constData(), nullptr, 0, 0) < 0)
            problem = QString("Unable to update DSP parameter: %1").arg(QString::fromUtf8(key));
    }
    void ramp(int frames) {
        if (!rampRemaining)
            return;
        double maxGain = 0;
        for (size_t i = 0; i < target.gains.size(); ++i)
            if (bands[i])
                maxGain = std::max(maxGain, target.gains[i]);
        const double desiredPreamp = target.enabled ? target.preampDb - (target.autoHeadroom ? maxGain : 0) : 0;
        const double step = std::min(1.0, double(frames) / rampRemaining);
        preamp += (desiredPreamp - preamp) * step;
        command(volume, "volume", std::pow(10.0, preamp / 20));
        for (size_t i = 0; i < gains.size(); ++i) {
            gains[i] += ((target.enabled ? target.gains[i] : 0) - gains[i]) * step;
            command(bands[i], "gain", gains[i]);
        }
        rampRemaining = std::max(0, rampRemaining - frames);
    }
    void collect(std::vector<float> &out) {
        AVFrame *frame = av_frame_alloc();
        if (!frame) {
            problem = "Unable to allocate DSP frame";
            return;
        }
        int status;
        while ((status = av_buffersink_get_frame(sink, frame)) >= 0) {
            // Filters may negotiate planar float; convert explicitly without assuming layout.
            const size_t previous = out.size();
            out.resize(previous + size_t(frame->nb_samples) * 2);
            if (frame->format == AV_SAMPLE_FMT_FLT) {
                std::memcpy(out.data() + previous, frame->data[0], size_t(frame->nb_samples) * 2 * sizeof(float));
            } else if (frame->format == AV_SAMPLE_FMT_FLTP) {
                auto *left = reinterpret_cast<const float *>(frame->extended_data[0]);
                auto *right = reinterpret_cast<const float *>(frame->extended_data[1]);
                for (int i = 0; i < frame->nb_samples; ++i) {
                    out[previous + size_t(i) * 2] = left[i];
                    out[previous + size_t(i) * 2 + 1] = right[i];
                }
            } else {
                problem = "Unexpected DSP sample format";
            }
            for (size_t i = previous; i < out.size(); ++i)
                if (!std::isfinite(out[i])) {
                    problem = "Non-finite DSP output";
                    break;
                }
            av_frame_unref(frame);
        }
        if (status != AVERROR(EAGAIN) && status != AVERROR_EOF)
            problem = "DSP output failed";
        av_frame_free(&frame);
    }
};
PcmDsp::PcmDsp(int rate) : d(std::make_unique<Impl>(rate)) {}
PcmDsp::~PcmDsp() = default;
void PcmDsp::setSettings(const EqualizerSettings &settings) {
    if (settings == d->target)
        return;
    d->target = settings;
    d->rampRemaining = std::max(1, d->rate / 50);
}
QString PcmDsp::error() const { return d->problem; }
std::vector<float> PcmDsp::process(std::span<const float> samples) {
    std::vector<float> output;
    if (!d->problem.isEmpty())
        return output;
    size_t offset = 0;
    while (offset < samples.size()) {
        const int frames = int(std::min<size_t>(d->rampRemaining ? 64 : 1024, (samples.size() - offset) / 2));
        d->ramp(frames);
        AVFrame *frame = av_frame_alloc();
        if (!frame) {
            d->problem = "Unable to allocate DSP input";
            break;
        }
        frame->format = AV_SAMPLE_FMT_FLT;
        frame->sample_rate = d->rate;
        frame->ch_layout = AV_CHANNEL_LAYOUT_STEREO;
        frame->nb_samples = frames;
        frame->pts = d->pts;
        int status = av_frame_get_buffer(frame, 0);
        if (status >= 0) {
            std::memcpy(frame->data[0], samples.data() + offset, size_t(frames) * 2 * sizeof(float));
            status = av_buffersrc_add_frame_flags(d->source, frame, AV_BUFFERSRC_FLAG_KEEP_REF);
        }
        av_frame_free(&frame);
        if (status < 0) {
            d->problem = "DSP input failed";
            break;
        }
        d->pts += frames;
        offset += size_t(frames) * 2;
        d->collect(output);
    }
    return output;
}
std::vector<float> PcmDsp::finish() {
    std::vector<float> output;
    if (d->problem.isEmpty()) {
        if (av_buffersrc_add_frame_flags(d->source, nullptr, 0) < 0)
            d->problem = "DSP drain failed";
        else
            d->collect(output);
    }
    return output;
}
} // namespace nekotune
