#pragma once
#include <nekotune/sdk/pcm_provider.h>
namespace nekotune {
class IPcmSource {
  public:
    virtual ~IPcmSource() = default;
    virtual sdk::PcmCapabilities capabilities() const = 0;
    virtual sdk::RegistrationPtr subscribe(sdk::PcmHandler handler) = 0;
};
} // namespace nekotune
