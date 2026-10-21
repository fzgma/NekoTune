#pragma once
#include <QString>

namespace nekotune {
struct ShuffleStrategyDescriptor {
    QString id, name;
    quint64 generation = 0;
    bool operator==(const ShuffleStrategyDescriptor &) const = default;
};
} // namespace nekotune
