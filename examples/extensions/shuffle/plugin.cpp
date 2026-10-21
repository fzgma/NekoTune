#include <nekotune/sdk/extension_plugin.h>
#include <nekotune/sdk/shuffle_provider.h>
#include <QObject>
#include <random>

using namespace nekotune;

class ArtistShuffle final : public sdk::IShuffleProvider {
  public:
    Result<sdk::ShuffleSelection> select(const sdk::ShuffleRequest &request) override {
        QString currentArtist;
        for (const auto &track : request.queue)
            if (track.queueId == request.currentId) currentArtist = track.artist;
        QList<int> otherArtists, otherTracks;
        for (const auto &track : request.queue) {
            if (track.queueId == request.currentId) continue;
            otherTracks.append(track.queueId);
            if (!currentArtist.isEmpty() && !track.artist.isEmpty() &&
                track.artist.compare(currentArtist, Qt::CaseInsensitive) != 0)
                otherArtists.append(track.queueId);
        }
        const auto &candidates = otherArtists.isEmpty() ? otherTracks : otherArtists;
        if (candidates.isEmpty()) {
            if (request.queue.isEmpty()) return failure("Queue is empty");
            return sdk::ShuffleSelection{request.queue.first().queueId, {}};
        }
        std::mt19937 random(request.seed);
        const auto index = std::uniform_int_distribution<int>(0, candidates.size() - 1)(random);
        return sdk::ShuffleSelection{candidates.at(index), {}};
    }
};

class ShuffleExample final : public QObject, public sdk::IExtensionPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID NEKOTUNE_EXTENSION_IID)
    Q_INTERFACES(nekotune::sdk::IExtensionPlugin)
  public:
    void activate(sdk::IExtensionContext &context, std::function<void(Result<void>)> done) override {
        auto *shuffle = sdk::shuffleContext(context);
        if (!shuffle) {
            done(failure("Artist-aware shuffle requires a host with NekoTune SDK 2.1 shuffle support"));
            return;
        }
        m_registration = shuffle->registerShuffleStrategy("artist", &m_provider,
                                                           {{"name", "Avoid consecutive artists"}});
        done(m_registration ? Result<void>{} : Result<void>{failure("Unable to register shuffle strategy")});
    }
    void deactivate() override { m_registration.reset(); }
  private:
    ArtistShuffle m_provider;
    sdk::RegistrationPtr m_registration;
};

#include "plugin.moc"
