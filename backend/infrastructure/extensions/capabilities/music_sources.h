#pragma once
#include "domain/extensions/music_source_catalog.h"
#include "infrastructure/extensions/capabilities/audio_proxy.h"
#include <QMap>

namespace nekotune {
class CapabilityHost;
class MusicSources final : public IMusicSourceCatalog {
    Q_OBJECT
  public:
    explicit MusicSources(CapabilityHost &host, QObject *parent = nullptr);
    void add(const QString &extensionId, const QString &localId, sdk::IMusicProvider *provider,
             QVariantMap descriptor);
    void remove(const QString &extensionId, const QString &localId);
    /// Drops every provider and proxy lease of one extension.
    void removeExtension(const QString &extensionId);
    void close();
    void notifyChanged() { emit sourcesChanged(); }
    QVariantList sources() const override;
    void search(const QString &source, const QString &query, const QString &cursor,
                std::function<void(Result<sdk::SearchPage>)> done) override;
    void track(const QString &source, const QString &id,
               std::function<void(Result<sdk::Track>)> done) override;
    void resolvePlayback(const QString &source, const QString &id,
                         std::function<void(Result<Playback>)> done) override;
    void release(const QUrl &url) override;
    void download(const QString &source, const QString &id, const QString &base, const QString &task,
                  Progress progress, std::function<void(Result<QString>)> done) override;
    void cancelDownload(const QString &task) override;
    void downloaded(const QString &source, const QString &id, const QString &path, int songId, bool existing,
                    std::function<void(Result<QVariantMap>)> done) override;

  private:
    struct Source {
        sdk::IMusicProvider *provider = nullptr;
        QVariantMap descriptor;
    };
    /// Provider of a running source, or the original unavailable error.
    Result<sdk::IMusicProvider *> provider(const QString &source) const;
    void resolve(const QString &source, const QString &id, const QString &purpose,
                 std::function<void(Result<sdk::ResolvedAudio>)> done);
    CapabilityHost &m_host;
    QHash<QString, QMap<QString, Source>> m_sources;
    AudioProxy m_audio;
};
} // namespace nekotune
