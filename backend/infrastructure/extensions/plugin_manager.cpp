#include "infrastructure/extensions/plugin_manager.h"
#include "infrastructure/extensions/runtime/call_guard.h"
#include "infrastructure/extensions/runtime/extension_runtime.h"
#include "infrastructure/extensions/runtime/host_context.h"
#include "infrastructure/extensions/runtime/dev_watcher.h"
#include "infrastructure/extensions/runtime/registration_scope.h"
#include "infrastructure/appearance/appearance_service.h"
#include <nekotune/sdk/extension_plugin.h>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QPluginLoader>
#include <QPointer>
#include <QThread>
#include <QTimer>
#include <algorithm>

namespace nekotune {
namespace {
using Done = std::function<void(Result<void>)>;
/// Run asynchronous steps one after another; stopOnError ends the chain at the first failure.
void sequence(QStringList items, std::function<void(const QString &, Done)> step, bool stopOnError, Done done) {
    if (items.isEmpty()) {
        done({});
        return;
    }
    const auto first = items.takeFirst();
    step(first, [items, step, stopOnError, done](Result<void> result) {
        if (!result && stopOnError)
            done(result);
        else
            sequence(items, step, stopOnError, done);
    });
}
} // namespace

PluginManager::PluginManager(QObject *parent) : IExtensionBackend(parent), m_registry(m_packages.registryPath()) {}
PluginManager::~PluginManager() { shutdown(); }

bool PluginManager::running(const QString &id) const {
    const auto run = m_runs.value(id);
    return run && run->state == ExtensionState::Running;
}
PendingCalls *PluginManager::pendingCalls(const QString &id) {
    const auto run = m_runs.value(id);
    return run && run->state == ExtensionState::Running ? &run->calls : nullptr;
}

void PluginManager::post(std::function<void()> work) {
    if (QThread::currentThread() == thread())
        work();
    else
        QMetaObject::invokeMethod(this, std::move(work), Qt::QueuedConnection);
}
Result<void> PluginManager::addRegistration(const std::shared_ptr<Run> &run, const QString &kind, const QString &id,
                                            QVariantMap descriptor) {
    if (run->state == ExtensionState::Stopping)
        return failure("Extension is stopping");
    const auto added = run->registrations.add(kind, id, std::move(descriptor), run->id, run->directory,
                                               m_entries.value(run->id).manifest.contributes);
    if (added)
        notifyChanged();
    return added;
}
void PluginManager::notifyChanged() {
    if (m_closing || m_changePending)
        return;
    m_changePending = true;
    QTimer::singleShot(0, this, [this] {
        m_changePending = false;
        if (m_closing)
            return;
        m_music.notifyChanged();
        m_lyrics.notifyChanged();
        emit m_shuffle.catalogueChanged();
        emit changed();
        auto event = snapshot();
        event.insert("event", "extensions.changed");
        emit eventReady(event);
    });
}
void PluginManager::reportError(const QString &message) {
    emit eventReady({{"event", "extensions.error"}, {"message", m_vault.redact(message)}});
}
void PluginManager::reloading(const QStringList &ids, bool active) {
    emit eventReady({{"event", "extensions.reloading"}, {"ids", ids}, {"active", active}});
}
void PluginManager::recordLog(const QString &id, const QString &level, const QString &message) {
    m_log.record(id, level, message);
}

void PluginManager::load() {
    auto state = m_registry.load();
    m_registryExtra = std::move(state.extra);
    m_legacySelections = std::move(state.selections);
    m_developmentDirectories = std::move(state.developmentDirectories);
    for (auto &record : state.entries) {
        Entry entry;
        static_cast<ExtensionRecord &>(entry) = std::move(record);
        entry.error = entry.failure;
        entry.state = entry.error.isEmpty() ? ExtensionState::Disabled : ExtensionState::Failed;
        auto manifest = readManifest(entry.directory);
        if (manifest && manifest.value().id == entry.id)
            entry.manifest = manifest.value();
        else {
            entry.manifest = ExtensionManifest::placeholder(entry.id);
            entry.state = ExtensionState::Failed;
            entry.error = manifest ? QStringLiteral("Installed extension ID changed") : manifest.error().message;
        }
        const auto id = entry.id;
        m_entries.insert(id, std::move(entry));
        m_order.append(id);
    }
}
Result<void> PluginManager::save() {
    RegistryState state;
    for (const auto &id : std::as_const(m_order)) {
        const auto &entry = m_entries[id];
        if (entry.development && !m_developmentDirectories.contains(entry.directory))
            m_developmentDirectories.append(entry.directory);
        if (entry.previous && entry.previous->development &&
            !m_developmentDirectories.contains(entry.previous->directory))
            m_developmentDirectories.append(entry.previous->directory);
        auto record = static_cast<const ExtensionRecord &>(entry);
        record.failure = entry.state == ExtensionState::Failed ? m_vault.redact(entry.error) : QString();
        state.entries.append(std::move(record));
    }
    state.extra = m_registryExtra;
    // Preserve migration input through unrelated saves if appearance.json could not be written.
    if (!m_legacySelections.isEmpty())
        state.extra.insert("selections", m_legacySelections);
    state.developmentDirectories = m_developmentDirectories;
    return m_registry.save(state);
}

void PluginManager::enqueue(Mutation mutation, Completion done) {
    m_mutations.emplace_back(std::move(mutation), std::move(done));
    // Always start from the event loop so lifecycle work never runs inside extension code.
    QTimer::singleShot(0, this, &PluginManager::runMutations);
}
void PluginManager::runMutations() {
    if (m_mutating || m_mutations.empty())
        return;
    m_mutating = true;
    auto [mutation, done] = std::move(m_mutations.front());
    m_mutations.pop_front();
    QPointer<PluginManager> guard(this);
    mutation([guard, done](Result<QVariantMap> result) {
        // Only collect after the complete operation, including dependent restarts and rollback.
        if (guard)
            guard->cleanUnusedPackages();
        done(std::move(result));
        if (!guard)
            return;
        guard->m_mutating = false;
        QTimer::singleShot(0, guard, &PluginManager::runMutations);
    });
}

void PluginManager::start(HostCall hostCall) {
    m_hostCall = std::move(hostCall);
    m_safeMode = qEnvironmentVariable("NEKOTUNE_SAFE_MODE") == "1";
    QDir().mkpath(m_packages.root());
    const auto sentinel = QDir(m_packages.root()).filePath("crash-sentinel");
    const auto crashed = crashguard::takeSentinel(sentinel);
    const auto previous = m_session.begin(!crashed.isEmpty());
    if (!m_safeMode && previous.unattributed >= 2) {
        m_safeMode = true;
        m_safeModeReason = "repeated_crashes";
        reportError("Extension code was skipped after repeated crashes that could not be attributed: " +
                    previous.loaded.join(", "));
    }
    crashguard::install(sentinel);
    watchdog::start(thread(), {}, [](const QString &id) { crashguard::writeSentinel(id); });
    // Development copies from the previous process are no longer mapped.
    QDir(m_packages.runtimeDirectory()).removeRecursively();
    load();
    if (m_appearance && !m_legacySelections.isEmpty()) {
        if (auto migrated = m_appearance->migrate(m_legacySelections); migrated) {
            m_legacySelections.clear();
            if (auto saved = save(); !saved)
                reportError("Cannot persist appearance migration: " + saved.error().message);
        } else
            reportError("Cannot migrate appearance selections: " + migrated.error().message);
    }
    if (m_entries.contains(crashed)) {
        auto &entry = m_entries[crashed];
        entry.enabled = false;
        entry.state = ExtensionState::Failed;
        entry.error = "The backend crashed while this extension was running; it has been disabled";
        if (auto saved = save(); !saved)
            reportError("Cannot persist crashed extension state: " + saved.error().message);
        reportError(QStringLiteral("%1: %2").arg(crashed, entry.error));
    }
    m_catalogueReady = true;
    notifyChanged();
    enqueue(
        [this](Completion done) {
            if (m_safeMode) {
                done(QVariantMap{});
                return;
            }
            QStringList order;
            const auto selectedOwners = m_appearance ? m_appearance->selectedOwners() : QStringList{};
            for (const auto &id : std::as_const(m_order))
                if (selectedOwners.contains(id))
                    order.append(id);
            for (const auto &id : std::as_const(m_order))
                if (!selectedOwners.contains(id))
                    order.append(id);
            startup(0, order, [done](Result<void>) { done(QVariantMap{}); });
        },
        [this](Result<QVariantMap>) {
            m_startupComplete = true;
            notifyChanged();
        });
}
void PluginManager::startup(int, QStringList order, Done finished) {
    sequence(
        order,
        [this](const QString &id, Done next) {
            if (!m_entries.contains(id) || !m_entries[id].enabled || m_entries[id].state == ExtensionState::Failed) {
                next({});
                return;
            }
            const auto block = [this, id](const QString &message) {
                auto &entry = m_entries[id];
                if (entry.state != ExtensionState::Failed)
                    entry.state = ExtensionState::Blocked;
                entry.error = message;
                notifyChanged();
            };
            const auto dependencies = dependencyOrder(id);
            if (!dependencies) {
                block(dependencies.error().message);
                next({});
                return;
            }
            for (const auto &dependency : dependencies.value())
                if (!m_entries[dependency].enabled || m_entries[dependency].state == ExtensionState::Failed) {
                    block("Dependency is disabled or failed; enable it manually");
                    next({});
                    return;
                }
            enable(id, false, [block, next](Result<void> result) {
                if (!result)
                    block(result.error().message);
                next({});
            });
        },
        false, std::move(finished));
}
void PluginManager::shutdown() {
    if (m_closing)
        return;
    watchdog::stop();
    m_closing = true;
    m_music.close();
    for (auto it = m_order.crbegin(); it != m_order.crend(); ++it)
        stop(*it);
    const auto mutations = std::exchange(m_mutations, {});
    for (const auto &[mutation, done] : mutations)
        done(failure("Extension runtime stopped"));
    m_session.end();
}

DependencyNodes PluginManager::dependencyNodes() const {
    DependencyNodes nodes;
    for (auto it = m_entries.cbegin(); it != m_entries.cend(); ++it)
        nodes.insert(it.key(), {it->manifest.version, it->manifest.dependencies});
    return nodes;
}
Result<QStringList> PluginManager::dependencyOrder(const QString &id) const {
    return nekotune::dependencyOrder(id, dependencyNodes());
}
QStringList PluginManager::dependents(const QString &id) const {
    return dependentsOf(id, m_order, dependencyNodes());
}

void PluginManager::enable(const QString &id, bool trusted, Done done) {
    if (m_safeMode) {
        done(failure("Restart without --safe-mode to enable extensions"));
        return;
    }
    if (!m_entries.contains(id)) {
        done(failure("Extension not found"));
        return;
    }
    if (trusted)
        m_entries[id].trusted = true;
    QSet<QString> visited;
    std::function<Result<void>(const QString &)> inspect = [&](const QString &candidate) -> Result<void> {
        if (visited.contains(candidate))
            return {};
        visited.insert(candidate);
        if (!m_entries.contains(candidate))
            return failure("Dependency missing: " + candidate);
        auto &installed = m_entries[candidate];
        auto manifest = readManifest(installed.directory);
        if (!manifest)
            return manifest.error();
        if (manifest.value().id != candidate)
            return failure("Manifest ID changed; reinstall the extension");
        installed.manifest = manifest.value();
        const auto dependencies = installed.manifest.dependencies;
        for (auto it = dependencies.cbegin(); it != dependencies.cend(); ++it)
            if (auto nested = inspect(it.key()); !nested)
                return nested;
        return {};
    };
    if (auto inspected = inspect(id); !inspected) {
        done(inspected);
        return;
    }
    if (m_entries[id].manifest.needsTrustToRun() && !m_entries[id].trusted) {
        done(failure("Trust must be explicitly granted to " + id));
        return;
    }
    const auto order = dependencyOrder(id);
    if (!order) {
        done(order.error());
        return;
    }
    for (const auto &dependency : order.value())
        if (m_entries[dependency].manifest.needsTrustToRun() && !m_entries[dependency].trusted) {
            done(failure("Enable and trust dependency first: " + dependency));
            return;
        }
    startChain(order.value(), 0, [this, done](Result<void> result) {
        if (result)
            if (auto saved = save(); !saved)
                result = saved;
        notifyChanged();
        done(result);
    });
}
void PluginManager::startChain(QStringList order, int index, Done done) {
    if (index >= order.size()) {
        done({});
        return;
    }
    const auto id = order[index];
    startOne(id, [this, order, index, id, done](Result<void> result) {
        if (!m_entries.contains(id)) {
            done(failure("Extension not found"));
            return;
        }
        auto &candidate = m_entries[id];
        if (!result) {
            if (!candidate.previous) {
                save();
                done(result);
                return;
            }
            stop(id);
            Entry restored;
            restored.id = id;
            restored.directory = candidate.previous->directory;
            restored.development = candidate.previous->development;
            restored.trusted = candidate.trusted;
            auto manifest = readManifest(restored.directory);
            if (!manifest) {
                save();
                done(failure("Activation failed and rollback is unavailable: " + manifest.error().message));
                return;
            }
            restored.manifest = manifest.value();
            m_entries[id] = restored;
            const auto message = result.error().message;
            startOne(id, [this, id, message, done](Result<void> restarted) {
                if (restarted && m_entries.contains(id))
                    m_entries[id].enabled = true;
                save();
                notifyChanged();
                done(failure("Activation rolled back: " + message));
            });
            return;
        }
        candidate.enabled = true;
        candidate.previous.reset();
        startChain(order, index + 1, done);
    });
}
void PluginManager::startOne(const QString &id, Done done) {
    auto &entry = m_entries[id];
    if (entry.manifest.needsTrustToRun() && !entry.trusted) {
        done(failure("Trust must be explicitly granted to " + id));
        return;
    }
    if (const auto existing = m_runs.value(id)) {
        if (existing->state == ExtensionState::Running)
            done({});
        else
            existing->waiters.append(std::move(done));
        return;
    }
    if (!entry.manifest.unavailable.isEmpty()) {
        entry.state = ExtensionState::Failed;
        entry.error = entry.manifest.unavailable;
        entry.enabled = false;
        notifyChanged();
        done(failure(entry.error));
        return;
    }
    if (entry.manifest.nativeModules) {
        m_nativeActivated.insert(id);
        m_retainedDirectories.insert(entry.directory);
    }
    QDir().mkpath(m_packages.dataDirectory(id));
    QFile::setPermissions(m_packages.dataDirectory(id), QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    entry.error.clear();
    entry.state = ExtensionState::Starting;
    auto run = std::make_shared<Run>();
    run->id = id;
    run->directory = entry.directory;
    run->generation = ++m_generation;
    m_runs.insert(id, run);
    if (entry.manifest.library.isEmpty()) {
        run->state = ExtensionState::Running;
        if (entry.development)
            watchDevelopment(run);
        notifyChanged();
        done({});
        return;
    }
    run->waiters.append(std::move(done));
    const auto path = m_loader.loadablePath(entry.manifest, entry.development);
    if (!path) {
        activated(run, path.error());
        return;
    }
    const auto loader = m_loader.loader(path.value());
    if (!loader) {
        activated(run, loader.error());
        return;
    }
    // Once mapped, the package directory must survive until exit (Windows also locks the DLL).
    m_retainedDirectories.insert(entry.directory);
    bool loaded = false;
    {
        const NativeLoader::SearchPath searchPath(path.value());
        m_session.loaded(id);
        if (!guarded(id, [&] { loaded = loader.value()->load(); }))
            return;
    }
    const auto plugin = NativeLoader::instance(loader.value(), loaded);
    if (!plugin) {
        activated(run, plugin.error());
        return;
    }
    const auto data = m_packages.dataDirectory(id);
    m_storage.load(id);
    m_vault.load(id);
    run->plugin = plugin.value();
    run->context = std::make_unique<HostContext>(this, run, entry.manifest.id, entry.manifest.version,
                                                entry.directory, data, readConfig(id));
    std::weak_ptr<Run> weak = run;
    run->startTimer = std::make_unique<QTimer>();
    run->startTimer->setSingleShot(true);
    connect(run->startTimer.get(), &QTimer::timeout, this, [this, weak] {
        if (const auto alive = weak.lock(); alive && alive->state == ExtensionState::Starting)
            activated(alive, failure("Activation timed out"));
    });
    run->startTimer->start(timeouts::activation);
    notifyChanged();
    QPointer<PluginManager> guard(this);
    guarded(id, [&] {
        plugin.value()->activate(*run->context, [guard, weak](Result<void> result) {
            if (!guard)
                return;
            guard->post([guard, weak, result] {
                if (const auto alive = weak.lock(); alive && guard && alive->state == ExtensionState::Starting)
                    guard->activated(alive, result);
            });
        });
    });
}
void PluginManager::activated(const std::shared_ptr<Run> &run, Result<void> result) {
    if (run->startTimer)
        run->startTimer->stop();
    const auto waiters = std::exchange(run->waiters, {});
    if (result) {
        run->state = ExtensionState::Running;
        emit m_shuffle.catalogueChanged();
        if (m_entries.value(run->id).development)
            watchDevelopment(run);
        notifyChanged();
    } else {
        const auto message = result.error().message;
        recordLog(run->id, "error", message);
        stop(run->id);
        if (m_entries.contains(run->id)) {
            auto &entry = m_entries[run->id];
            entry.state = ExtensionState::Failed;
            entry.error = message;
            entry.enabled = false;
        }
        notifyChanged();
    }
    for (const auto &waiter : waiters)
        waiter(result);
}
void PluginManager::stop(const QString &id) {
    const auto run = m_runs.value(id);
    if (!run || run->state == ExtensionState::Stopping)
        return;
    run->state = ExtensionState::Stopping;
    run->startTimer.reset();
    run->watcher.reset();
    run->pcm.clear();
    if (run->plugin)
        guarded(id, [&] { run->plugin->deactivate(); });
    // Every remaining function object or provider pointer belongs to the plugin; drop them before
    // anything else can reach them.
    m_bus.removeExtension(id);
    m_music.removeExtension(id);
    m_lyrics.removeExtension(id);
    m_shuffle.removeExtension(id);
    run->registrations.clear();
    run->calls.failAll("Extension stopped");
    for (const auto &waiter : std::exchange(run->waiters, {}))
        waiter(failure("Extension stopped"));
    m_runs.remove(id);
    m_storage.unload(id);
    if (m_entries.contains(id))
        m_entries[id].state = ExtensionState::Disabled;
    notifyChanged();
}
void PluginManager::fault(const QString &id, const QString &message) {
    const auto run = m_runs.value(id);
    if (!run || run->state == ExtensionState::Stopping || !m_entries.contains(id))
        return;
    if (run->state == ExtensionState::Starting) {
        activated(run, failure(message));
        return;
    }
    recordLog(id, "error", message);
    stop(id);
    auto &entry = m_entries[id];
    entry.error = message;
    entry.state = ExtensionState::Failed;
    entry.enabled = false;
    auto others = dependents(id);
    for (auto it = others.crbegin(); it != others.crend(); ++it)
        if (*it != id) {
            stop(*it);
            m_entries[*it].state = ExtensionState::Blocked;
            m_entries[*it].error = "Dependency unavailable: " + id;
        }
    if (auto saved = save(); !saved)
        reportError(QStringLiteral("Cannot persist disabled extension %1: %2").arg(id, saved.error().message));
    notifyChanged();
}
void PluginManager::disable(const QString &id) {
    const auto affected = dependents(id);
    for (auto it = affected.crbegin(); it != affected.crend(); ++it) {
        stop(*it);
        if (*it != id) {
            m_entries[*it].state = ExtensionState::Blocked;
            m_entries[*it].error = "Dependency disabled: " + id;
        }
    }
    m_entries[id].enabled = false;
    notifyChanged();
}
void PluginManager::reload(const QString &id, Done done) {
    if (!m_entries.contains(id)) {
        done(failure("Extension not found"));
        return;
    }
    if (m_entries[id].manifest.nativeModules) {
        done(failure("Native QML modules require application restart"));
        return;
    }
    const auto manifest = readManifest(m_entries[id].directory);
    if (!manifest) {
        done(manifest.error());
        return;
    }
    if (manifest.value().id != id) {
        done(failure("Reload cannot change extension ID"));
        return;
    }
    const auto affected = dependents(id);
    reloading(affected, true);
    for (auto it = affected.crbegin(); it != affected.crend(); ++it)
        stop(*it);
    m_entries[id].manifest = manifest.value();
    sequence(
        affected,
        [this](const QString &dependent, Done next) {
            if (m_entries.value(dependent).enabled)
                enable(dependent, false, next);
            else
                next({});
        },
        true,
        [this, affected, done](Result<void> result) {
            reloading(affected, false);
            done(result);
        });
}
void PluginManager::install(const QVariantMap &params, Completion done) {
    const auto path = params.value("path").toString();
    if (path.isEmpty()) {
        done(failure("Local package path required"));
        return;
    }
    const bool development = params.value("development").toBool();
    const bool themeOnly = params.value("themeOnly").toBool() || path.endsWith(".nktheme.zip");
    const auto unpacked = development ? m_packages.developmentDirectory(path) : m_packages.unpack(path, themeOnly);
    if (!unpacked) {
        done(unpacked.error());
        return;
    }
    const auto directory = unpacked.value();
    // Re-read at the final location: library paths in the manifest are absolute.
    auto read = readManifest(directory);
    if (!read) {
        if (!development)
            QDir(directory).removeRecursively();
        done(read.error());
        return;
    }
    const auto discard = [this, development, directory, done](const QString &message) {
        if (!development)
            QDir(directory).removeRecursively();
        done(failure(message));
    };
    const auto manifest = read.value();
    const auto id = manifest.id;
    if (params.contains("expectedId")) {
        const auto expected = params.value("expectedId").toString();
        if (!m_entries.contains(expected)) {
            discard("Extension to update is not installed");
            return;
        }
        if (id != expected) {
            discard("theme_update_id_mismatch");
            return;
        }
    }
    if (params.value("themeOnly").toBool() && !manifest.isTheme()) {
        discard("Use the extension manager to install executable extensions");
        return;
    }
    if (m_nativeActivated.contains(id)) {
        discard("Native QML modules were loaded; restart in safe mode before updating");
        return;
    }
    const bool hadOld = m_entries.contains(id);
    if (hadOld && !params.value("replace").toBool()) {
        discard("Extension already installed; set replace to update");
        return;
    }
    const auto old = m_entries.value(id);
    const auto affected = hadOld ? dependents(id) : QStringList{};
    if (!affected.isEmpty())
        reloading(affected, true);
    for (auto it = affected.crbegin(); it != affected.crend(); ++it)
        stop(*it);
    const bool trustRetained = hadOld && old.trusted && (old.manifest.needsTrust() || !manifest.themeHasCode);
    Entry entry;
    entry.id = id;
    entry.directory = directory;
    entry.development = development;
    entry.manifest = manifest;
    // QML newly added to a theme stays unloaded until the user trusts it; the package keeps running.
    entry.trusted = trustRetained;
    entry.enabled = hadOld && old.enabled;
    if (hadOld && !old.enabled)
        entry.previous = old.previous ? *old.previous
                                      : PreviousPackage{old.directory, old.development, false, old.trusted};
    m_entries.insert(id, entry);
    if (!hadOld)
        m_order.append(id);
    auto finish = [this, affected, id, done](Result<void> result) {
        if (!affected.isEmpty())
            reloading(affected, false);
        notifyChanged();
        if (!result)
            done(result.error());
        else
            done(QVariantMap{{"extension", publicEntry(id)}});
    };
    auto restartOthers = [this, affected, id](Done next) {
        QStringList others;
        for (const auto &other : affected)
            if (other != id && m_entries.value(other).enabled)
                others.append(other);
        sequence(others, [this](const QString &other, Done step) { enable(other, false, step); }, true, next);
    };
    auto rollback = [this, hadOld, old, id, restartOthers, finish](const QString &message) {
        stop(id);
        if (!hadOld) {
            m_entries.remove(id);
            m_order.removeAll(id);
            save();
            finish(failure("Update rolled back: " + message));
            return;
        }
        m_entries.insert(id, old);
        auto restart = [this, restartOthers, finish, message](Result<void>) {
            restartOthers([this, finish, message](Result<void>) {
                save();
                finish(failure("Update rolled back: " + message));
            });
        };
        if (old.enabled)
            enable(id, false, restart);
        else
            restart({});
    };
    auto activate = [this, id](Done next) {
        if (m_entries[id].enabled)
            enable(id, false, next);
        else
            next({});
    };
    activate([this, restartOthers, rollback, finish](Result<void> result) {
        if (!result) {
            rollback(result.error().message);
            return;
        }
        if (auto saved = save(); !saved) {
            rollback(saved.error().message);
            return;
        }
        restartOthers([rollback, finish](Result<void> restarted) {
            if (!restarted)
                rollback(restarted.error().message);
            else
                finish({});
        });
    });
}
void PluginManager::uninstall(const QVariantMap &params, Completion done) {
    const auto id = params.value("id").toString();
    if (!m_entries.contains(id)) {
        done(failure("Extension not found"));
        return;
    }
    if (m_entries[id].manifest.isTheme() && m_appearance)
        if (auto forgotten = m_appearance->forgetThemePackage(id); !forgotten) {
            done(forgotten.error());
            return;
        }
    disable(id);
    const bool clearData = params.value("clearData").toBool();
    const auto finish = [this, id, clearData, done](Result<void> cleared) {
        if (!cleared) {
            done(cleared.error());
            return;
        }
        m_entries.remove(id);
        m_order.removeAll(id);
        m_log.remove(id);
        if (auto saved = save(); !saved) {
            done(saved.error());
            return;
        }
        if (clearData)
            QDir(m_packages.dataDirectory(id)).removeRecursively();
        notifyChanged();
        done(snapshot());
    };
    if (clearData)
        m_vault.clear(id, finish);
    else
        finish({});
}
void PluginManager::cleanUnusedPackages() {
    // References on disk also protect the last committed version if saving a mutation failed.
    const auto stored = m_registry.storedDirectories();
    if (!stored)
        return;
    QStringList references = m_developmentDirectories + *stored;
    for (const auto &directory : std::as_const(m_retainedDirectories))
        references.append(directory);
    for (const auto &run : std::as_const(m_runs))
        references.append(run->directory);
    for (const auto &entry : std::as_const(m_entries))
        references << entry.directory << (entry.previous ? entry.previous->directory : QString());
    for (const auto &message : m_packages.clean(references))
        reportError(message);
}
QVariantMap PluginManager::readConfig(const QString &id) const {
    return m_storage.readConfig(id, m_entries.value(id).manifest.defaultConfig);
}
Result<QVariantMap> PluginManager::setConfig(const QString &id, const QVariant &value) {
    if (!m_entries.contains(id) || value.typeId() != QMetaType::QVariantMap ||
        QJsonDocument::fromVariant(value).toJson(QJsonDocument::Compact).size() > 256 * 1024)
        return failure("Invalid extension configuration");
    const auto config = value.toMap();
    if (auto saved = m_storage.writeConfig(id, config); !saved)
        return saved.error();
    if (const auto run = m_runs.value(id); run && run->context) {
        run->context->setConfig(config);
        m_bus.deliver(id, {{"event", "extension.config_changed"}, {"config", config}});
    }
    return QVariantMap{{"config", config}};
}

void PluginManager::publish(const QVariantMap &event) {
    if (!m_closing)
        m_bus.broadcast(event);
}
void PluginManager::watchDevelopment(const std::shared_ptr<Run> &run) {
    run->watcher = std::make_unique<DevWatcher>(run->directory);
    const auto id = run->id;
    connect(run->watcher.get(), &DevWatcher::changed, this, [this, id] {
        enqueue(
            [this, id](Completion done) {
                reload(id, [done](Result<void> result) {
                    done(result ? Result<QVariantMap>(QVariantMap{}) : Result<QVariantMap>(result.error()));
                });
            },
            [this, id](Result<QVariantMap> result) {
                if (!result)
                    recordLog(id, "error", result.error().message);
            });
    });
}

void PluginManager::request(const QString &method, const QVariantMap &params, Completion done) {
    if (!m_catalogueReady || m_closing) {
        done(failure("Extension runtime is not available"));
        return;
    }
    const auto id = params.value("id").toString();
    if (method == "extensions.list")
        done(snapshot());
    else if (method == "extensions.logs") {
        done(QVariantMap{{"logs", m_log.entries(id)}});
    } else if (method == "extensions.get_config") {
        if (!m_entries.contains(id))
            done(failure("Extension not found"));
        else
            done(QVariantMap{{"config", readConfig(id)}});
    } else if (method == "extensions.call")
        m_bus.invoke(id, params.value("service").toString(), params.value("params").toMap(), std::move(done));
    else
        enqueue(
            [this, method, params, id](Completion finish) {
                const auto reply = [this, finish](Result<void> result) {
                    if (result)
                        finish(snapshot());
                    else
                        finish(result.error());
                };
                if (method == "extensions.install")
                    install(params, finish);
                else if (method == "extensions.uninstall")
                    uninstall(params, finish);
                else if (method == "extensions.set_config")
                    finish(setConfig(id, params.value("config")));
                else if (!m_entries.contains(id))
                    finish(failure(method == "extensions.enable" || method == "extensions.disable" ||
                                           method == "extensions.reload"
                                       ? "Extension not found"
                                       : "Unknown extension operation: " + method));
                else if (method == "extensions.enable")
                    enable(id, params.value("trusted").toBool(), [this, reply](Result<void> result) {
                        // Enabling a dependency may unblock extensions that were waiting for it.
                        QStringList blocked;
                        for (const auto &other : std::as_const(m_order))
                            if (m_entries[other].enabled && m_entries[other].state == ExtensionState::Blocked)
                                blocked.append(other);
                        sequence(
                            blocked, [this](const QString &other, Done next) { enable(other, false, next); }, false,
                            [reply, result](Result<void>) { reply(result); });
                    });
                else if (method == "extensions.disable") {
                    disable(id);
                    reply(save());
                } else if (method == "extensions.reload")
                    reload(id, reply);
                else
                    finish(failure("Unknown extension operation: " + method));
            },
            std::move(done));
}
} // namespace nekotune
