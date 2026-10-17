#pragma once
#include "infrastructure/playback/pcm_renderer.h"
#include <algorithm>
#include <deque>
namespace nekotune {
// Submitted PCM is deliberately not the playback clock. A device (or a capture
// sink in tests) advances this clock only as it consumes submitted frames.
class PcmOutputClock final {
  public:
    void reset() {
        m_pending.clear();
        m_consumed = 0;
    }
    void append(std::vector<PcmBoundary> boundaries) {
        for (auto &boundary : boundaries)
            m_pending.push_back(std::move(boundary));
    }
    bool transitionPending() const {
        return std::any_of(m_pending.begin(), m_pending.end(),
                           [](const auto &boundary) { return boundary.token != 0; });
    }
    std::vector<PcmBoundary> advance(qint64 consumed) {
        m_consumed = std::max(m_consumed, consumed);
        std::vector<PcmBoundary> result;
        while (!m_pending.empty() && m_pending.front().frame <= m_consumed) {
            result.push_back(std::move(m_pending.front()));
            m_pending.pop_front();
        }
        return result;
    }

  private:
    qint64 m_consumed = 0;
    std::deque<PcmBoundary> m_pending;
};
} // namespace nekotune
