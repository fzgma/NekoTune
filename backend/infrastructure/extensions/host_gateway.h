#pragma once
#include "domain/result.h"
#include <QString>

namespace nekotune {
/// Host IPC methods an extension may reach through hostCall. Trust, lifecycle, appearance and
/// irreversible library operations stay with the user; refused calls never reach the router.
Result<void> checkHostCall(const QString &method);
} // namespace nekotune
