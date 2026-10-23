#include "domain/appearance/appearance_backend.h"
#include "ipc/api/api_context.h"

namespace nekotune {
void registerAppearanceApi(IpcRouter &router, ApiContext &api) {
    for (const QString method : {"appearance.get", "appearance.select", "appearance.export_theme"}) {
        router.registerMethod(method, [&api, method](const QJsonObject &params, auto done) {
            if (!api.appearance) {
                done(error(failure("Appearance unavailable")));
                return;
            }
            api.appearance->request(method, params.toVariantMap(), [done](Result<QVariantMap> result) {
                done(result ? success(QJsonObject::fromVariantMap(result.value())) : error(result.error()));
            });
        });
    }
}
} // namespace nekotune
