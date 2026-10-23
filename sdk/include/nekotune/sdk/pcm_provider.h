#pragma once
#include <QVector>
#include <nekotune/sdk/types.h>

namespace nekotune::sdk {
struct PcmCapabilities {
    bool available = false;
    QString backend;
    QString reason;
};
struct PcmBlock {
    QVector<float> samples; // Interleaved, post-DSP stereo; includes player volume.
    int sampleRate = 0;
    int channels = 2;
    quint64 generation = 0;   // Changes on stream discontinuities, including device resets.
    qint64 framePosition = 0; // Absolute output frame index within this generation.
    qint64 positionMs = 0;    // Media position of the first sample.
};
/// Callbacks run on the backend thread and must return promptly; perform analysis on a
/// separate worker. Only consumed output is delivered. Blocks can be dropped under load;
/// detect discontinuities using generation and framePosition.
using PcmHandler = std::function<void(const PcmBlock &)>;

} // namespace nekotune::sdk
