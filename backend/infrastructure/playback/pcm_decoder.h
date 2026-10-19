#pragma once
#include "domain/playback/playback_backend.h"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
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
    PcmDecoder(QUrl source, qint64 seekMs = 0, size_t capacityBytes = 24 * 1024 * 1024);
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
    bool append(std::vector<float> samples);
    static int interrupted(void *self);
    std::atomic_bool m_cancelled{false};
    std::atomic<qint64> m_deadline{0};
    mutable std::mutex m_mutex;
    std::condition_variable m_space;
    DecodedAudioInfo m_info;
    std::optional<ReplayGainData> m_analysis;
    std::deque<std::vector<float>> m_blocks;
    size_t m_offset = 0, m_samples = 0;
    const size_t m_capacity;
    std::thread m_thread;
};
} // namespace nekotune
