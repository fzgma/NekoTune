#include "controllers/history_controller.h"
#include <QJsonArray>
#include <QTimeZone>

HistoryController::HistoryController(IpcClient &client) : FeatureController(client) {
    m_refreshTimer.setSingleShot(true);
    m_refreshTimer.setInterval(250);
    connect(&m_refreshTimer, &QTimer::timeout, this, &HistoryController::refresh);
    m_dateTimer.setInterval(60000);
    connect(&m_dateTimer, &QTimer::timeout, this, &HistoryController::refresh);
    m_dateTimer.start();
    connect(&client, &IpcClient::eventReceived, this, [this](const QJsonObject &event) {
        const auto name = event.value("event").toString();
        if (name == "server.connected")
            refresh();
        else if (name == "history.changed" && !m_refreshTimer.isActive())
            m_refreshTimer.start();
    });
}
void HistoryController::filter(int days, const QString &search, bool completedOnly) {
    m_days = days;
    m_search = search;
    m_completedOnly = completedOnly;
    m_offset = 0;
    loadPage();
}
void HistoryController::goToPage(int offset) {
    if (offset < 0 || (offset >= m_total && offset != 0))
        return;
    m_offset = offset;
    loadPage();
}
void HistoryController::loadPage() {
    const auto generation = ++m_generation;
    m_loading = true;
    m_error.clear();
    emit changed();
    send(
        "history.list",
        {{"days", m_days},
         {"search", m_search},
         {"completed_only", m_completedOnly},
         {"offset", m_offset},
         {"limit", 50},
         {"timezone", QString::fromUtf8(QTimeZone::systemTimeZoneId())}},
        [this, generation](const QJsonObject &data, const QString &error) {
            if (generation != m_generation)
                return;
            m_loading = false;
            m_error = error;
            if (error.isEmpty()) {
                m_records.update(data.value("items").toArray().toVariantList());
                m_total = data.value("total").toInteger();
                m_loaded = true;
                if (m_offset >= m_total && m_offset != 0) {
                    m_offset = 0;
                    loadPage();
                    return;
                }
            }
            emit changed();
        },
        false);
}
void HistoryController::refresh() {
    if (!m_client.connected())
        return;
    loadPage();
    const auto generation = ++m_statsGeneration;
    send(
        "history.stats", {{"timezone", QString::fromUtf8(QTimeZone::systemTimeZoneId())}},
        [this, generation](const QJsonObject &data, const QString &error) {
            if (generation != m_statsGeneration)
                return;
            m_statsError = error;
            if (error.isEmpty())
                m_stats = data.toVariantMap();
            emit changed();
        },
        false);
}
void HistoryController::clearHistory() {
    ++m_generation;
    ++m_statsGeneration;
    m_loading = false;
    send("history.clear", {{"confirm", true}}, [this](const QJsonObject &, const QString &error) {
        if (error.isEmpty()) {
            m_offset = 0;
            m_total = 0;
            m_records.update({});
            m_stats.clear();
            refresh();
        }
        emit changed();
    });
}
