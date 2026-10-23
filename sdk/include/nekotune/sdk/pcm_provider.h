#pragma once
#include <QVector>
#include <nekotune/sdk/extension_context.h>

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
using PcmHandler = std::function<void(const PcmBlock &)>;

/// Optional SDK 2.2 interface; does not change the SDK 2.0 context ABI or plugin IID.
/// These methods and callbacks run on the backend thread. Callbacks must return promptly;
/// perform analysis on a separate worker. Only consumed output is delivered. Blocks can be
/// dropped under load; detect discontinuities using generation and framePosition.
class IPcmExtensionContext {
  public:
    virtual ~IPcmExtensionContext() = default;
    virtual PcmCapabilities pcmCapabilities() const = 0;
    /// Null when unsupported. Destroying/disposing the handle immediately prevents further
    /// callbacks; stopping the extension removes every outstanding subscription.
    virtual RegistrationPtr subscribePcm(PcmHandler handler) = 0;
};
inline IPcmExtensionContext *pcmContext(IExtensionContext &context) {
    return dynamic_cast<IPcmExtensionContext *>(&context);
}
} // namespace nekotune::sdk
