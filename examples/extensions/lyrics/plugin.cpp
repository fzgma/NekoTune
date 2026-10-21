#include <nekotune/sdk/extension_plugin.h>
#include <QObject>

using namespace nekotune;

namespace {
class DemoLyrics final : public sdk::ILyricsProvider {
  public:
    void search(const sdk::Params &query, sdk::Reply done) override {
        const sdk::Params candidate{
            {"id", "demo"},
            {"title", query.value("title")},
            {"artist", query.value("artist")},
            {"score", 1},
            {"synced_lyrics", "[00:00.00]NekoTune extension lyrics\n[00:02.00]歌词来源可独立安装与重载\n"
                              "[00:05.00]Enjoy your music"}};
        done(sdk::Params{{"candidates", QVariantList{candidate}}});
    }
    void resolve(const sdk::Params &candidate, sdk::Reply done) override { done(candidate); }
};
} // namespace

class LyricsExample final : public QObject, public sdk::IExtensionPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID NEKOTUNE_EXTENSION_IID)
    Q_INTERFACES(nekotune::sdk::IExtensionPlugin)
  public:
    void activate(sdk::IExtensionContext &context, std::function<void(Result<void>)> done) override {
        m_registration = context.registerLyricsProvider("demo", &m_provider, {{"name", "Example / 示例歌词"}});
        done(m_registration ? Result<void>() : Result<void>(failure("Cannot register lyrics source")));
    }
    void deactivate() override { m_registration.reset(); }

  private:
    DemoLyrics m_provider;
    sdk::RegistrationPtr m_registration;
};

#include "plugin.moc"
