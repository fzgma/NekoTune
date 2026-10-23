#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <nekotune/sdk/pcm_provider.h>
#include <span>

namespace nekotune {
// One output-thread producer, one backend-thread consumer. Fixed storage and no waits or
// allocations on the producer. History holds rendered output until the device consumes it.
class PcmCaptureBuffer final {
  public:
    static constexpr size_t blockFrames = 512, historyFrames = 262144, queueSlots = 64;
    void setEnabled(bool value) { enabled.store(value, std::memory_order_release); }
    bool isEnabled() const { return enabled.load(std::memory_order_acquire); }
    void reset(quint64 sourceGeneration) {
        source = sourceGeneration;
        stream = nextGeneration.fetch_add(1, std::memory_order_relaxed) + 1;
        generation.store(stream, std::memory_order_release);
        begin = end = delivered = 0;
    }
    void append(std::span<const float> samples, qint64 firstFrame) {
        if (!isEnabled()) {
            begin = end = delivered = firstFrame + qint64(samples.size() / 2);
            return;
        }
        if (end != firstFrame)
            begin = delivered = firstFrame;
        const auto frames = qint64(samples.size() / 2);
        for (qint64 i = 0; i < frames; ++i) {
            const auto offset = size_t(firstFrame + i) % historyFrames * 2;
            history[offset] = samples[size_t(i) * 2];
            history[offset + 1] = samples[size_t(i) * 2 + 1];
        }
        end = firstFrame + frames;
        begin = std::max(begin, end - qint64(historyFrames));
    }
    void advance(qint64 consumed, int rate, double volume, qint64 mediaPositionMs) {
        if (!isEnabled() || rate <= 0) {
            delivered = consumed;
            return;
        }
        auto first = std::max(delivered, begin);
        const auto last = std::min(consumed, end);
        while (first < last) {
            const auto write = head.load(std::memory_order_relaxed);
            if (write - tail.load(std::memory_order_acquire) >= queueSlots) {
                dropped.fetch_add(quint64(last - first), std::memory_order_relaxed);
                break;
            }
            auto &block = ready[write % queueSlots];
            block.count = int(std::min<qint64>(blockFrames, last - first));
            block.rate = rate;
            block.stream = stream;
            block.source = source;
            block.frame = first;
            block.position = mediaPositionMs - (consumed - first) * 1000 / rate;
            for (int i = 0; i < block.count; ++i) {
                const auto offset = size_t(first + i) % historyFrames * 2;
                block.samples[size_t(i) * 2] = float(history[offset] * volume);
                block.samples[size_t(i) * 2 + 1] = float(history[offset + 1] * volume);
            }
            head.store(write + 1, std::memory_order_release);
            first += block.count;
        }
        delivered = std::max(delivered, last);
    }
    QVector<sdk::PcmBlock> take(quint64 sourceGeneration) {
        QVector<sdk::PcmBlock> result;
        auto read = tail.load(std::memory_order_relaxed);
        const auto stop = head.load(std::memory_order_acquire);
        while (read != stop) {
            const auto &block = ready[read % queueSlots];
            if (isEnabled() && block.source == sourceGeneration &&
                block.stream == generation.load(std::memory_order_acquire)) {
                sdk::PcmBlock value;
                value.samples = QVector<float>(block.samples.data(), block.samples.data() + block.count * 2);
                value.sampleRate = block.rate;
                value.generation = block.stream;
                value.framePosition = block.frame;
                value.positionMs = block.position;
                result.append(std::move(value));
            }
            ++read;
        }
        tail.store(read, std::memory_order_release);
        return result;
    }
    quint64 droppedFrames() const { return dropped.load(std::memory_order_relaxed); }

  private:
    struct Block {
        std::array<float, blockFrames * 2> samples{};
        int count = 0, rate = 0;
        quint64 stream = 0, source = 0;
        qint64 frame = 0, position = 0;
    };
    std::array<float, historyFrames * 2> history{};
    std::array<Block, queueSlots> ready{};
    inline static std::atomic<quint64> nextGeneration{0};
    std::atomic<bool> enabled{false};
    std::atomic<quint64> head{0}, tail{0}, generation{0}, dropped{0};
    quint64 source = 0, stream = 0;
    qint64 begin = 0, end = 0, delivered = 0;
};
} // namespace nekotune
