#include "application/playback/listening_history_service.h"
#include "ipc/api/api_context.h"
#include <QDateTime>
#include <cmath>

namespace nekotune {
namespace {
Result<QTimeZone> timezone(const QJsonObject &params) {
    if (!params.contains("timezone"))
        return QTimeZone::systemTimeZone();
    if (!params.value("timezone").isString())
        return failure("timezone must be an IANA timezone name");
    QTimeZone zone(params.value("timezone").toString().toUtf8());
    if (!zone.isValid())
        return failure("Invalid timezone");
    return zone;
}
bool integer(const QJsonValue &value, int low, int high) {
    return value.isDouble() && std::isfinite(value.toDouble()) &&
           value.toDouble() == std::floor(value.toDouble()) && value.toDouble() >= low &&
           value.toDouble() <= high;
}
QString timestamp(qint64 value) {
    return QDateTime::fromMSecsSinceEpoch(value, QTimeZone::UTC).toString(Qt::ISODateWithMs);
}
} // namespace
void registerHistoryApi(IpcRouter &router, ListeningHistoryService &history) {
    router.registerMethod("history.list", [&history](const QJsonObject &params, auto done) {
        auto zone = timezone(params);
        if (!zone) {
            done(error(zone.error()));
            return;
        }
        const int days = params.value("days").toInt(30);
        if ((params.contains("days") && !integer(params.value("days"), 1, 30)) ||
            (days != 1 && days != 7 && days != 30) ||
            (params.contains("limit") && !integer(params.value("limit"), 1, 100)) ||
            (params.contains("offset") && !integer(params.value("offset"), 0, 100000000)) ||
            (params.contains("search") &&
             (!params.value("search").isString() || params.value("search").toString().size() > 500)) ||
            (params.contains("completed_only") && !params.value("completed_only").isBool())) {
            done(error(failure("Invalid history filter or pagination")));
            return;
        }
        const auto today = QDateTime::currentDateTimeUtc().toTimeZone(zone.value()).date();
        HistoryFilter filter{today.addDays(1 - days).startOfDay(zone.value()).toMSecsSinceEpoch(),
                             today.addDays(1).startOfDay(zone.value()).toMSecsSinceEpoch(),
                             params.value("search").toString().trimmed(),
                             params.value("completed_only").toBool(),
                             params.value("limit").toInt(50),
                             params.value("offset").toInt()};
        const auto result = history.list(filter);
        if (!result) {
            done(error(result.error()));
            return;
        }
        QJsonArray items;
        for (const auto &s : result.value().items)
            items.append(QJsonObject{{"id", s.id},
                                     {"song_id", s.songId},
                                     {"song_key", s.songKey},
                                     {"queue_id", s.queueId},
                                     {"title", s.title},
                                     {"artist", s.artist},
                                     {"started_at", timestamp(s.startedAt)},
                                     {"updated_at", timestamp(s.updatedAt)},
                                     {"listened_ms", s.listenedMs},
                                     {"duration_ms", s.durationMs},
                                     {"end_reason", s.reason},
                                     {"completed", s.completed}});
        done(success({{"items", items},
                      {"total", result.value().total},
                      {"offset", filter.offset},
                      {"limit", filter.limit}}));
    });
    router.registerMethod("history.stats", [&history](const QJsonObject &params, auto done) {
        auto zone = timezone(params);
        if (!zone) {
            done(error(zone.error()));
            return;
        }
        auto result = history.statistics(zone.value());
        if (!result) {
            done(error(result.error()));
            return;
        }
        QJsonArray days, songs;
        for (const auto &day : result.value().days)
            days.append(QJsonObject{{"date", day.date}, {"listened_ms", day.listenedMs}});
        for (const auto &song : result.value().songs)
            songs.append(QJsonObject{{"song_key", song.songKey},
                                     {"song_id", song.songId},
                                     {"title", song.title},
                                     {"artist", song.artist},
                                     {"plays", song.plays},
                                     {"listened_ms", song.listenedMs}});
        done(success({{"days", days},
                      {"top_songs", songs},
                      {"week_ms", result.value().weekMs},
                      {"total_sessions", result.value().totalSessions},
                      {"timezone", QString::fromUtf8(zone.value().id())}}));
    });
    router.registerMethod(
        "history.clear",
        [&history](const QJsonObject &params, auto done) {
            if (!params.value("confirm").isBool() || !params.value("confirm").toBool()) {
                done(error(failure("confirm must be true")));
                return;
            }
            done(response(history.clear()));
        },
        true);
}
} // namespace nekotune
