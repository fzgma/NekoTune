#include <nekotune/sdk/extension_plugin.h>
#include <nekotune/sdk/shuffle_provider.h>
#include <QObject>
#include <stdexcept>

using namespace nekotune;

class TestShuffle final : public sdk::IShuffleProvider {
  public:
    QString mode;
    Result<sdk::ShuffleSelection> select(const sdk::ShuffleRequest &request) override {
        if (mode == "throw") throw std::runtime_error("shuffle exploded");
        if (mode == "error") return failure("shuffle refused");
        if (mode == "invalid") return sdk::ShuffleSelection{999999, {}};
        if (mode == "zero") return sdk::ShuffleSelection{0, {}};
        const int step = request.state.value("step", 0).toInt();
        const auto index = step % request.queue.size();
        return sdk::ShuffleSelection{request.queue.at(index).queueId, {{"step", step + 1}}};
    }
};

class ShuffleFixture final : public QObject, public sdk::IExtensionPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID NEKOTUNE_EXTENSION_IID)
    Q_INTERFACES(nekotune::sdk::IExtensionPlugin)
  public:
    void activate(sdk::ExtensionContext &context, sdk::Done done) override {
        auto *shuffle = &context.shuffle();
        m_provider.mode = context.config().get().value("mode").toString();
        m_registration = shuffle->add("test", &m_provider, {.name = "Test shuffle"});
        m_validation = context.services().add("validate", [this, shuffle](const sdk::Params &, sdk::Reply reply) {
            const bool duplicate = bool(shuffle->add("test", &m_provider, {.name = "Duplicate"}));
            const bool invalid = bool(shuffle->add("../bad", &m_provider, {.name = "Invalid"}));
            const bool unnamed = bool(shuffle->add("unnamed", &m_provider, {}));
            const bool nullProvider = bool(shuffle->add("null", nullptr, {.name = "Null"}));
            reply(sdk::Params{{"duplicate", duplicate}, {"invalid", invalid}, {"unnamed", unnamed}, {"null", nullProvider}});
        });
        m_release = context.services().add("release", [this](const sdk::Params &, sdk::Reply reply) {
            m_registration.reset();
            reply(sdk::Params{});
        });
        m_register = context.services().add("register", [this, shuffle](const sdk::Params &, sdk::Reply reply) {
            m_registration.reset();
            m_registration = shuffle->add("test", &m_provider, {.name = "Test shuffle"});
            reply(sdk::Params{{"registered", bool(m_registration)}});
        });
        done({});
    }
    void deactivate() override {
        m_registration.reset(); m_validation.reset(); m_release.reset(); m_register.reset();
    }
  private:
    TestShuffle m_provider;
    sdk::RegistrationPtr m_registration, m_validation, m_release, m_register;
};

#include "plugin.moc"
