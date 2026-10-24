#include <nekotune/sdk/extension_plugin.h>
#include <QObject>

using namespace nekotune;

class CustomUiExample final : public QObject, public sdk::IExtensionPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID NEKOTUNE_EXTENSION_IID)
    Q_INTERFACES(nekotune::sdk::IExtensionPlugin)
  public:
    void activate(sdk::ExtensionContext &context, sdk::Done done) override {
        m_hello = context.services().add("hello", [](const sdk::Params &, sdk::Reply reply) {
            reply(sdk::Params{{"message", "Hello from the native extension!"}});
        });
        m_inspect = context.commands().add(
            "inspect-song",
            [&context](const sdk::Params &params, sdk::Reply reply) {
                context.log().write(QtInfoMsg, "Selection received");
                context.events().publish("selection", params);
                reply(params);
            },
            {.title = "Inspect selection"});
        done({});
    }
    void deactivate() override {
        m_hello.reset();
        m_inspect.reset();
    }

  private:
    sdk::RegistrationPtr m_hello, m_inspect;
};

#include "plugin.moc"
