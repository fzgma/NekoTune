#include "runtime/backend_runtime.h"
#include "app_paths.h"
#include "infrastructure/library/library_archive.h"
#include "runtime/backend_session.h"
#include <QDir>
#include <QFileInfo>
namespace nekotune {
BackendRuntime::BackendRuntime(QObject *parent) : BackendRuntime(PlaybackBackendFactory{}, parent) {}
BackendRuntime::BackendRuntime(PlaybackBackendFactory audioFactory, QObject *parent)
    : QObject(parent), m_audioFactory(std::move(audioFactory)) {}
BackendRuntime::~BackendRuntime() { stop(); }
bool BackendRuntime::start() {
    if (m_thread.isRunning())
        return m_session != nullptr;
    if (!AppPaths::prepare(&m_error))
        return false;
    m_libraryLock = std::make_unique<QLockFile>(AppPaths::configFile("library.lock"));
    const auto database = DatabaseSession::defaultDatabasePath();
    QDir().mkpath(QFileInfo(database).absolutePath());
    m_databaseLock = std::make_unique<QLockFile>(database + ".library-lock");
    if (!m_libraryLock->tryLock(0) || !m_databaseLock->tryLock(0)) {
        m_error = "Library is already open in another backend";
        m_libraryLock.reset();
        m_databaseLock.reset();
        return false;
    }
    const auto recovery = LibraryArchive::recoverRestore(AppPaths::musicDirectory(), database);
    if (!recovery) {
        m_error = recovery.error().message;
        m_libraryLock.reset();
        m_databaseLock.reset();
        return false;
    }
    m_context = new QObject;
    m_context->moveToThread(&m_thread);
    connect(&m_thread, &QThread::finished, m_context, &QObject::deleteLater);
    m_thread.start();
    bool ok = false;
    // Construct the session here so SQL, socket and network objects acquire backend-thread affinity.
    // Blocking also keeps the captured startup result alive until initialization finishes.
    QMetaObject::invokeMethod(
        m_context,
        [this, &ok] {
            m_session = new BackendSession(m_audioFactory ? m_audioFactory() : nullptr);
            wireSession();
            ok = m_session->start();
            m_error = m_session->errorString();
            m_serverName = m_session->serverName();
            if (!ok) {
                delete m_session;
                m_session = nullptr;
            }
        },
        Qt::BlockingQueuedConnection);
    if (!ok)
        stop();
    return ok;
}
void BackendRuntime::stop() {
    if (!m_thread.isRunning())
        return;
    // Destroy services while their event loop still runs, so shutdown can complete worker callbacks.
    QMetaObject::invokeMethod(
        m_context,
        [this] {
            delete m_session;
            m_session = nullptr;
        },
        Qt::BlockingQueuedConnection);
    m_thread.quit();
    m_thread.wait();
    m_context = nullptr;
    m_libraryLock.reset();
    m_databaseLock.reset();
}
void BackendRuntime::wireSession() {
    connect(
        m_session, &BackendSession::restoreRequested, m_context,
        [this](const auto &journal, const auto &task) { restore(journal, task); }, Qt::QueuedConnection);
}
void BackendRuntime::restore(const QJsonObject &journal, const QString &task) {
    delete m_session;
    m_session = nullptr;
    const auto root = AppPaths::musicDirectory(), database = DatabaseSession::defaultDatabasePath();
    auto result = LibraryArchive::applyRestore(journal, root, database);
    QString message = result ? QString() : result.error().message;
    if (!result) {
        auto recovered = LibraryArchive::rollbackRestore(journal, root, database);
        if (!recovered) {
            m_error = recovered.error().message;
            return;
        }
    }
    m_session = new BackendSession(m_audioFactory ? m_audioFactory() : nullptr);
    wireSession();
    if (!m_session->start()) {
        message = m_session->errorString();
        delete m_session;
        m_session = nullptr;
        auto rolled = LibraryArchive::rollbackRestore(journal, root, database);
        if (!rolled) {
            m_error = rolled.error().message;
            return;
        }
        m_session = new BackendSession(m_audioFactory ? m_audioFactory() : nullptr);
        wireSession();
        if (!m_session->start()) {
            m_error = m_session->errorString();
            delete m_session;
            m_session = nullptr;
            return;
        }
        result = failure(message);
    }
    if (result) {
        auto recovered = LibraryArchive::recoverRestore(root, database);
        if (!recovered)
            message = recovered.error().message;
    }
    m_session->restored(task, bool(result), message);
}
} // namespace nekotune
