#include <nekotune/sdk/extension_plugin.h>
#include <QObject>

using namespace nekotune;

/// Built against the frozen SDK 2.0 context, including virtual calls after registration methods.
class LegacyPlugin final : public QObject, public sdk::IExtensionPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID NEKOTUNE_EXTENSION_IID)
    Q_INTERFACES(nekotune::sdk::IExtensionPlugin)
  public:
    void activate(sdk::IExtensionContext &context, std::function<void(Result<void>)> done) override {
        m_service = context.registerService("ping", [&context](const sdk::Params &, sdk::Reply reply) {
            context.storageSet("legacy", "works", [&context, reply](Result<void> result) {
                if (!result) { reply(result.error()); return; }
                context.storageGet("legacy", [reply](Result<QVariant> value) {
                    if (!value) { reply(value.error()); return; }
                    reply(sdk::Params{{"value", value.value()}});
                });
            });
        });
        context.log(QtInfoMsg, "SDK 2.0 plugin activated");
        done({});
    }
    void deactivate() override { m_service.reset(); }
  private:
    sdk::RegistrationPtr m_service;
};
#include "plugin.moc"
