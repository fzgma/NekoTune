#include "controllers/library_storage_controller.h"
#include <QJsonArray>
LibraryStorageController::LibraryStorageController(IpcClient &client) : FeatureController(client) {
    connect(&client, &IpcClient::eventReceived, this, [this](const QJsonObject &event) {
        const auto name = event["event"].toString();
        if (name == "server.connected" || name == "library.storage_changed" ||
            name == "library.scan_finished")
            refresh();
        if (name == "library.task")
            receive(event["data"].toObject());
    });
}
void LibraryStorageController::receive(const QJsonObject &task) {
    if (task.isEmpty())
        return;
    m_task = task;
    m_pending = false;
    m_message = task["message"].toString();
    emit changed();
    if (task["kind"] == "restore_preview" && task["state"] == "finished" &&
        m_preview != task["task"].toString()) {
        m_preview = task["task"].toString();
        emit restorePreviewReady(task["result"].toObject().toVariantMap());
    }
}
void LibraryStorageController::refresh() {
    send(
        "library.storage_status", {},
        [this](const auto &data, const auto &error) {
            if (!error.isEmpty()) {
                m_message = error;
                emit changed();
                return;
            }
            m_status = data;
            const auto active = data["active_task"].toString();
            if (!active.isEmpty())
                send("library.task", {{"task", active}}, [this](const auto &task, const auto &error) {
                    if (error.isEmpty())
                        receive(task);
                });
            else if (!data["tasks"].toArray().isEmpty())
                receive(data["tasks"].toArray().first().toObject());
            emit changed();
        },
        false);
}
void LibraryStorageController::start(const QString &method, const QJsonObject &params) {
    m_message.clear();
    m_pending = true;
    emit changed();
    send(method, params, [this](const auto &data, const auto &error) {
        m_pending = false;
        if (!error.isEmpty()) {
            m_message = error;
            emit changed();
            return;
        }
        if (data.contains("task"))
            send("library.task", {{"task", data["task"]}}, [this](const auto &task, const auto &error) {
                if (error.isEmpty())
                    receive(task);
            });
        else
            refresh();
        emit changed();
    });
}
void LibraryStorageController::migrate() { start("library.migrate"); }
void LibraryStorageController::verify() { start("library.verify"); }
void LibraryStorageController::backup(const QString &directory) {
    start("library.backup", {{"directory", IpcClient::normalizePath(directory)}});
}
void LibraryStorageController::previewRestore(const QString &directory) {
    m_preview.clear();
    start("library.restore_preview", {{"directory", IpcClient::normalizePath(directory)}});
}
void LibraryStorageController::restore() {
    start("library.restore", {{"preview_task", m_preview}, {"confirmed", true}});
}
void LibraryStorageController::cancel() { send("library.cancel", {{"task", m_task["task"]}}); }
void LibraryStorageController::exportSongs(const QVariantList &ids, const QString &directory) {
    start("library.export", {{"song_ids", QJsonArray::fromVariantList(ids)},
                             {"directory", IpcClient::normalizePath(directory)}});
}
void LibraryStorageController::exportPlaylist(int id, const QString &directory) {
    start("library.export", {{"playlist_id", id}, {"directory", IpcClient::normalizePath(directory)}});
}
void LibraryStorageController::removeRollback(const QString &id) {
    start("library.remove_rollback", {{"id", id}, {"confirmed", true}});
}
