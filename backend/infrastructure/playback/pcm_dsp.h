#pragma once
#include "domain/playback/audio_settings.h"
#include <memory>
#include <span>
#include <vector>
namespace nekotune {
// Per-source gain precedes mixing; the shared EQ and limiter remain after mixing.
class PcmGainEnvelope final {
  public:
    void setGain(double db, int sampleRate, bool immediate = false);
    float next();
  private:
    double current = 1, target = 1, step = 0;
    int remaining = 0;
};
// A persistent filter graph; control updates never reset its delay or IIR state.
class PcmDsp final {
  public:
    explicit PcmDsp(int sampleRate);
    ~PcmDsp();
    void setSettings(const EqualizerSettings &settings);
    std::vector<float> process(std::span<const float> samples);
    std::vector<float> finish();
    QString error() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> d;
};
} // namespace nekotune
