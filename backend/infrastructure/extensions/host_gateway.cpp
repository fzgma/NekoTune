#include "infrastructure/extensions/host_gateway.h"
#include <QSet>

namespace nekotune {
Result<void> checkHostCall(const QString &method) {
    static const QSet<QString> extensionMethods{"extensions.call", "extensions.list"};
    static const QSet<QString> refused{"library.delete",          "library.migrate", "library.restore",
                                       "library.restore_preview", "library.remove_rollback",
                                       "history.clear",           "ai.config.get",   "ai.config.set",
                                       "ai.config.clear_key"};
    if ((method.startsWith("extensions.") && !extensionMethods.contains(method)) ||
        method.startsWith("appearance.") || refused.contains(method))
        return failure("Host method is not available to extensions: " + method, ErrorCode::PermissionDenied);
    return {};
}
} // namespace nekotune
