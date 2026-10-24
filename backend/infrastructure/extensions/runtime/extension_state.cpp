#include "infrastructure/extensions/runtime/extension_state.h"

namespace nekotune {
QString toString(ExtensionState state) {
    switch (state) {
    case ExtensionState::Disabled:
        return "disabled";
    case ExtensionState::Blocked:
        return "blocked";
    case ExtensionState::Starting:
        return "starting";
    case ExtensionState::Running:
        return "running";
    case ExtensionState::Stopping:
        return "stopping";
    case ExtensionState::Failed:
        return "failed";
    }
    Q_UNREACHABLE();
}
} // namespace nekotune
