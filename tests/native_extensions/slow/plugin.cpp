#include <nekotune/sdk/extension_plugin.h>
#include <QObject>
#include <QTimer>

using namespace nekotune;

/// Completes activation after 250 ms so startup ordering with pending activations is exercised.
class SlowExtension final : public QObject, public sdk::IExtensionPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID NEKOTUNE_EXTENSION_IID)
    Q_INTERFACES(nekotune::sdk::IExtensionPlugin)
  public:
    void activate(sdk::ExtensionContext &, sdk::Done done) override {
        QTimer::singleShot(250, this, [done] { done({}); });
    }
};

#include "plugin.moc"
