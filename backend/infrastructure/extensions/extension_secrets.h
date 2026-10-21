#pragma once
#include "domain/extensions/extension_backend.h"

namespace nekotune {
/// Keyring access for extension secrets. params carries extensionId, key, operation
/// (get/set/delete) and value; get replies with {value}, null when absent.
/// Kugou's legacy account keys keep their pre-plugin storage identity.
void extensionSecret(const QVariantMap &params, IExtensionBackend::Completion done);
} // namespace nekotune
