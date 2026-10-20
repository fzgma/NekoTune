#pragma once
#include "domain/lyrics/lyrics_types.h"
#include "storage/database_session.h"
#include <QJsonObject>

namespace nekotune {
// All SQL operations run on the session owner. Paths in the catalog are
// relative to root.
class ResourceRepository {
  public:
    explicit ResourceRepository(DatabaseSession &session, const QString &root = {});
    QString root() const { return m_root; }
    QString audioPath(int songId) const;
    QString audioHash(int songId) const;
    QString coverPath(int songId) const;
    bool hasAudio(int songId) const;
    QHash<int, QPair<QString, QString>> audioLocations() const;
    bool attach(int songId, const QString &hash);
    bool saveLyrics(int songId, const LyricsDocument &document, const QString &kind,
                    const QByteArray &rawKrc = {});
    std::optional<LyricsDocument> lyrics(int songId, const QString &kind) const;
    QByteArray rawKrc(int songId, const QString &kind) const;
    QJsonObject status() const;
    static QJsonObject encode(const LyricsDocument &document);
    static LyricsDocument decode(const QJsonObject &object);
    static QString safePath(const QString &root, const QString &relative);

  private:
    DatabaseSession &m_session;
    QString m_root;
};
} // namespace nekotune
