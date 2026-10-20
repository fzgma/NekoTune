#include "storage/resource_repository.h"
#include "app_paths.h"
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSqlError>
#include <QSqlQuery>

namespace nekotune {
ResourceRepository::ResourceRepository(DatabaseSession &session, const QString &root)
    : m_session(session),
      m_root(root.isEmpty()
                 ? (session.libraryRoot().isEmpty() ? AppPaths::musicDirectory() : session.libraryRoot())
                 : root) {}
QString ResourceRepository::safePath(const QString &root, const QString &relative) {
    if (relative.isEmpty() || QDir::isAbsolutePath(relative) || relative.contains('\\'))
        return {};
    const auto parts = relative.split('/');
    for (const auto &part : parts)
        if (part.isEmpty() || part == "." || part == ".." || part.contains(':'))
            return {};
    QString path = QFileInfo(root).absoluteFilePath();
    if (QFileInfo(path).isSymLink())
        return {};
    for (const auto &part : parts) {
        path = QDir(path).filePath(part);
        if (QFileInfo(path).isSymLink())
            return {};
    }
    return path;
}
QString ResourceRepository::audioPath(int id) const {
    QSqlQuery q(m_session.database());
    q.prepare("SELECT o.relative_path FROM song_assets a JOIN audio_objects o ON "
              "o.hash=a.audio_hash "
              "WHERE a.song_id=? AND o.state='ready'");
    q.addBindValue(id);
    return q.exec() && q.next() ? safePath(m_root, q.value(0).toString()) : QString();
}
QString ResourceRepository::audioHash(int id) const {
    QSqlQuery q(m_session.database());
    q.prepare("SELECT audio_hash FROM song_assets WHERE song_id=?");
    q.addBindValue(id);
    return q.exec() && q.next() ? q.value(0).toString() : QString();
}
bool ResourceRepository::hasAudio(int id) const { return !audioHash(id).isEmpty(); }
QHash<int, QPair<QString, QString>> ResourceRepository::audioLocations() const {
    QHash<int, QPair<QString, QString>> result;
    QSqlQuery q(m_session.database());
    if (q.exec("SELECT a.song_id,a.audio_hash,o.relative_path,o.state FROM "
               "song_assets a JOIN audio_objects o ON o.hash=a.audio_hash"))
        while (q.next())
            result.insert(q.value(0).toInt(),
                          {q.value(1).toString(),
                           q.value(3) == "ready" ? safePath(m_root, q.value(2).toString()) : QString()});
    return result;
}
QString ResourceRepository::coverPath(int id) const {
    QSqlQuery q(m_session.database());
    q.prepare("SELECT o.relative_path FROM song_assets a JOIN artwork_objects o "
              "ON o.hash=a.cover_hash WHERE a.song_id=? AND o.state='ready'");
    q.addBindValue(id);
    return q.exec() && q.next() ? safePath(m_root, q.value(0).toString()) : QString();
}
bool ResourceRepository::attach(int id, const QString &hash) {
    QSqlQuery q(m_session.database());
    q.prepare("INSERT INTO song_assets(song_id,audio_hash) VALUES(?,?) "
              "ON CONFLICT(song_id) DO UPDATE SET audio_hash=excluded.audio_hash");
    q.addBindValue(id);
    q.addBindValue(hash);
    if (!q.exec()) {
        m_session.setError(q.lastError().text());
        return false;
    }
    return true;
}
QJsonObject ResourceRepository::encode(const LyricsDocument &d) {
    return {
        {"source", d.source},          {"cover_url", d.coverUrl},         {"krc", d.krcLyrics},
        {"lrc", d.syncedLyrics},       {"plain", d.plainLyrics},          {"instrumental", d.instrumental},
        {"provider_id", d.providerId}, {"title", d.matched.title},        {"artist", d.matched.artist},
        {"album", d.matched.album},    {"duration", d.matched.durationMs}};
}
LyricsDocument ResourceRepository::decode(const QJsonObject &o) {
    LyricsDocument d;
    d.source = o["source"].toString();
    d.coverUrl = o["cover_url"].toString();
    d.krcLyrics = o["krc"].toString();
    d.syncedLyrics = o["lrc"].toString();
    d.plainLyrics = o["plain"].toString();
    d.instrumental = o["instrumental"].toBool();
    d.providerId = o["provider_id"].toInteger();
    d.matched = {o["title"].toString(), o["artist"].toString(), o["album"].toString(),
                 o["duration"].toInteger()};
    d.validate();
    return d;
}
bool ResourceRepository::saveLyrics(int id, const LyricsDocument &d, const QString &kind,
                                    const QByteArray &raw) {
    QSqlQuery q(m_session.database());
    q.prepare("INSERT INTO song_lyrics(song_id,kind,document,raw_krc) VALUES(?,?,?,?) "
              "ON CONFLICT(song_id,kind) DO UPDATE SET "
              "document=excluded.document,raw_krc=excluded.raw_krc");
    q.addBindValue(id);
    q.addBindValue(kind);
    q.addBindValue(QString::fromUtf8(QJsonDocument(encode(d)).toJson(QJsonDocument::Compact)));
    q.addBindValue(raw.isNull() ? QByteArray("") : raw);
    if (q.exec())
        return true;
    m_session.setError(q.lastError().text());
    return false;
}
std::optional<LyricsDocument> ResourceRepository::lyrics(int id, const QString &kind) const {
    QSqlQuery q(m_session.database());
    q.prepare("SELECT document FROM song_lyrics WHERE song_id=? AND kind=?");
    q.addBindValue(id);
    q.addBindValue(kind);
    if (!q.exec() || !q.next())
        return {};
    auto d = decode(QJsonDocument::fromJson(q.value(0).toByteArray()).object());
    return d.isEmpty() ? std::nullopt : std::optional(d);
}
QByteArray ResourceRepository::rawKrc(int id, const QString &kind) const {
    QSqlQuery q(m_session.database());
    q.prepare("SELECT raw_krc FROM song_lyrics WHERE song_id=? AND kind=?");
    q.addBindValue(id);
    q.addBindValue(kind);
    return q.exec() && q.next() ? q.value(0).toByteArray() : QByteArray();
}
QJsonObject ResourceRepository::status() const {
    QJsonObject result{{"root", m_root}};
    const QList<QPair<QString, QString>> counts{
        {"songs", "SELECT count(*) FROM songs"},
        {"objects", "SELECT count(*) FROM audio_objects"},
        {"bytes", "SELECT coalesce(sum(size),0) FROM audio_objects"},
        {"missing", "SELECT (SELECT count(*) FROM audio_objects WHERE state<>'ready') + "
                    "(SELECT count(*) FROM artwork_objects WHERE state<>'ready')"},
        {"pending_migration", "SELECT count(*) FROM songs s LEFT JOIN song_assets a ON a.song_id=s.id "
                              "LEFT JOIN storage_migrations m ON m.song_id=s.id WHERE "
                              "(s.source_provider='' AND a.audio_hash IS NULL) OR m.state IN "
                              "('copying','failed')"}};
    for (const auto &[key, sql] : counts) {
        QSqlQuery q(m_session.database());
        if (q.exec(sql) && q.next())
            result[key] = q.value(0).toLongLong();
    }
    return result;
}
} // namespace nekotune
