#pragma once
#include <nekotune/sdk/extension_context.h>
#include <QtPlugin>

namespace nekotune::sdk {
/// Entry point of a native extension. Implementations derive from QObject and this interface:
///
///     class MyPlugin : public QObject, public nekotune::sdk::IExtensionPlugin {
///         Q_OBJECT
///         Q_PLUGIN_METADATA(IID NEKOTUNE_EXTENSION_IID)
///         Q_INTERFACES(nekotune::sdk::IExtensionPlugin)
///         ...
///     };
///
/// The library stays loaded until the application exits, and the same instance is reused when
/// the extension is enabled again, so deactivate() must return the plugin to its initial state.
class IExtensionPlugin {
  public:
    virtual ~IExtensionPlugin() = default;
    /// Called on the backend thread. Return quickly and complete done once activation finished;
    /// activation that does not complete within 30 seconds fails.
    virtual void activate(IExtensionContext &context, std::function<void(Result<void>)> done) = 0;
    /// Stop owned threads, cancel network work, release registrations and destroy providers before
    /// returning. Callbacks still pending in the host are dropped afterwards.
    virtual void deactivate() {}
};
} // namespace nekotune::sdk

#define NEKOTUNE_EXTENSION_IID "org.nekotune.IExtensionPlugin/2"
Q_DECLARE_INTERFACE(nekotune::sdk::IExtensionPlugin, NEKOTUNE_EXTENSION_IID)
