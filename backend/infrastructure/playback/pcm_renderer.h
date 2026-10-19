#pragma once
#include "infrastructure/playback/pcm_decoder.h"
#include <memory>
#include <vector>
namespace nekotune {
struct PcmBoundary {
    qint64 frame = 0;
    quint64 token = 0;
    QUrl source;
    DecodedAudioInfo info;
    QUrl releasedSource;
    bool ended = false;
};
// Deterministic render core. Its clock advances only through returned PCM frames;
// the output adapter publishes boundaries after its device clock consumes them.
class PcmRenderer final {
  public:
    PcmRenderer(int sampleRate, std::shared_ptr<PcmDecoder> current);
    ~PcmRenderer();
    void preload(std::shared_ptr<PcmDecoder> decoder, const PreloadRequest &request);
    void authorize(quint64 token);
    bool canPreload() const;
    bool hasScheduledTransition() const;
    void cancelNext();
    void finishAfterCurrent();
    bool waitingForPlan() const;
    void releasePlanWait();
    qint64 bufferedFrames() const;
    std::vector<float> render(size_t frames);
    std::vector<PcmBoundary> takeBoundaries();
    qint64 framesRendered() const;
    bool finished() const;
    void setReplayGain(const ReplayGainSettings &settings);
    void updateReplayGainData(const QUrl &source, const ReplayGainData &data);

  private:
    struct Impl;
    std::unique_ptr<Impl> d;
};
} // namespace nekotune
