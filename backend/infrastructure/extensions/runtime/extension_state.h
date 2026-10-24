#pragma once
#include <QString>

namespace nekotune {
/// Runtime state of an installed extension; the strings are part of the extensions.list protocol.
enum class ExtensionState { Disabled, Blocked, Starting, Running, Stopping, Failed };
QString toString(ExtensionState state);
} // namespace nekotune
