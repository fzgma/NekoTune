#include "infrastructure/extensions/extension_secrets.h"
#include "app_paths.h"
#include "infrastructure/credentials/credential_store.h"
#include "infrastructure/kugou/kugou_account_session.h"
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>

namespace nekotune {
void extensionSecret(const QVariantMap &params, IExtensionBackend::Completion done) {
    // These two keys retain their pre-plugin secure-store identity. Existing verified migration
    // handles old plaintext/libsecret data; secrets never pass through public business IPC.
    const auto extensionId = params.value("extensionId").toString();
    const auto secretKey = params.value("key").toString();
    const auto action = params.value("operation").toString();
    if (extensionId == "nekotune.kugou" &&
        (secretKey == "account-key" || secretKey == "account-session")) {
        KugouAccountSession legacy(AppPaths::configFile("kugou-account-key"),
                                   AppPaths::configFile("kugou-session.json"));
        const bool accountKey = secretKey == "account-key";
        QString error;
        if (action == "get") {
            error = accountKey ? legacy.keyError : legacy.sessionError;
            QVariant value;
            if (accountKey && legacy.keySaved)
                value = legacy.key;
            else if (!accountKey && !legacy.cookies.isEmpty()) {
                QJsonObject cookies;
                for (auto it = legacy.cookies.cbegin(); it != legacy.cookies.cend(); ++it)
                    cookies.insert(it.key(), it.value());
                value = QString::fromUtf8(QJsonDocument(QJsonObject{{"version", 1}, {"cookies", cookies}})
                                              .toJson(QJsonDocument::Compact));
            }
            done(error.isEmpty() ? Result<QVariantMap>(QVariantMap{{"value", value}})
                                 : Result<QVariantMap>(failure(error)));
            return;
        }
        if (action == "delete")
            error = accountKey ? legacy.clearAccountKey() : legacy.clearSession();
        else if (action == "set" && accountKey)
            error = legacy.saveAccountKey(params.value("value").toString());
        else if (action == "set") {
            const auto document = QJsonDocument::fromJson(params.value("value").toString().toUtf8());
            const auto object = document.object();
            if (!document.isObject() || object.value("version").toInt() != 1 ||
                !object.value("cookies").isObject())
                error = "Invalid session data";
            else {
                legacy.cookies.clear();
                const auto cookies = object.value("cookies").toObject();
                for (auto it = cookies.begin(); it != cookies.end(); ++it) {
                    if (!it.value().isString()) {
                        error = "Invalid session data";
                        break;
                    }
                    legacy.cookies.insert(it.key(), it.value().toString());
                }
                if (error.isEmpty() && !legacy.saveSession())
                    error = legacy.sessionError;
            }
        } else
            error = "Unknown credential operation";
        done(error.isEmpty() ? Result<QVariantMap>(QVariantMap{}) : Result<QVariantMap>(failure(error)));
        return;
    }
    const auto suffix = QString::fromLatin1(
        QCryptographicHash::hash(params.value("key").toString().toUtf8(), QCryptographicHash::Sha256)
            .toHex());
    const auto key =
        AppPaths::extensionsDirectory() + "/" + params.value("extensionId").toString() + "/" + suffix;
    const auto store = systemCredentialStore();
    const auto operation = params.value("operation").toString();
    if (operation == "get") {
        auto value = store->read(key);
        if (!value)
            done(value.error());
        else
            done(QVariantMap{
                {"value", value.value() ? QVariant(QString::fromUtf8(*value.value())) : QVariant()}});
    } else if (operation == "set" || operation == "delete") {
        auto value = operation == "set" ? store->write(key, params.value("value").toString().toUtf8())
                                        : store->remove(key);
        if (!value)
            done(value.error());
        else
            done(QVariantMap{});
    } else
        done(failure("Unknown credential operation"));
}
} // namespace nekotune
