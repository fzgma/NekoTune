#include "infrastructure/library/loudness_analysis.h"
#include "app_paths.h"
#include "infrastructure/library/audio_duration.h"
#include "infrastructure/playback/pcm_decoder.h"
#include "infrastructure/playback/pcm_renderer.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <cstring>
extern "C" {
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
}

namespace nekotune {
namespace {
QString fingerprint(const QString &path) {
    const auto canonical = QFileInfo(path).canonicalFilePath();
    const QFileInfo file(canonical);
    if (canonical.isEmpty() || !file.isFile())
        return {};
    // Windows can report a symlink's own size/timestamps. Fingerprint the same
    // target for both its managed audio link and its original playback path.
    const auto identity = canonical.toUtf8() + '\0' + QByteArray::number(file.size()) + '\0' +
                          QByteArray::number(file.lastModified().toMSecsSinceEpoch()) + '\0' +
                          QByteArray::number(file.metadataChangeTime().toMSecsSinceEpoch());
    return QString::fromLatin1(QCryptographicHash::hash(identity, QCryptographicHash::Sha256).toHex());
}
QString cachePath(const QString &key) { return AppPaths::configFile("loudness/" + key + ".json"); }
QJsonObject readRecord(const QString &key) {
    if (key.isEmpty())
        return {};
    QFile file(cachePath(key));
    if (!file.open(QIODevice::ReadOnly) || file.size() > 4 * 1024 * 1024)
        return {};
    const auto record = QJsonDocument::fromJson(file.readAll()).object();
    return record.value("version").toInt() == 1 ? record : QJsonObject{};
}
bool writeRecord(const QString &key, const QJsonObject &record) {
    if (!QDir().mkpath(QFileInfo(cachePath(key)).absolutePath()))
        return false;
    QSaveFile file(cachePath(key));
    const auto bytes = QJsonDocument(record).toJson(QJsonDocument::Compact);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
}
std::optional<double> number(const QJsonObject &record, const char *name, bool peak = false) {
    auto value = record.value(QLatin1String(name));
    if (!value.isDouble() || !std::isfinite(value.toDouble()))
        return {};
    double n = value.toDouble();
    if (peak ? n > 0 && n <= 64 : std::abs(n) <= 60)
        return n;
    return {};
}
class Meter {
  public:
    AVFilterGraph *graph = avfilter_graph_alloc();
    AVFilterContext *input = nullptr, *output = nullptr;
    qint64 frames = 0;
    double loudness = -70, peak = 0;
    bool valid = false;
    Meter() {
        if (!graph)
            return;
        graph->nb_threads = 1;
        for (const auto *name : {"abuffer", "ebur128", "abuffersink"})
            if (!avfilter_get_by_name(name))
                return;
        AVFilterContext *meter = nullptr;
        valid = avfilter_graph_create_filter(&input, avfilter_get_by_name("abuffer"), "input",
                                             "sample_rate=48000:sample_fmt=flt:channel_layout=stereo:time_base=1/48000",
                                             nullptr, graph) >= 0 &&
                avfilter_graph_create_filter(&meter, avfilter_get_by_name("ebur128"), "meter",
                                             "metadata=1:peak=true:framelog=verbose", nullptr, graph) >= 0 &&
                avfilter_graph_create_filter(&output, avfilter_get_by_name("abuffersink"), "output", nullptr, nullptr,
                                             graph) >= 0 &&
                avfilter_link(input, 0, meter, 0) >= 0 && avfilter_link(meter, 0, output, 0) >= 0 &&
                avfilter_graph_config(graph, nullptr) >= 0;
    }
    ~Meter() { avfilter_graph_free(&graph); }
    bool collect() {
        AVFrame *frame = av_frame_alloc();
        if (!frame)
            return false;
        int result;
        while ((result = av_buffersink_get_frame(output, frame)) >= 0) {
            if (const auto *entry = av_dict_get(frame->metadata, "lavfi.r128.I", nullptr, 0))
                loudness = QString::fromUtf8(entry->value).toDouble();
            if (const auto *entry = av_dict_get(frame->metadata, "lavfi.r128.true_peak", nullptr, 0))
                peak = std::max(peak, QString::fromUtf8(entry->value).toDouble());
            av_frame_unref(frame);
        }
        av_frame_free(&frame);
        return result == AVERROR(EAGAIN) || result == AVERROR_EOF;
    }
    bool push(const std::vector<float> &pcm) {
        if (!valid)
            return false;
        AVFrame *frame = av_frame_alloc();
        if (!frame)
            return false;
        frame->format = AV_SAMPLE_FMT_FLT;
        frame->sample_rate = 48000;
        frame->ch_layout = AV_CHANNEL_LAYOUT_STEREO;
        frame->nb_samples = int(pcm.size() / 2);
        frame->pts = frames;
        int result = av_frame_get_buffer(frame, 0);
        if (result >= 0) {
            std::memcpy(frame->data[0], pcm.data(), pcm.size() * sizeof(float));
            for (float sample : pcm)
                peak = std::max(peak, double(std::abs(sample)));
            result = av_buffersrc_add_frame_flags(input, frame, AV_BUFFERSRC_FLAG_KEEP_REF);
        }
        frames += frame->nb_samples;
        av_frame_free(&frame);
        return result >= 0 && collect();
    }
    bool finish() { return valid && av_buffersrc_add_frame_flags(input, nullptr, 0) >= 0 && collect(); }
    std::optional<double> gain() const {
        // Below the absolute gate (including digital silence) has no usable integrated loudness.
        if (frames < 19200 || peak <= 0 || !std::isfinite(loudness) || loudness <= -69.9)
            return {};
        return std::clamp(-18.0 - loudness, -60.0, 60.0);
    }
};
} // namespace
ReplayGainData readLoudnessCache(const QString &path) {
    const auto record = readRecord(fingerprint(path));
    ReplayGainData data;
    data.trackGainDb = number(record, "track_gain");
    data.trackPeak = number(record, "track_peak", true);
    if (data.trackGainDb)
        data.trackSource = "analysis";
    const auto members = record.value("members").toObject();
    bool albumValid = !members.isEmpty();
    for (auto it = members.begin(); it != members.end(); ++it)
        if (fingerprint(it.key()) != it.value().toString()) {
            albumValid = false;
            break;
        }
    if (albumValid) {
        data.albumGainDb = number(record, "album_gain");
        data.albumPeak = number(record, "album_peak", true);
        if (data.albumGainDb)
            data.albumSource = "analysis";
    }
    return data;
}
LoudnessAnalysis::~LoudnessAnalysis() {
    m_cancelled = true;
    if (m_worker)
        m_worker->wait();
}
void LoudnessAnalysis::cancel() {
    if (!m_status.running)
        return;
    m_cancelled = true;
    m_status.cancelling = true;
    emit changed();
}
Result<void> LoudnessAnalysis::start(const QVector<LoudnessAnalysisGroup> &groups) {
    if (m_status.running)
        return failure("Loudness analysis is already running", ErrorCode::Unavailable);
    if (groups.isEmpty())
        return failure("No local audio available for analysis");
    if (m_worker)
        m_worker->wait();
    m_cancelled = false;
    m_status = {};
    m_status.running = true;
    for (const auto &group : groups)
        m_status.total += group.paths.size();
    emit changed();
    m_worker.reset(QThread::create([this, groups] {
        int completed = 0;
        QString error;
        for (const auto &group : groups) {
            if (m_cancelled)
                break;
            QJsonObject members;
            for (const auto &path : group.paths)
                members.insert(path, fingerprint(path));
            bool needed = false;
            for (const auto &path : group.paths) {
                const auto data =
                    withReplayGainFallback(readAudioFileInfo(path, m_cancelled).replayGain, readLoudnessCache(path));
                if (!data.trackGainDb || (group.album && !data.albumGainDb))
                    needed = true;
                if (group.album && data.albumSource == "analysis" &&
                    readRecord(members.value(path).toString()).value("members").toObject() != members)
                    needed = true;
                if (m_cancelled)
                    break;
            }
            if (m_cancelled)
                break;
            if (!needed) {
                completed += group.paths.size();
                QMetaObject::invokeMethod(
                    this,
                    [this, completed] {
                        m_status.completed = completed;
                        emit changed();
                    },
                    Qt::QueuedConnection);
                continue;
            }
            Meter album;
            QVector<QJsonObject> records;
            bool failed = false;
            for (const auto &path : group.paths) {
                if (m_cancelled)
                    break;
                auto decoder = std::make_shared<PcmDecoder>(QUrl::fromLocalFile(path), 0, 256 * 1024);
                PcmRenderer renderer(48000, decoder);
                renderer.finishAfterCurrent();
                Meter track;
                if (!track.valid || !album.valid) {
                    error = "Unable to create EBU R128 meter";
                    failed = true;
                    break;
                }
                while (!m_cancelled && !renderer.finished()) {
                    const auto info = decoder->info();
                    if (!info.error.isEmpty()) {
                        error = path + ": " + info.error;
                        failed = true;
                        break;
                    }
                    const auto pcm = renderer.render(4096);
                    if (pcm.empty()) {
                        QThread::msleep(5);
                        continue;
                    }
                    if (!track.push(pcm) || !album.push(pcm)) {
                        error = "Loudness measurement failed";
                        failed = true;
                        break;
                    }
                    // Backpressure also throttles the bounded decoder while playback is active.
                    if (m_playing)
                        for (int i = 0; i < 4 && !m_cancelled && m_playing; ++i)
                            QThread::msleep(10);
                }
                decoder->cancel();
                if (failed || m_cancelled)
                    break;
                if (!track.finish()) {
                    error = "Loudness measurement drain failed";
                    failed = true;
                    break;
                }
                QJsonObject record{{"version", 1}, {"track_peak", track.peak}};
                if (const auto gain = track.gain())
                    record.insert("track_gain", *gain);
                records.append(record);
                QMetaObject::invokeMethod(
                    this,
                    [this, value = ++completed] {
                        m_status.completed = value;
                        emit changed();
                    },
                    Qt::QueuedConnection);
            }
            if (m_cancelled)
                break;
            if (failed || !album.finish()) {
                if (error.isEmpty())
                    error = "Album measurement failed";
                continue;
            }
            for (auto it = members.begin(); it != members.end(); ++it)
                if (it.value().toString().isEmpty() || fingerprint(it.key()) != it.value().toString())
                    failed = true;
            if (failed) {
                error = "Audio changed during analysis; results discarded";
                continue;
            }
            for (qsizetype i = 0; i < records.size() && !m_cancelled; ++i) {
                auto record = records[i];
                if (group.album && album.gain()) {
                    record.insert("album_gain", *album.gain());
                    record.insert("album_peak", album.peak);
                    record.insert("members", members);
                }
                const auto &path = group.paths[i];
                if (!writeRecord(members.value(path).toString(), record)) {
                    error = "Unable to save loudness cache";
                    continue;
                }
                auto data = readLoudnessCache(path);
                QMetaObject::invokeMethod(
                    this, [this, path, data] { emit resultReady(path, data); }, Qt::QueuedConnection);
            }
        }
        QMetaObject::invokeMethod(
            this,
            [this, error] {
                m_status.running = false;
                m_status.cancelling = false;
                m_status.cancelled = m_cancelled.load();
                m_status.error = error;
                emit changed();
            },
            Qt::QueuedConnection);
    }));
    m_worker->start(QThread::LowPriority);
    return {};
}
} // namespace nekotune
