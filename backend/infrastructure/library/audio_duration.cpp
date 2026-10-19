#include "infrastructure/library/audio_duration.h"
#include <QElapsedTimer>
#include <QRegularExpression>
extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/mathematics.h>
}

namespace nekotune {
namespace {
// FFmpeg can block inside reads while probing damaged/slow input.
// Its interrupt callback observes cancellation without an event loop.
// The elapsed deadline bounds that work even when no user cancels.
// The context must stay alive until format probing/cleanup finishes.
struct ProbeContext {
  const std::atomic_bool &cancelled;
  QElapsedTimer elapsed;
  static int interrupt(void *opaque) {
    const auto &probe = *static_cast<ProbeContext *>(opaque);
    return probe.cancelled.load() || probe.elapsed.hasExpired(5000);
  }
};
} // namespace

ReplayGainData readReplayGainTags(AVDictionary *container, AVDictionary *stream) {
  auto number = [&](const char *key, bool gain) -> std::optional<double> {
    // Stream-specific values take precedence; malformed values can fall back to the container.
    for (auto *dictionary : {stream, container}) {
      const auto *entry = av_dict_get(dictionary, key, nullptr, 0);
      if (!entry) continue;
      QString value = QString::fromUtf8(entry->value).trimmed();
      if (gain && value.endsWith("dB", Qt::CaseInsensitive)) value.chop(2);
      bool ok = false;
      double result = value.trimmed().toDouble(&ok);
      if (ok && std::isfinite(result) && (gain ? std::abs(result) <= 60 : result > 0 && result <= 64))
        return result;
    }
    return {};
  };
  ReplayGainData result;
  result.trackGainDb = number("REPLAYGAIN_TRACK_GAIN", true);
  result.albumGainDb = number("REPLAYGAIN_ALBUM_GAIN", true);
  result.trackPeak = number("REPLAYGAIN_TRACK_PEAK", false);
  result.albumPeak = number("REPLAYGAIN_ALBUM_PEAK", false);
  if (result.trackGainDb) result.trackSource = "tag";
  if (result.albumGainDb) result.albumSource = "tag";
  return result;
}

AudioFileInfo readAudioFileInfo(const QString &path,
                         const std::atomic_bool &cancelled) {
  if (cancelled.load())
    return {};
  ProbeContext probe{cancelled, {}};
  probe.elapsed.start();
  auto *format = avformat_alloc_context();
  if (!format)
    return {};
  format->interrupt_callback = {&ProbeContext::interrupt, &probe};
  AVDictionary *options = nullptr;
  // Imported files are local; probing must never follow a network playlist.
  av_dict_set(&options, "protocol_whitelist", "file", 0);
  av_dict_set(&options, "format_whitelist", "mp3,mov,aac,wav,flac,ogg", 0);
  av_dict_set(&options, "probesize", "1048576", 0);
  av_dict_set(&options, "analyzeduration", "2000000", 0);
  // Keep the encoded filename alive throughout the synchronous FFmpeg open call.
  // The probe's cancellation/deadline context likewise outlives all format reads.
  // The whitelist restricts protocols even if a local container references another resource.
  const auto filename = path.toUtf8();
  const auto opened =
      avformat_open_input(&format, filename.constData(), nullptr, &options);
  av_dict_free(&options);
  if (opened < 0)
    return {}; // avformat_open_input frees the context on failure.
  AudioFileInfo result;
  qint64 duration = 0;
  // Finding a positive duration is useful only if the probe completed within its allowance.
  // An interrupted probe returns unknown timing rather than a partially obtained value.
  // Import can still accept the hashed audio; later scans may retry duration recovery.
  if (avformat_find_stream_info(format, nullptr) >= 0 &&
      !ProbeContext::interrupt(&probe)) {
    for (unsigned int index = 0; index < format->nb_streams; ++index) {
      const auto *stream = format->streams[index];
      if (stream->codecpar->codec_type != AVMEDIA_TYPE_AUDIO)
        continue;
      auto tag = [&](std::initializer_list<const char *> keys) {
        for (auto *dictionary : {format->metadata, stream->metadata})
          for (const char *key : keys)
            if (const auto *entry = av_dict_get(dictionary, key, nullptr, 0)) {
              const auto value = QString::fromUtf8(entry->value).trimmed();
              if (!value.isEmpty()) return value.left(65536);
            }
        return QString();
      };
      auto number = [](const QString &value) {
        static const QRegularExpression leading(QStringLiteral("^(\\d{1,4})(?:\\D|$)"));
        const auto match = leading.match(value);
        return match.hasMatch() ? match.captured(1).toInt() : 0;
      };
      result.metadata = {tag({"title"}), tag({"artist", "author"}), tag({"album"}),
                         tag({"album_artist", "albumartist"}), tag({"genre"}), tag({"lyrics", "unsyncedlyrics"}),
                         number(tag({"date", "year"})), number(tag({"track", "tracknumber"})),
                         number(tag({"disc", "discnumber"}))};
      result.replayGain = readReplayGainTags(format->metadata, stream->metadata);
      // Prefer the audio stream over container duration, which may include longer non-audio tracks.
      if (stream->duration != AV_NOPTS_VALUE && stream->duration > 0)
        duration = av_rescale_q(stream->duration, stream->time_base,
                                AVRational{1, 1000});
      else if (format->duration != AV_NOPTS_VALUE && format->duration > 0)
        duration =
            av_rescale_q(format->duration, AV_TIME_BASE_Q, AVRational{1, 1000});
      break;
    }
  }
  avformat_close_input(&format);
  result.durationMs = qMax(qint64(0), duration);
  return result;
}
qint64 readAudioDuration(const QString &path, const std::atomic_bool &cancelled) {
  return readAudioFileInfo(path, cancelled).durationMs;
}
} // namespace nekotune
