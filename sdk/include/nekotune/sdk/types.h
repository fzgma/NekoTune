#pragma once
#include <nekotune/result.h>
#include <QVariantMap>
#include <functional>
#include <memory>

namespace nekotune::sdk {
using Params = QVariantMap;
using Reply = std::function<void(Result<Params>)>;
using Done = std::function<void(Result<void>)>;
/// Services and commands must reply with an object; wrap scalars in {value}.
using Handler = std::function<void(const Params &params, Reply done)>;
using EventHandler = std::function<void(const Params &event)>;

/// Host-owned registration handle. dispose() or destruction removes the registration; both are
/// no-ops after the extension stopped, so plugins may keep handles until deactivate() returns.
class Registration {
  public:
    virtual ~Registration() = default;
    virtual void dispose() = 0;
};
using RegistrationPtr = std::unique_ptr<Registration>;
} // namespace nekotune::sdk
