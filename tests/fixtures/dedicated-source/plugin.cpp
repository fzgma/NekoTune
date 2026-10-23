#include <nekotune/sdk/extension_plugin.h>
#include <QObject>

using namespace nekotune;

namespace {
class EmptyMusic final : public sdk::IMusicProvider {
  public:
    void search(const QString &, const QString &, std::function<void(Result<sdk::SearchPage>)> done) override {
        done(sdk::SearchPage{});
    }
    void track(const QString &, std::function<void(Result<sdk::Track>)> done) override {
        done(failure("No tracks in this UI fixture"));
    }
    void resolve(const QString &, sdk::ResolvePurpose, std::function<void(Result<sdk::ResolvedAudio>)> done) override {
        done(failure("No audio in this UI fixture"));
    }
};
class EmptyLyrics final : public sdk::ILyricsProvider {
  public:
    void search(const sdk::LyricsQuery &,
                std::function<void(Result<QVector<sdk::LyricsCandidate>>)> done) override {
        done(QVector<sdk::LyricsCandidate>{});
    }
    void resolve(const sdk::LyricsCandidate &candidate,
                 std::function<void(Result<sdk::LyricsResolution>)> done) override {
        done(sdk::LyricsResolution{candidate.document, {}});
    }
};
} // namespace

class DedicatedSourceFixture final : public QObject, public sdk::IExtensionPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID NEKOTUNE_EXTENSION_IID)
    Q_INTERFACES(nekotune::sdk::IExtensionPlugin)
  public:
    void activate(sdk::ExtensionContext &context, sdk::Done done) override {
        m_music = context.music().add("music", &m_musicProvider, {.name = "Test source", .page = "music"});
        m_lyrics = context.lyrics().add("lyrics", &m_lyricsProvider, {.name = "Test lyrics"});
        done({});
    }
    void deactivate() override {
        m_music.reset();
        m_lyrics.reset();
    }

  private:
    EmptyMusic m_musicProvider;
    EmptyLyrics m_lyricsProvider;
    sdk::RegistrationPtr m_music, m_lyrics;
};

#include "plugin.moc"
