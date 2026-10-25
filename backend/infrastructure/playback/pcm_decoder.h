#pragma once
#include "domain/playback/playback_backend.h"
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <span>
#include <thread>
#include <vector>
namespace nekotune {
struct DecodedAudioInfo {
    int sampleRate = 0;
    qint64 durationMs = 0;
    qint64 decodedFrames = 0;
    bool seekable = false;
    bool ready = false;
    bool eof = false;
    bool fallbackEligible = false;
    QString error;
    AudioMetadata metadata;
    ReplayGainData replayGain;
};
// One decoder owns all FFmpeg input state. Native-rate stereo float samples are
// buffered so the render side can preserve one resampler across gapless sources.
class PcmDecoder final {
  public:
    // Zero selects the local-file (3 MiB) or network (8 MiB) buffer budget.
    PcmDecoder(QUrl source, qint64 seekMs = 0, size_t capacityBytes = 0);
    ~PcmDecoder();
    PcmDecoder(const PcmDecoder &) = delete;
    PcmDecoder &operator=(const PcmDecoder &) = delete;
    DecodedAudioInfo info() const;
    size_t available() const; // stereo frames
    size_t read(float *destination, size_t frames);
    void cancel();
    void updateReplayGainData(const ReplayGainData &analysis);
    const QUrl source;

  private:
    void decode(qint64 seekMs);
    bool append(std::span<const float> samples);
    static int interrupted(void *self);
    std::atomic_bool m_cancelled{false};
    std::atomic<qint64> m_deadline{0};
    mutable std::mutex m_mutex;
    std::condition_variable m_space;
    DecodedAudioInfo m_info;
    std::optional<ReplayGainData> m_analysis;
    // Allocated before starting the decoder; neither thread reallocates PCM blocks.
    std::vector<float> m_buffer;
    size_t m_offset = 0, m_samples = 0;
    std::thread m_thread;
};
} // namespace nekotune
