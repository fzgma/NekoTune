#include <nekotune/sdk/extension_plugin.h>
#include <QObject>

class HelloExtension final : public QObject, public nekotune::sdk::IExtensionPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID NEKOTUNE_EXTENSION_IID)
    Q_INTERFACES(nekotune::sdk::IExtensionPlugin)

  public:
    void activate(nekotune::sdk::IExtensionContext &context,
                  std::function<void(nekotune::Result<void>)> done) override {
        m_hello = context.registerService("hello", [](const nekotune::sdk::Params &params,
                                                     nekotune::sdk::Reply reply) {
            reply(nekotune::sdk::Params{{"message", "Hello, " + params.value("name").toString()}});
        });
        if (!m_hello) {
            done(nekotune::failure("Cannot register the hello service"));
            return;
        }
        done({});
    }

    void deactivate() override {
        m_hello.reset();
    }

  private:
    nekotune::sdk::RegistrationPtr m_hello;
};

#include "plugin.moc"
