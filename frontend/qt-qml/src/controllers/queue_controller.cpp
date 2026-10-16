#include "controllers/queue_controller.h"
#include <QJsonArray>
void QueueController::addSongs(const QVariantList &ids, bool next) {
    send("queue.add", {{"song_ids", QJsonArray::fromVariantList(ids)}, {"next", next}});
}
QueueController::QueueController(IpcClient &client) : FeatureController(client) {
    auto apply = [this](const QJsonObject &data) {
        if (data.contains("queue"))
            m_model.update(data.value("queue").toArray().toVariantList());
    };
    connect(&client, &IpcClient::eventReceived, this, [apply](const QJsonObject &event) {
        apply(event.value("event") == "server.connected" ? event.value("data").toObject() : event);
    });
    connect(&client, &IpcClient::responseReceived, this,
            [this, apply](const QString &method, const QJsonObject &data) {
                if (method.startsWith("queue.") && data.contains("items"))
                    m_model.update(data.value("items").toArray().toVariantList());
                else
                    apply(data);
            });
}
