#include <nekotune/sdk/extension_plugin.h>
#include <QObject>

using namespace nekotune;

namespace {
class DemoLyrics final : public sdk::ILyricsProvider {
  public:
    void search(const sdk::LyricsQuery &query,
                std::function<void(Result<QVector<sdk::LyricsCandidate>>)> done) override {
        sdk::LyricsCandidate candidate;
        candidate.title = query.title;
        candidate.artist = query.artist;
        candidate.document.syncedLyrics = "[00:00.00]NekoTune extension lyrics\n[00:02.00]歌词来源可独立安装与重载\n"
                                          "[00:05.00]Enjoy your music";
        candidate.extra.insert("id", "demo");
        done(QVector<sdk::LyricsCandidate>{candidate});
    }
    void resolve(const sdk::LyricsCandidate &candidate,
                 std::function<void(Result<sdk::LyricsResolution>)> done) override {
        done(sdk::LyricsResolution{candidate.document, {}});
    }
};
} // namespace

class LyricsExample final : public QObject, public sdk::IExtensionPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID NEKOTUNE_EXTENSION_IID)
    Q_INTERFACES(nekotune::sdk::IExtensionPlugin)
  public:
    void activate(sdk::ExtensionContext &context, sdk::Done done) override {
        m_registration = context.lyrics().add("demo", &m_provider, {.name = "Example / 示例歌词"});
        done(m_registration ? Result<void>() : Result<void>(failure("Cannot register lyrics source")));
    }
    void deactivate() override { m_registration.reset(); }

  private:
    DemoLyrics m_provider;
    sdk::RegistrationPtr m_registration;
};

#include "plugin.moc"
