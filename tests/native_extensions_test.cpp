#include "app_paths.h"
#include "application/playback/shuffle_strategy_service.h"
#include "application/playback/playback_order_service.h"
#include "infrastructure/extensions/runtime/crash_guard.h"
#include "infrastructure/extensions/runtime/call_guard.h"
#include "infrastructure/extensions/runtime/dependency_graph.h"
#include "infrastructure/extensions/runtime/extension_state.h"
#include "infrastructure/extensions/runtime/session_marker.h"
#include "infrastructure/extensions/package/extension_manifest.h"
#include "infrastructure/extensions/package/extension_registry.h"
#include "infrastructure/extensions/package/package_store.h"
#include "infrastructure/extensions/plugin_manager.h"
#include "infrastructure/appearance/appearance_service.h"
#include "infrastructure/extensions/package/semver.h"
#include "infrastructure/extensions/package/theme_package.h"
#include "infrastructure/extensions/package/zip_archive.h"
#include "themes/theme_resolution.h"
#include <QDirIterator>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <algorithm>
#include <memory>
#include <optional>
#include <thread>
#ifdef Q_OS_UNIX
#include <csignal>
#include <sys/wait.h>
#include <unistd.h>
#endif

using namespace nekotune;

namespace {
const QString examples = QStringLiteral(NEKOTUNE_EXAMPLE_EXTENSIONS);
const QString faultyDirectory = QStringLiteral(NEKOTUNE_TEST_EXTENSIONS "/test.faulty");
const QString dependentDirectory = QStringLiteral(NEKOTUNE_TEST_SOURCES "/dependent");

template <class Backend> Result<QVariantMap> call(Backend &manager, const QString &method, const QVariantMap &params = {},
                         int timeout = 10'000) {
    std::optional<Result<QVariantMap>> result;
    manager.request(method, params, [&](Result<QVariantMap> value) { result = std::move(value); });
    QElapsedTimer timer;
    timer.start();
    while (!result && timer.elapsed() < timeout)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    if (!result)
        return failure("Test request timed out: " + method);
    return *result;
}
template <class T, class Start> Result<T> wait(Start start, int timeout = 10'000) {
    std::optional<Result<T>> result;
    start([&](Result<T> value) { result = std::move(value); });
    QElapsedTimer timer;
    timer.start();
    while (!result && timer.elapsed() < timeout)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    return result ? *result : Result<T>(failure("Test request timed out"));
}
QVariantMap entry(const PluginManager &manager, const QString &id) {
    for (const auto &value : manager.snapshot().value("extensions").toList())
        if (value.toMap().value("id") == id)
            return value.toMap();
    return {};
}
QVariantList contributions(const PluginManager &manager, const QString &kind) {
    return manager.snapshot().value("contributions").toMap().value(kind).toList();
}
QVariantList forExtension(const QVariantList &items, const QString &id) {
    QVariantList result;
    for (const auto &item : items)
        if (item.toMap().value("extensionId") == id)
            result.append(item);
    return result;
}
AppearanceService &appearance(PluginManager &manager) {
    return *manager.findChild<AppearanceService *>();
}
Result<QVariantMap> callAppearance(AppearanceService &service, const QString &method, const QVariantMap &params = {}) {
    return call(service, method, params);
}
QString errorOf(const Result<QVariantMap> &result) { return result ? QString() : result.error().message; }
} // namespace

class NativeExtensionsTest final : public QObject {
    Q_OBJECT
    std::unique_ptr<QTemporaryDir> m_home;
    QList<QPair<QString, QVariantMap>> m_hostCalls;

    std::unique_ptr<AppearanceService> startAppearance(PluginManager &manager) {
        auto service = std::make_unique<AppearanceService>(manager.contributions(),
            QDir(AppPaths::extensionsDirectory()).filePath("appearance.json"));
        manager.setAppearance(service.get());
        return service;
    }
    std::unique_ptr<PluginManager> startManager() {
        auto manager = std::make_unique<PluginManager>();
        auto service = startAppearance(*manager);
        service->setParent(manager.get());
        service.release();
        manager->start([this](const QString &method, const QVariantMap &params, IExtensionBackend::Completion done) {
            m_hostCalls.append({method, params});
            done(QVariantMap{});
        });
        [&] { QTRY_VERIFY(manager->snapshot().value("runtime").toMap().value("startupComplete").toBool()); }();
        return manager;
    }
    void install(PluginManager &manager, const QString &directory) {
        const auto installed = call(manager, "extensions.install", {{"path", directory}, {"development", true}});
        QVERIFY2(installed, qPrintable(errorOf(installed)));
    }
    void enable(PluginManager &manager, const QString &id) {
        const auto enabled = call(manager, "extensions.enable", {{"id", id}, {"trusted", true}});
        QVERIFY2(enabled, qPrintable(errorOf(enabled)));
        QCOMPARE(entry(manager, id).value("state").toString(), "running");
    }
    QString runtimeDirectory() const { return QDir(AppPaths::extensionsDirectory()).filePath("runtime"); }

  private slots:
    void init() {
        m_home = std::make_unique<QTemporaryDir>();
        QVERIFY(m_home->isValid());
        qputenv("NEKOTUNE_HOME", m_home->path().toUtf8());
        qunsetenv("NEKOTUNE_SAFE_MODE");
        m_hostCalls.clear();
    }

    void semverRanges() {
        QVERIFY(semver::satisfies("1.2.0", "^1.1.0"));
        QVERIFY(!semver::satisfies("2.0.0", "^1.1.0"));
        QVERIFY(!semver::satisfies("1.2.3-beta.1", "^1.2.0"));
        QVERIFY(semver::satisfies("1.2.3-beta.2", ">=1.2.3-beta.1"));
        QVERIFY(semver::satisfies("0.2.5", "^0.2.1"));
        QVERIFY(!semver::satisfies("0.3.0", "^0.2.1"));
        QVERIFY(semver::satisfies("1.2.9", "~1.2"));
        QVERIFY(!semver::satisfies("1.3.0", "~1.2"));
        QVERIFY(semver::satisfies("1.9.0", ">=1.0.0 <2.0.0"));
        QVERIFY(semver::satisfies("1.4.0", "1.x"));
        QVERIFY(semver::satisfies("3.0.0", "*"));
        QVERIFY(semver::satisfies("1.2.9", "1.0.0 - 1.2"));
        QVERIFY(!semver::satisfies("1.3.0", "1.0.0 - 1.2"));
        QVERIFY(semver::satisfies("3.1.0", "^1.0.0 || ^3.0.0"));
        QVERIFY(semver::satisfies("1.0.0", "1.0.0"));
        QVERIFY(!semver::validRange("^banana"));
        QVERIFY(!semver::parse("1.2"));
        QVERIFY(!semver::parse("01.2.3"));
    }

    void dependencyGraph() {
        const DependencyNodes nodes{{"a", {"1.0.0", {}}},
                                    {"b", {"1.2.0", {{"a", "^1.0.0"}}}},
                                    {"c", {"1.0.0", {{"b", "^1.0.0"}}}},
                                    {"d", {"1.0.0", {{"a", "^2.0.0"}}}},
                                    {"x", {"1.0.0", {{"y", "*"}}}},
                                    {"y", {"1.0.0", {{"x", "*"}}}}};
        QCOMPARE(dependencyOrder("c", nodes).value(), (QStringList{"a", "b", "c"}));
        QVERIFY(dependencyOrder("d", nodes).error().message.contains("must satisfy ^2.0.0"));
        QVERIFY(dependencyOrder("x", nodes).error().message.contains("Dependency cycle"));
        QVERIFY(dependencyOrder("missing", nodes).error().message.contains("Missing extension"));
        QCOMPARE(dependentsOf("a", {"a", "b", "c", "d"}, nodes), (QStringList{"a", "b", "c", "d"}));
    }
    void extensionStateStrings() {
        QCOMPARE(toString(ExtensionState::Disabled), "disabled");
        QCOMPARE(toString(ExtensionState::Blocked), "blocked");
        QCOMPARE(toString(ExtensionState::Starting), "starting");
        QCOMPARE(toString(ExtensionState::Running), "running");
        QCOMPARE(toString(ExtensionState::Stopping), "stopping");
        QCOMPARE(toString(ExtensionState::Failed), "failed");
    }
    void pendingCallsCompleteOnce() {
        QObject owner;
        PendingCalls calls;
        int completed = 0;
        QString error;
        auto reply = calls.add(&owner, [&](Result<QVariantMap> result) {
            ++completed;
            error = result ? QString() : result.error().message;
        }, 50);
        reply(QVariantMap{});
        reply(QVariantMap{});
        QCOMPARE(completed, 1);
        QVERIFY(error.isEmpty());

        auto timed = calls.add(&owner, [&](Result<QVariantMap> result) { error = result.error().message; }, 20);
        QTRY_COMPARE(error, "Extension request timed out");
        timed(QVariantMap{});

        error.clear();
        auto stopped = calls.add(&owner, [&](Result<QVariantMap> result) { error = result.error().message; }, 10'000);
        calls.failAll("Extension stopped");
        QCOMPARE(error, "Extension stopped");
    }

    void registryKeepsUnknownKeys() {
        const auto path = QDir(m_home->path()).filePath("registry.json");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(R"({"bundledSeen":["a"],"selections":{"theme":"x/y"},"developmentDirectories":["/dev/x"],)"
                   R"("entries":[{"id":"x.one","directory":"/p/one","enabled":true,"trusted":true,)"
                   R"("previous":{"directory":"/p/old","development":false,"enabled":false,"trusted":true}},)"
                   R"({"id":"x.one","directory":"/p/duplicate"},{"id":"Bad","directory":"/p/bad"},)"
                   R"({"id":"x.two","directory":"/p/two","failure":"boom"}]})");
        file.close();
        const ExtensionRegistry registry(path);
        auto state = registry.load();
        QCOMPARE(state.entries.size(), 2);
        QCOMPARE(state.entries[0].directory, "/p/one");
        QVERIFY(state.entries[0].previous);
        QCOMPARE(state.entries[0].previous->directory, "/p/old");
        QCOMPARE(state.entries[1].failure, "boom");
        QCOMPARE(state.extra.value("bundledSeen").toStringList(), QStringList{"a"});
        QVERIFY(registry.save(state));
        const auto reloaded = registry.load();
        QCOMPARE(reloaded.extra, state.extra);
        QCOMPARE(state.selections.value("theme").toString(), "x/y");
        QVERIFY(reloaded.selections.isEmpty());
        QCOMPARE(reloaded.entries.size(), 2);
        QCOMPARE(*registry.storedDirectories(),
                 (QStringList{"/dev/x", "/p/one", "/p/old", "/p/two", QString()}));
    }
    void packageStoreCleansOnlyUnreferenced() {
        const PackageStore store;
        const QDir packages(store.packagesDirectory());
        const QString kept = "x.one-1.0.0-123e4567-e89b-42d3-a456-426614174000";
        const QString unused = "x.one-0.9.0-123e4567-e89b-42d3-a456-426614174001";
        const QString staging = ".staging-123e4567-e89b-42d3-a456-426614174002";
        for (const auto &name : {kept, unused, staging, QStringLiteral("not-a-package")})
            QVERIFY(packages.mkpath(name));
        QVERIFY(store.clean({packages.filePath(kept)}).isEmpty());
        QVERIFY(packages.exists(kept));
        QVERIFY(packages.exists("not-a-package"));
        QVERIFY(!packages.exists(unused));
        QVERIFY(!packages.exists(staging));
    }

    void manifestValidation() {
        QTemporaryDir directory;
        const auto write = [&](const QByteArray &json) {
            QFile file(directory.filePath("extension.json"));
            QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
            file.write(json);
        };
        write(R"({"id":"x.node","name":"Old","version":"1.0.0","apiVersion":1,"main":"main.mjs"})");
        auto manifest = readManifest(directory.path());
        QVERIFY(!manifest);
        QVERIFY(manifest.error().message.contains("apiVersion 3"));

        write(R"({"id":"x.qt","name":"Qt","version":"1.0.0","apiVersion":3,"qt":"5.15",)"
              R"("library":{")" + extensionPlatform().toUtf8() + R"(":"lib/x.so"}})");
        manifest = readManifest(directory.path());
        QVERIFY(manifest);
        QVERIFY(manifest.value().unavailable.contains("Qt 5.15"));

        write(R"({"id":"x.old","name":"Old native","version":"1.0.0","apiVersion":2,"library":{}})");
        manifest = readManifest(directory.path());
        QVERIFY(!manifest);
        QVERIFY(manifest.error().message.contains("Native libraries require apiVersion 3"));
        for (const int version : {1, 2, 3}) {
            write(QByteArray(R"({"id":"x.ui","name":"UI","version":"1.0.0","apiVersion":)") +
                  QByteArray::number(version) + R"(,"contributes":{}})");
            QVERIFY(readManifest(directory.path()));
        }

        write(R"({"id":"x.ui","name":"UI","version":"1.0.0","apiVersion":2,)"
              R"("contributes":{"pages":[{"id":"p","source":"../escape.qml"}]}})");
        QVERIFY(!readManifest(directory.path()));

        write(R"({"id":"x.typed","name":"Typed","version":"1.2.3","apiVersion":2,"description":"D",)"
              R"("nativeModules":true,"config":{"a":1},"dependencies":{"x.base":"^1.0.0"},)"
              R"("contributes":{"menus":[{"id":"menu","title":"M","context":"song","command":"c"}]}})");
        manifest = readManifest(directory.path());
        QVERIFY2(manifest, qPrintable(manifest ? QString() : manifest.error().message));
        QCOMPARE(manifest.value().id, "x.typed");
        QCOMPARE(manifest.value().description, "D");
        QCOMPARE(manifest.value().type, "extension");
        QVERIFY(manifest.value().nativeModules);
        QCOMPARE(manifest.value().defaultConfig.value("a").toInt(), 1);
        QCOMPARE(manifest.value().dependencies.value("x.base"), "^1.0.0");
        QCOMPARE(manifest.value().contributes.value("menus").toList().size(), 1);
        QCOMPARE(manifest.value().raw.value("name").toString(), "Typed");
    }

    void trustIsRequiredBeforeLoading() {
        auto manager = startManager();
        install(*manager, examples + "/example.custom-ui");
        QCOMPARE(entry(*manager, "example.custom-ui").value("state").toString(), "disabled");
        const auto refused = call(*manager, "extensions.enable", {{"id", "example.custom-ui"}});
        QVERIFY(!refused);
        QVERIFY(refused.error().message.contains("Trust"));
        // Development libraries are staged right before loading; nothing was staged, so nothing ran.
        QVERIFY(!QFileInfo::exists(runtimeDirectory()));

        enable(*manager, "example.custom-ui");
        // The whole library directory is staged so sibling dependencies keep resolving.
        const auto staged = QDir(runtimeDirectory()).entryInfoList({"example.custom-ui-*"}, QDir::Dirs);
        QCOMPARE(staged.size(), 1);
        QVERIFY(!QDir(staged.first().filePath()).entryList(QDir::Files).isEmpty());
        const auto extension = entry(*manager, "example.custom-ui");
        const auto page = forExtension(contributions(*manager, "pages"), "example.custom-ui").value(0).toMap();
        QCOMPARE(page.value("id").toString(), "example.custom-ui/hello");
        QVERIFY(QDir::isAbsolutePath(page.value("source").toString()));
        QCOMPARE(page.value("group").toString(), "primary");
        QCOMPARE(page.value("generation"), extension.value("generation"));
        QVERIFY(!extension.contains("contributes"));
        QVERIFY(!extension.contains("registrations"));
        QVERIFY(!extension.contains("themes"));
        const auto commands = forExtension(manager->snapshot().value("commands").toList(), "example.custom-ui");
        QCOMPARE(commands.value(0).toMap().value("id").toString(), "example.custom-ui/inspect-song");
        QCOMPARE(commands.value(0).toMap().value("title").toString(), "Inspect selection");

        const auto hello = call(*manager, "extensions.call", {{"id", "example.custom-ui"}, {"service", "hello"}});
        QVERIFY2(hello, qPrintable(errorOf(hello)));
        QVERIFY(hello.value().value("message").toString().contains("native"));

        QSignalSpy events(manager.get(), &IExtensionBackend::eventReady);
        const auto inspected = call(*manager, "extensions.call",
                                    {{"id", "example.custom-ui"}, {"service", "example.custom-ui/inspect-song"},
                                     {"params", QVariantMap{{"song_id", 7}}}});
        QCOMPARE(inspected.value().value("song_id").toInt(), 7);
        bool published = false;
        for (const auto &arguments : events) {
            const auto event = arguments.value(0).toMap();
            published = published || (event.value("event") == "extension.example.custom-ui.selection" &&
                                       event.value("data").toMap().value("song_id") == 7);
        }
        QVERIFY(published);

        QVERIFY(call(*manager, "extensions.disable", {{"id", "example.custom-ui"}}));
        QVERIFY(forExtension(contributions(*manager, "pages"), "example.custom-ui").isEmpty());
        QVERIFY(!call(*manager, "extensions.call", {{"id", "example.custom-ui"}, {"service", "hello"}}));
    }

    void shuffleRegistrationsAndGenerations() {
        auto manager = startManager();
        install(*manager, QStringLiteral(NEKOTUNE_TEST_EXTENSIONS "/test.shuffle"));
        enable(*manager, "test.shuffle");
        auto catalogue = manager->shuffle().strategies();
        QCOMPARE(catalogue.size(), 1);
        QCOMPARE(catalogue.first().id, "test.shuffle/test");
        const auto generation = catalogue.first().generation;
        const auto validation = call(*manager, "extensions.call", {{"id", "test.shuffle"}, {"service", "validate"}});
        QVERIFY(validation);
        for (const auto &value : validation.value()) QVERIFY(!value.toBool());
        sdk::ShuffleRequest request{{{11, 7, "Title", "Artist"}, {12, 7, "Duplicate", "Artist"}}, 11, 42, {}};
        const auto first = manager->shuffle().select("test.shuffle/test", generation, request);
        QVERIFY(first);
        QCOMPARE(first.value().queueId, 11);
        QCOMPARE(first.value().state.value("step").toInt(), 1);
        QCOMPARE(manager->shuffle().select("test.shuffle/test", generation, request).value().queueId, 11);
        request.state = first.value().state;
        QCOMPARE(manager->shuffle().select("test.shuffle/test", generation, request).value().queueId, 12);
        QVERIFY(call(*manager, "extensions.call", {{"id", "test.shuffle"}, {"service", "release"}}));
        QVERIFY(manager->shuffle().strategies().isEmpty());
        QVERIFY(!manager->shuffle().select("test.shuffle/test", generation, request));
        QVERIFY(call(*manager, "extensions.call", {{"id", "test.shuffle"}, {"service", "register"}}));
        catalogue = manager->shuffle().strategies();
        QCOMPARE(catalogue.size(), 1);
        QVERIFY(catalogue.first().generation > generation);
        QVERIFY(!manager->shuffle().select("test.shuffle/test", generation, request));
        const auto beforeReload = catalogue.first().generation;
        QVERIFY(call(*manager, "extensions.reload", {{"id", "test.shuffle"}}));
        QCOMPARE(manager->shuffle().strategies().size(), 1);
        QVERIFY(manager->shuffle().strategies().first().generation > beforeReload);
        QVERIFY(!manager->shuffle().select("test.shuffle/test", beforeReload, request));
        QVERIFY(call(*manager, "extensions.disable", {{"id", "test.shuffle"}}));
        QVERIFY(manager->shuffle().strategies().isEmpty());
    }
    void sdk2BinariesAreRejected() {
        auto manager = startManager();
        install(*manager, QStringLiteral(NEKOTUNE_TEST_EXTENSIONS "/test.legacy"));
        const auto refused = call(*manager, "extensions.enable", {{"id", "test.legacy"}, {"trusted", true}});
        QVERIFY(!refused);
        QVERIFY(refused.error().message.contains("Unsupported extension interface org.nekotune.IExtensionPlugin/2"));
        QCOMPARE(entry(*manager, "test.legacy").value("state").toString(), "failed");
    }

    void shuffleFailuresFallback_data() {
        QTest::addColumn<QString>("mode");
        for (const auto &mode : {"error", "throw", "invalid", "zero"}) QTest::newRow(mode) << QString(mode);
    }
    void shuffleFailuresFallback() {
        QFETCH(QString, mode);
        auto manager = startManager();
        install(*manager, QStringLiteral(NEKOTUNE_TEST_EXTENSIONS "/test.shuffle"));
        QVERIFY(call(*manager, "extensions.set_config", {{"id", "test.shuffle"}, {"config", QVariantMap{{"mode", mode}}}}));
        enable(*manager, "test.shuffle");
        ShuffleStrategyService service("test.shuffle/test");
        auto sync = [&] {
            service.setCatalogue(manager->shuffle().strategies(),
                [&](const QString &id, quint64 generation, const sdk::ShuffleRequest &request) {
                    return manager->shuffle().select(id, generation, request);
                });
        };
        connect(&manager->shuffle(), &ShuffleStrategies::catalogueChanged, &service, sync);
        sync();
        QCOMPARE(service.effective(), "test.shuffle/test");
        const auto generation = manager->shuffle().strategies().first().generation;
        PlaybackOrderService order(service.createStrategy(), PlaybackMode::Shuffle);
        PlayerQueue queue;
        queue.add("/first.wav", {1, "hash"}); queue.add("/second.wav", {1, "hash"});
        queue.setCurrentIndex(0); order.reset(queue);
        QCOMPARE(order.propose(queue, PlaybackAdvance::Next).queueId, 2); // Immediate safe fallback.
        QVERIFY(manager->shuffle().strategies().isEmpty());
        QTRY_COMPARE(service.effective(), "shuffle_bag");
        QCOMPARE(service.preferred(), "test.shuffle/test");
        QCOMPARE(entry(*manager, "test.shuffle").value("state").toString(), "running");
        const auto logs = call(*manager, "extensions.logs", {{"id", "test.shuffle"}});
        QVERIFY(logs);
        bool logged = false;
        for (const auto &value : logs.value().value("logs").toList())
            logged = logged || value.toMap().value("message").toString().contains("Shuffle strategy");
        QVERIFY(logged);
        QVERIFY(call(*manager, "extensions.call", {{"id", "test.shuffle"}, {"service", "register"}}));
        QCOMPARE(service.effective(), "test.shuffle/test");
        QVERIFY(manager->shuffle().strategies().first().generation > generation);
    }

    void artistShuffleExample() {
        auto manager = startManager();
        install(*manager, examples + "/example.shuffle");
        enable(*manager, "example.shuffle");
        const auto generation = manager->shuffle().strategies().first().generation;
        sdk::ShuffleRequest request{{{11, 1, "One", "Artist A"}, {12, 2, "Two", "artist a"},
                                     {13, 3, "Three", "Artist B"}}, 11, 42, {}};
        for (quint32 seed = 0; seed < 20; ++seed) {
            request.seed = seed;
            const auto chosen = manager->shuffle().select("example.shuffle/artist", generation, request);
            QVERIFY(chosen); QCOMPARE(chosen.value().queueId, 13);
        }
        request.queue.removeLast();
        QCOMPARE(manager->shuffle().select("example.shuffle/artist", generation, request).value().queueId, 12);
        request.queue.removeLast();
        QCOMPARE(manager->shuffle().select("example.shuffle/artist", generation, request).value().queueId, 11);
    }

    void lyricsAndAutomationExamples() {
        auto manager = startManager();
        install(*manager, examples + "/example.lyrics");
        install(*manager, examples + "/example.automation");
        enable(*manager, "example.lyrics");
        enable(*manager, "example.automation");

        const auto lyrics = wait<QVariantMap>([&](auto done) {
            manager->lyricsSources().search("example.lyrics/demo", {{"title", "Song"}}, done);
        });
        QVERIFY2(lyrics, qPrintable(errorOf(lyrics)));
        const auto candidate = lyrics.value().value("candidates").toList().value(0).toMap();
        QCOMPARE(candidate.value("title").toString(), "Song");
        QVERIFY(candidate.value("synced_lyrics").toString().contains("NekoTune"));

        const auto timer = call(*manager, "extensions.call", {{"id", "example.automation"}, {"service", "sleep-timer"}});
        QCOMPARE(timer.value().value("seconds").toInt(), 30);
        manager->publish({{"event", "player.track_changed"}, {"song", QVariantMap{{"title", "Neko"}}}});
        QTRY_VERIFY([&] {
            for (const auto &log : call(*manager, "extensions.logs", {{"id", "example.automation"}}).value().value("logs").toList())
                if (log.toMap().value("message").toString() == "Now playing: Neko")
                    return true;
            return false;
        }());
        QVERIFY(call(*manager, "extensions.set_config", {{"id", "example.automation"}, {"config", QVariantMap{{"sleepSeconds", 2}}}}));
        QCOMPARE(call(*manager, "extensions.call", {{"id", "example.automation"}, {"service", "sleep-timer"}})
                     .value()
                     .value("seconds")
                     .toInt(),
                 2);
        QTRY_VERIFY_WITH_TIMEOUT(!m_hostCalls.isEmpty() && m_hostCalls.last().first == "player.pause", 5000);
    }

    void activationFailuresAreContained() {
        auto manager = startManager();
        install(*manager, faultyDirectory);
        for (const auto &[mode, message] : {std::pair{"fail", "activation refused"}, {"throw", "activation exploded"}}) {
            QVERIFY(call(*manager, "extensions.set_config", {{"id", "test.faulty"}, {"config", QVariantMap{{"mode", mode}}}}));
            const auto enabled = call(*manager, "extensions.enable", {{"id", "test.faulty"}, {"trusted", true}});
            QVERIFY(!enabled);
            QVERIFY2(enabled.error().message.contains(message), qPrintable(enabled.error().message));
            const auto extension = entry(*manager, "test.faulty");
            QCOMPARE(extension.value("state").toString(), "failed");
            QVERIFY(!extension.value("enabled").toBool());
        }
        QVERIFY(call(*manager, "extensions.set_config", {{"id", "test.faulty"}, {"config", QVariantMap{{"mode", "ok"}}}}));
        enable(*manager, "test.faulty");
        const int first = call(*manager, "extensions.call", {{"id", "test.faulty"}, {"service", "state"}})
                              .value()
                              .value("activations")
                              .toInt();
        QVERIFY(call(*manager, "extensions.reload", {{"id", "test.faulty"}}));
        QCOMPARE(entry(*manager, "test.faulty").value("state").toString(), "running");
        // The library stays mapped and its instance is reused for the next activation.
        QCOMPARE(call(*manager, "extensions.call", {{"id", "test.faulty"}, {"service", "state"}})
                     .value()
                     .value("activations")
                     .toInt(),
                 first + 1);

        const auto thrown = call(*manager, "extensions.call", {{"id", "test.faulty"}, {"service", "throw"}});
        QVERIFY(!thrown);
        QTRY_COMPARE(entry(*manager, "test.faulty").value("state").toString(), "failed");
        QVERIFY(entry(*manager, "test.faulty").value("error").toString().contains("service exploded"));
    }

    void serviceBusDeliversInOrder() {
        auto manager = startManager();
        install(*manager, faultyDirectory);
        enable(*manager, "test.faulty");
        QSignalSpy events(manager.get(), &IExtensionBackend::eventReady);
        for (const auto &value : QStringList{"a", "b", "c"})
            manager->publish({{"event", "test.host"}, {"value", value}});
        QTRY_VERIFY([&] {
            for (const auto &arguments : events) {
                const auto event = arguments.value(0).toMap();
                if (event.value("event") == "extension.test.faulty.seen" &&
                    event.value("data").toMap().value("values").toStringList() == QStringList{"a", "b", "c"})
                    return true;
            }
            return false;
        }());
    }

    void servicesStorageEventsAndSources() {
        auto manager = startManager();
        install(*manager, faultyDirectory);
        enable(*manager, "test.faulty");
        const auto service = [&](const QString &name, const QVariantMap &params = {}) {
            return call(*manager, "extensions.call", {{"id", "test.faulty"}, {"service", name}, {"params", params}});
        };
        QCOMPARE(service("thread", {{"value", "cross"}}).value().value("echo").toString(), "cross");

        const auto stored = service("storage", {{"value", QVariantList{1, "two"}}});
        QVERIFY2(stored, qPrintable(errorOf(stored)));
        QCOMPARE(stored.value().value("value").toList(), (QVariantList{1, "two"}));
        QFile storage(QDir(AppPaths::extensionsDirectory()).filePath("data/test.faulty/storage.json"));
        QVERIFY(storage.open(QIODevice::ReadOnly));
        QCOMPARE(QJsonDocument::fromJson(storage.readAll()).toVariant().toMap().value("value").toList().size(), 2);

        QVERIFY(!service("ui", {{"id", "escape"}, {"source", "../../escape.qml"}}).value().value("accepted").toBool());
        QVERIFY(service("ui", {{"id", "runtime"}, {"source", "extension.json"}}).value().value("accepted").toBool());
        QTRY_COMPARE(forExtension(contributions(*manager, "pages"), "test.faulty").size(), 1);

        QSignalSpy events(manager.get(), &IExtensionBackend::eventReady);
        manager->publish({{"event", "test.host"}, {"value", "a"}});
        manager->publish({{"event", "test.other"}, {"value", "b"}});
        QTRY_VERIFY([&] {
            for (const auto &arguments : events) {
                const auto event = arguments.value(0).toMap();
                if (event.value("event") == "extension.test.faulty.seen")
                    return event.value("data").toMap().value("values").toStringList() == QStringList{"a"};
            }
            return false;
        }());

        const auto search = wait<sdk::SearchPage>([&](auto done) {
            manager->musicSources().search("test.faulty/tones", "A", {}, done);
        });
        QVERIFY2(search, qPrintable(search ? QString() : search.error().message));
        QCOMPARE(search.value().tracks.value(0).title, "Tone A");
        QCOMPARE(search.value().tracks.value(0).durationMs, 8000);
        QCOMPARE(search.value().cursor, "next");
        const auto audio = wait<IMusicSourceCatalog::Playback>([&](auto done) {
            manager->musicSources().resolvePlayback("test.faulty/tones", "one", done);
        });
        QVERIFY2(audio, qPrintable(audio ? QString() : audio.error().message));
        QVERIFY(audio.value().url.toLocalFile().endsWith("/data/test.faulty/tone.wav"));
        QVERIFY(!wait<IMusicSourceCatalog::Playback>([&](auto done) {
            manager->musicSources().resolvePlayback("test.faulty/tones", "outside", done);
        }));
        const auto music = forExtension(manager->snapshot().value("sources").toMap().value("music").toList(), "test.faulty");
        QCOMPARE(music.value(0).toMap().value("name").toString(), "Tones");
    }

    void hostCallGateway_data() {
        QTest::addColumn<QString>("method");
        QTest::addColumn<bool>("allowed");
        QTest::newRow("player") << "player.pause" << true;
        QTest::newRow("music") << "music.enqueue" << true;
        QTest::newRow("service") << "extensions.call" << true;
        QTest::newRow("list") << "extensions.list" << true;
        QTest::newRow("library-read") << "library.list" << true;
        QTest::newRow("ai-chat") << "ai.chat" << true;
        QTest::newRow("enable") << "extensions.enable" << false;
        QTest::newRow("install") << "extensions.install" << false;
        QTest::newRow("disable") << "extensions.disable" << false;
        QTest::newRow("reload") << "extensions.reload" << false;
        QTest::newRow("uninstall") << "extensions.uninstall" << false;
        QTest::newRow("config") << "extensions.set_config" << false;
        QTest::newRow("select") << "appearance.select" << false;
        QTest::newRow("export-theme") << "appearance.export_theme" << false;
        QTest::newRow("unknown-extension") << "extensions.future_method" << false;
        QTest::newRow("appearance") << "appearance.select" << false;
        QTest::newRow("appearance-read") << "appearance.get" << false;
        QTest::newRow("unknown-appearance") << "appearance.future_method" << false;
        QTest::newRow("restore") << "library.restore" << false;
        QTest::newRow("restore-preview") << "library.restore_preview" << false;
        QTest::newRow("remove-rollback") << "library.remove_rollback" << false;
        QTest::newRow("delete") << "library.delete" << false;
        QTest::newRow("migrate") << "library.migrate" << false;
        QTest::newRow("history") << "history.clear" << false;
        QTest::newRow("ai") << "ai.config.get" << false;
        QTest::newRow("ai-set") << "ai.config.set" << false;
        QTest::newRow("ai-clear-key") << "ai.config.clear_key" << false;
    }
    void hostCallGateway() {
        QFETCH(QString, method);
        QFETCH(bool, allowed);
        auto manager = startManager();
        install(*manager, faultyDirectory);
        enable(*manager, "test.faulty");
        m_hostCalls.clear();
        const QVariantMap params{{"id", "test.faulty"}, {"trusted", true}};
        const auto result = call(*manager, "extensions.call",
                                 {{"id", "test.faulty"},
                                  {"service", "host"},
                                  {"params", QVariantMap{{"method", method}, {"params", params}}}});
        QCOMPARE(bool(result), allowed);
        const bool forwarded = std::any_of(m_hostCalls.cbegin(), m_hostCalls.cend(),
                                           [&](const auto &host) { return host.first == method; });
        QCOMPARE(forwarded, allowed);
        QCOMPARE(m_hostCalls.size(), allowed ? 1 : 0);
        if (allowed) {
            QCOMPARE(m_hostCalls.first().second, params);
            return;
        }
        const auto message = "Host method is not available to extensions: " + method;
        QCOMPARE(result.error().code, ErrorCode::PermissionDenied);
        QCOMPARE(result.error().message, message);
        const auto logs = call(*manager, "extensions.logs", {{"id", "test.faulty"}});
        QVERIFY(logs);
        bool logged = false;
        for (const auto &log : logs.value().value("logs").toList())
            logged = logged || (log.toMap().value("level") == "error" && log.toMap().value("message") == message);
        QVERIFY(logged);
    }

    void dependenciesBlockAndRecover() {
        auto manager = startManager();
        install(*manager, faultyDirectory);
        install(*manager, dependentDirectory);
        const auto refused = call(*manager, "extensions.enable", {{"id", "test.dependent"}, {"trusted", true}});
        QVERIFY(!refused);
        QVERIFY(refused.error().message.contains("test.faulty"));
        enable(*manager, "test.faulty");
        enable(*manager, "test.dependent");
        QVERIFY(call(*manager, "extensions.disable", {{"id", "test.faulty"}}));
        QCOMPARE(entry(*manager, "test.dependent").value("state").toString(), "blocked");
        QVERIFY(call(*manager, "extensions.enable", {{"id", "test.faulty"}}));
        QCOMPARE(entry(*manager, "test.dependent").value("state").toString(), "running");
    }

    void crashSentinelDisablesExtension() {
        {
            auto manager = startManager();
            install(*manager, faultyDirectory);
            install(*manager, examples + "/example.lyrics");
            enable(*manager, "test.faulty");
            enable(*manager, "example.lyrics");
            manager->shutdown();
        }
        QFile sentinel(QDir(AppPaths::extensionsDirectory()).filePath("crash-sentinel"));
        QVERIFY(sentinel.open(QIODevice::WriteOnly));
        sentinel.write("test.faulty");
        sentinel.close();
        auto manager = startManager();
        const auto crashed = entry(*manager, "test.faulty");
        QCOMPARE(crashed.value("state").toString(), "failed");
        QVERIFY(!crashed.value("enabled").toBool());
        QVERIFY(crashed.value("error").toString().contains("crashed"));
        QCOMPARE(entry(*manager, "example.lyrics").value("state").toString(), "running");
        QVERIFY(!QFileInfo::exists(sentinel.fileName()));
    }

    void crashHandlerWritesSentinel() {
#ifndef Q_OS_UNIX
        QSKIP("Crash attribution uses POSIX signals");
#else
        const auto path = m_home->filePath("sentinel");
        crashguard::install(path);
        const pid_t child = ::fork();
        QVERIFY(child >= 0);
        if (child == 0) {
            crashguard::Scope outer("outer.extension");
            {
                crashguard::Scope inner("inner.extension");
            }
            ::raise(SIGSEGV);
            ::_exit(0);
        }
        int status = 0;
        QCOMPARE(::waitpid(child, &status, 0), child);
        // Chained handlers decide how the process ends (sanitizers exit instead of re-raising).
        QVERIFY(!WIFEXITED(status) || WEXITSTATUS(status) != 0);
        QCOMPARE(crashguard::takeSentinel(path), "outer.extension");
        QVERIFY(crashguard::takeSentinel(path).isEmpty());
#endif
    }

    void crashOnExtensionThreadIsAttributed() {
#ifndef Q_OS_UNIX
        QSKIP("Crash attribution uses POSIX signals");
#else
        const auto path = QDir(AppPaths::extensionsDirectory()).filePath("crash-sentinel");
        const pid_t child = ::fork();
        QVERIFY(child >= 0);
        if (child == 0) {
            auto manager = startManager();
            install(*manager, faultyDirectory);
            enable(*manager, "test.faulty");
            call(*manager, "extensions.call", {{"id", "test.faulty"}, {"service", "crash-thread"}});
            ::_exit(1);
        }
        int status = 0;
        QCOMPARE(::waitpid(child, &status, 0), child);
        QVERIFY(WIFSIGNALED(status));
        QCOMPARE(WTERMSIG(status), SIGABRT);
        QCOMPARE(crashguard::takeSentinel(path), "test.faulty");
#endif
    }

    void unattributedThreadDoesNotBorrowAttribution() {
#ifndef Q_OS_UNIX
        QSKIP("Crash attribution uses POSIX signals");
#else
        const auto path = m_home->filePath("sentinel");
        crashguard::install(path);
        const pid_t child = ::fork();
        QVERIFY(child >= 0);
        if (child == 0) {
            std::atomic<bool> ready{false};
            std::thread worker([&] {
                crashguard::Scope scope("another.thread");
                ready.store(true);
                for (;;)
                    QThread::msleep(1000);
            });
            while (!ready.load())
                std::this_thread::yield();
            ::raise(SIGSEGV);
            ::_exit(1);
        }
        int status = 0;
        QCOMPARE(::waitpid(child, &status, 0), child);
        QVERIFY(!WIFEXITED(status) || WEXITSTATUS(status) != 0);
        QVERIFY(crashguard::takeSentinel(path).isEmpty());
#endif
    }

    void watchdogStopsBlockingExtension() {
        auto manager = startManager();
        install(*manager, faultyDirectory);
        enable(*manager, "test.faulty");
        // The call still completes after exceeding the production 30 s hang limit.
        QVERIFY(call(*manager, "extensions.call",
                     {{"id", "test.faulty"}, {"service", "block"},
                      {"params", QVariantMap{{"ms", 31'000}}}}, 40'000));
        QTRY_COMPARE_WITH_TIMEOUT(entry(*manager, "test.faulty").value("state").toString(), "failed", 5'000);
        QVERIFY(entry(*manager, "test.faulty").value("error").toString().contains("blocked the backend"));
        QVERIFY(!QFileInfo::exists(QDir(AppPaths::extensionsDirectory()).filePath("crash-sentinel")));
    }

    void slowCallIsLogged() {
        auto manager = startManager();
        install(*manager, faultyDirectory);
        enable(*manager, "test.faulty");
        QVERIFY(call(*manager, "extensions.call",
                     {{"id", "test.faulty"}, {"service", "block"},
                      {"params", QVariantMap{{"ms", 5'500}}}}));
        const auto logs = call(*manager, "extensions.logs", {{"id", "test.faulty"}});
        QVERIFY(logs);
        bool logged = false;
        for (const auto &value : logs.value().value("logs").toList()) {
            const auto log = value.toMap();
            logged = logged || (log.value("level") == "warn" &&
                                log.value("message").toString().startsWith("Extension call blocked the backend for"));
        }
        QVERIFY(logged);
        QCOMPARE(entry(*manager, "test.faulty").value("state").toString(), "running");
    }

    void slowShuffleCalls_data() {
        QTest::addColumn<int>("delayMs");
        QTest::addColumn<bool>("hang");
        QTest::newRow("warn") << 350 << false;
        QTest::newRow("hang") << 31'000 << true;
    }
    void slowShuffleCalls() {
        QFETCH(int, delayMs);
        QFETCH(bool, hang);
        auto manager = startManager();
        install(*manager, faultyDirectory);
        QVERIFY(call(*manager, "extensions.set_config",
                     {{"id", "test.faulty"}, {"config", QVariantMap{{"shuffleMs", delayMs}}}}));
        enable(*manager, "test.faulty");
        const auto strategies = manager->shuffle().strategies();
        QCOMPARE(strategies.size(), 1);
        sdk::ShuffleRequest request;
        request.queue.append({.queueId = 1});
        const auto selected = manager->shuffle().select(strategies.first().id, strategies.first().generation, request);
        QCOMPARE(bool(selected), !hang);
        QCOMPARE(entry(*manager, "test.faulty").value("state").toString(), hang ? "failed" : "running");
        if (hang) {
            QVERIFY(entry(*manager, "test.faulty").value("error").toString().contains("blocked the backend"));
            QVERIFY(manager->shuffle().strategies().isEmpty());
        }
        const auto logs = call(*manager, "extensions.logs", {{"id", "test.faulty"}});
        QVERIFY(logs);
        bool warned = false;
        for (const auto &value : logs.value().value("logs").toList()) {
            const auto log = value.toMap();
            warned = warned || (log.value("level") == "warn" && log.value("message").toString().startsWith(
                                    "Shuffle strategy test.faulty/slow took"));
        }
        QVERIFY(warned);
        QVERIFY(!QFileInfo::exists(QDir(AppPaths::extensionsDirectory()).filePath("crash-sentinel")));
    }

    void watchdogKeepsOuterAttribution() {
        const auto path = m_home->filePath("sentinel");
        crashguard::install(path);
        QString hungId;
        watchdog::start(QThread::currentThread(), {100, 300}, [&](const QString &id) {
            hungId = id;
            crashguard::writeSentinel(id);
        });
        AttributedOutcome background;
        std::thread other([&] {
            background = runAttributed("background", [] { QThread::msleep(1100); });
        });
        other.join();
        const bool backgroundSentinel = QFileInfo::exists(path);
        AttributedOutcome nested;
        bool sentinelWritten = false;
        const auto outer = runAttributed("outer", [&] {
            nested = runAttributed("inner", [] { QThread::msleep(1100); });
            QFile file(path);
            sentinelWritten = file.open(QIODevice::ReadOnly) && file.readAll() == "outer";
        });
        watchdog::stop();
        watchdog::stop();
        QVERIFY(!background.hung);
        QVERIFY(!backgroundSentinel);
        QVERIFY(!nested.hung);
        QVERIFY(outer.hung);
        QVERIFY(sentinelWritten);
        QCOMPARE(hungId, "outer");
        crashguard::clearSentinel();
        QVERIFY(!QFileInfo::exists(path));
    }

    void sessionMarkerCountsUnattributedExits() {
        const auto path = m_home->filePath("session-active");
        SessionMarker first(path);
        QVERIFY(first.begin(false).ended);
        first.loaded("x.one");
        first.loaded("x.one");
        SessionMarker second(path);
        const auto previous = second.begin(false);
        QVERIFY(!previous.ended);
        QCOMPARE(previous.loaded, QStringList{"x.one"});
        QCOMPARE(previous.unattributed, 1);
        second.loaded("x.one");
        SessionMarker third(path);
        QCOMPARE(third.begin(false).unattributed, 2);
        third.loaded("x.one");
        QCOMPARE(SessionMarker(path).begin(true).unattributed, 0);
        // A session which loaded no extension code breaks the streak, even after an abnormal exit.
        QCOMPARE(SessionMarker(path).begin(false).unattributed, 0);
        SessionMarker clean(path);
        clean.begin(false);
        clean.end();
        QVERIFY(!QFileInfo::exists(path));
        QCOMPARE(SessionMarker(path).begin(false).unattributed, 0);
    }

    void repeatedUnattributedExitsEnterSafeMode() {
        const auto marker = QDir(AppPaths::extensionsDirectory()).filePath("session-active");
        QVERIFY(QDir().mkpath(QFileInfo(marker).absolutePath()));
        QFile file(marker);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(R"({"loaded":["test.faulty"],"unattributed":1})");
        file.close();
        auto manager = startManager();
        QVERIFY(manager->snapshot().value("runtime").toMap().value("safeMode").toBool());
        QCOMPARE(manager->snapshot().value("runtime").toMap().value("safeModeReason").toString(), "repeated_crashes");
        manager->shutdown();
        QVERIFY(!QFileInfo::exists(marker));
    }

    void safeModeSkipsExtensionCode() {
        {
            auto manager = startManager();
            install(*manager, examples + "/example.lyrics");
            enable(*manager, "example.lyrics");
        }
        qputenv("NEKOTUNE_SAFE_MODE", "1");
        auto manager = startManager();
        QVERIFY(manager->snapshot().value("runtime").toMap().value("safeMode").toBool());
        QCOMPARE(entry(*manager, "example.lyrics").value("state").toString(), "disabled");
        QVERIFY(entry(*manager, "example.lyrics").value("enabled").toBool());
        QVERIFY(!call(*manager, "extensions.enable", {{"id", "example.lyrics"}}));
    }

    void themeCatalogTokenGroups() {
        // Separate appearance and layout selection depends on every token declaring its owner.
        const QStringList groups{"appearance", "layout", "shared", "host"};
        const auto tokens = themes::catalog().value("tokens").toMap();
        QCOMPARE(tokens.size(), 68);
        for (auto it = tokens.cbegin(); it != tokens.cend(); ++it)
            QVERIFY2(groups.contains(it.value().toMap().value("group").toString()), qPrintable(it.key()));
        QCOMPARE(tokens.value("accentPrimary").toMap().value("group").toString(), "appearance");
        QCOMPARE(tokens.value("sidebarWidthWide").toMap().value("group").toString(), "layout");
        QCOMPARE(tokens.value("radiusMd").toMap().value("group").toString(), "shared");
        QCOMPARE(tokens.value("reducedMotion").toMap().value("group").toString(), "host");
    }

    void sharedThemeTokenResolution() {
        const auto &catalog = themes::catalog();
        const auto rules = catalog.value("tokens").toMap();
        const auto dark = themes::baseTokens(catalog, true);
        QCOMPARE(dark.size(), rules.size());
        QCOMPARE(dark.value("bgCanvas").toString(), rules.value("bgCanvas").toMap().value("default").toString());
        const auto light = themes::baseTokens(catalog, false);
        QCOMPARE(light.value("bgCanvas").toString(), catalog.value("light").toMap().value("bgCanvas").toString());

        const QVariantMap theme{
            {"schemaVersion", 1},
            {"tokens", QVariantMap{{"accentPrimary", "#111111"}, {"radiusMd", 20}, {"notAToken", 1}}},
            {"variants",
             QVariantMap{{"dark", QVariantMap{{"tokens", QVariantMap{{"accentPrimary", "#222222"},
                                                                     {"bgSurface", "#333333"}}}}},
                         {"light", QVariantMap{{"tokens", QVariantMap{}}}}}}};
        const auto resolved = themes::resolveThemeTokens(catalog, theme, true);
        QCOMPARE(resolved.value("accentPrimary").toString(), "#222222");
        QCOMPARE(resolved.value("radiusMd").toInt(), 20);
        QVERIFY(!resolved.contains("notAToken"));
        QCOMPARE(resolved.value("bgCard").toString(), "#333333");
        QCOMPARE(themes::resolveThemeTokens(catalog, theme, false).value("accentPrimary").toString(), "#111111");

        auto explicitCard = theme;
        explicitCard["tokens"] = QVariantMap{{"bgCard", "#444444"}};
        QCOMPARE(themes::resolveThemeTokens(catalog, explicitCard, true).value("bgCard").toString(), "#444444");

        const QVariantMap legacy{{"tokens", QVariantMap{{"accentPrimary", "#555555"}}}};
        QVERIFY(themes::resolveThemeTokens(catalog, legacy, false) == light);
        QCOMPARE(themes::resolveThemeTokens(catalog, legacy, true).value("accentPrimary").toString(), "#555555");
    }

    void themeUiCapabilities() {
        QVERIFY(!themes::providesLayout({}));
        QVERIFY(!themes::providesLayout({{"wallpaper", "Wallpaper.qml"}}));
        QVERIFY(!themes::providesLayout({{"ui", QVariantMap{{"pages", QVariantMap{}}}}}));
        for (const auto *key : {"shell", "bottomPlayer", "background"}) {
            QVERIFY(!themes::providesLayout({{"ui", QVariantMap{{QLatin1String(key), ""}}}}));
            QVERIFY(themes::providesLayout({{"ui", QVariantMap{{QLatin1String(key), "View.qml"}}}}));
        }
        QVERIFY(themes::providesLayout({{"ui", QVariantMap{{"pages", QVariantMap{{"home", "Home.qml"}}}}}}));
        QVERIFY(themes::providesWallpaper({{"wallpaper", "Wallpaper.qml"}}));
        QVERIFY(!themes::providesWallpaper({}));
        QVERIFY(!themes::providesWallpaper({{"wallpaper", ""}}));
        QVERIFY(!themes::providesWallpaper({{"ui", QVariantMap{{"wallpaper", "Wallpaper.qml"}}}}));
    }

    void themeWallpaperField() {
        const QDir folder(m_home->filePath("wallpaper-field"));
        QVERIFY(QDir().mkpath(folder.path()));
        QFile file(folder.filePath("Wallpaper.qml"));
        QVERIFY(file.open(QIODevice::WriteOnly) && file.write("import QtQuick\nItem {}\n") > 0);
        file.close();
        const QVariantMap variants{{"light", QVariantMap{}}, {"dark", QVariantMap{}}};
        auto theme = [&](const QVariantMap &extra) {
            QVariantMap value{{"id", "default"}, {"schemaVersion", 1}, {"variants", variants}};
            value.insert(extra);
            return value;
        };
        auto message = [&](const QVariantMap &value) {
            const auto result = themes::validate(value, folder.path(), true);
            return result ? QString() : result.error().message;
        };

        QCOMPARE(message(theme({{"wallpaper", "Wallpaper.qml"}})), QString());
        QCOMPARE(message(theme({{"ui", QVariantMap{{"wallpaper", "Wallpaper.qml"}}}})),
                 QString("Theme wallpaper moved from ui.wallpaper to wallpaper"));
        // Legacy themes only pass the non-strict path (ordinary extensions), so check it there.
        const auto legacy = themes::validate({{"id", "legacy"}, {"wallpaper", "Wallpaper.qml"}}, folder.path(), false);
        QVERIFY(!legacy);
        QCOMPARE(legacy.error().message, QString("Theme wallpaper requires schemaVersion 1"));
        for (const auto &invalid : {QVariant(""), QVariant(1), QVariant("Wallpaper.js")})
            QCOMPARE(message(theme({{"wallpaper", invalid}})), QString("Theme wallpaper source must be QML"));
        QVERIFY(!message(theme({{"wallpaper", "Missing.qml"}})).isEmpty());
        QVERIFY(!message(theme({{"wallpaper", "../Wallpaper.qml"}})).isEmpty());
    }

    void themeLayoutSelection() {
        auto manager = startManager();
        const QDir folder(m_home->filePath("layout-theme"));
        QVERIFY(QDir().mkpath(folder.path()));
        auto write = [&](const QString &name, const QByteArray &bytes) {
            QFile file(folder.filePath(name));
            return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
        };
        QVERIFY(write("Player.qml", "import QtQuick\nItem {}\n"));
        QVERIFY(write("Wallpaper.qml", "import QtQuick\nItem {}\n"));
        const QJsonObject variants{{"light", QJsonObject{}}, {"dark", QJsonObject{}}};
        auto theme = [&](const QString &id, const QJsonObject &ui) {
            QJsonObject value{{"id", id}, {"title", id}, {"schemaVersion", 1}, {"variants", variants}};
            if (!ui.isEmpty())
                value.insert("ui", ui);
            return value;
        };
        auto wall = theme("wall", {});
        wall.insert("wallpaper", "Wallpaper.qml");
        const QJsonObject manifest{
            {"id", "test.layout"}, {"name", "Layout test"}, {"version", "1.0.0"}, {"apiVersion", 1}, {"type", "theme"},
            {"contributes", QJsonObject{{"themes", QJsonArray{theme("palette", {}),
                                                              theme("layout", {{"bottomPlayer", "Player.qml"}}),
                                                              wall}}}}};
        QVERIFY(write("extension.json", QJsonDocument(manifest).toJson()));
        install(*manager, folder.path());
        enable(*manager, "test.layout");
        const auto selections = [&] { return appearance(*manager).selections(); };

        // The legacy single-slot form keeps working and does not create themeLayout.
        QVERIFY(callAppearance(appearance(*manager), "appearance.select", {{"slot", "theme"}, {"contribution", "test.layout/palette"}}));
        QVERIFY(!selections().contains("themeLayout"));
        QVERIFY(call(*manager, "extensions.uninstall", {{"id", "test.layout"}}));
        QVERIFY(!selections().contains("themeLayout"));
        install(*manager, folder.path());
        enable(*manager, "test.layout");

        const auto both = callAppearance(appearance(*manager), "appearance.select",
                               {{"selections", QVariantMap{{"theme", "builtin/ocean"}, {"themeLayout", "test.layout/layout"}}}});
        QVERIFY2(both, qPrintable(errorOf(both)));
        QCOMPARE(selections().value("theme").toString(), "builtin/ocean");
        QCOMPARE(selections().value("themeLayout").toString(), "test.layout/layout");

        // Only running themes with layout entries qualify; wallpaper-only themes do not.
        for (const auto *invalid : {"builtin/ocean", "test.layout/palette", "test.layout/wall", "test.layout/missing"})
            QVERIFY(!callAppearance(appearance(*manager), "appearance.select", {{"slot", "themeLayout"}, {"contribution", QString(invalid)}}));
        QCOMPARE(selections().value("themeLayout").toString(), "test.layout/layout");

        QVERIFY(callAppearance(appearance(*manager), "appearance.select", {{"slot", "themeLayout"}, {"contribution", ""}}));
        QCOMPARE(selections().value("themeLayout").toString(), QString());

        // One invalid entry rejects the whole batch.
        const auto beforeInvalid = selections();
        QVERIFY(!callAppearance(appearance(*manager), "appearance.select",
                      {{"selections", QVariantMap{{"theme", "test.layout/palette"}, {"themeLayout", "builtin/ocean"}}}}));
        QCOMPARE(selections(), beforeInvalid);
        QVERIFY(!callAppearance(appearance(*manager), "appearance.select", {{"selections", QVariantMap{}}}));
        QVERIFY(!callAppearance(appearance(*manager), "appearance.select", {{"selections", QVariantMap{{"themeLayout", 1}}}}));
        QVERIFY(!callAppearance(appearance(*manager), "appearance.select", {{"selections", "theme"}}));
        QVERIFY(!callAppearance(appearance(*manager), "appearance.select", {{"selections", QVariantMap{{"", ""}}}}));
        QVERIFY(!callAppearance(appearance(*manager), "appearance.select", {{"slot", "themeLayout"}}));
        QCOMPARE(selections(), beforeInvalid);

        // A failed save rolls back every slot in the batch.
        const auto appearanceFile = QDir(AppPaths::extensionsDirectory()).filePath("appearance.json");
        QVERIFY(QFile::rename(appearanceFile, appearanceFile + ".saved"));
        QVERIFY(QDir().mkdir(appearanceFile));
        const auto failed = callAppearance(appearance(*manager), "appearance.select",
                                 {{"selections", QVariantMap{{"theme", "test.layout/palette"},
                                                            {"themeLayout", "test.layout/layout"}}}});
        QVERIFY(!failed);
        QCOMPARE(failed.error().code, ErrorCode::Io);
        QCOMPARE(selections(), beforeInvalid);
        QVERIFY(QDir().rmdir(appearanceFile));
        QVERIFY(QFile::rename(appearanceFile + ".saved", appearanceFile));

        QVERIFY(callAppearance(appearance(*manager), "appearance.select",
                     {{"selections", QVariantMap{{"theme", "test.layout/palette"}, {"themeLayout", "test.layout/layout"}}}}));
        manager.reset();
        manager = startManager();
        QCOMPARE(selections().value("theme").toString(), "test.layout/palette");
        QCOMPARE(selections().value("themeLayout").toString(), "test.layout/layout");

        // A disabled package keeps its selections; unchanged entries are not revalidated.
        QVERIFY(call(*manager, "extensions.disable", {{"id", "test.layout"}}));
        const auto keep = callAppearance(appearance(*manager), "appearance.select",
                               {{"selections", QVariantMap{{"theme", "test.layout/palette"}, {"themeLayout", ""}}}});
        QVERIFY2(keep, qPrintable(errorOf(keep)));
        QCOMPARE(selections().value("theme").toString(), "test.layout/palette");
        // Changing to a contribution of the disabled package is still rejected.
        QVERIFY(!callAppearance(appearance(*manager), "appearance.select", {{"slot", "themeLayout"}, {"contribution", "test.layout/layout"}}));

        enable(*manager, "test.layout");
        QVERIFY(callAppearance(appearance(*manager), "appearance.select", {{"slot", "themeLayout"}, {"contribution", "test.layout/layout"}}));
        QVERIFY(call(*manager, "extensions.disable", {{"id", "test.layout"}}));
        QVERIFY(callAppearance(appearance(*manager), "appearance.select",
                     {{"selections", QVariantMap{{"theme", "builtin/ocean"}, {"themeLayout", "test.layout/layout"}}}}));
        QCOMPARE(selections().value("theme").toString(), "builtin/ocean");
        QCOMPARE(selections().value("themeLayout").toString(), "test.layout/layout");
        enable(*manager, "test.layout");
        QVERIFY(callAppearance(appearance(*manager), "appearance.select", {{"slot", "theme"}, {"contribution", "test.layout/palette"}}));

        // Uninstalling the package resets both nonempty selections to the defaults.
        QVERIFY(call(*manager, "extensions.uninstall", {{"id", "test.layout"}}));
        QCOMPARE(selections().value("theme").toString(), QString());
        QCOMPARE(selections().value("themeLayout").toString(), QString());
        manager.reset();
        manager = startManager();
        QCOMPARE(selections().value("theme").toString(), QString());
        QCOMPARE(selections().value("themeLayout").toString(), QString());
    }

    void layeredThemeTokenResolution() {
        const auto &catalog = themes::catalog();
        const QVariantMap appearance{
            {"schemaVersion", 1},
            {"tokens", QVariantMap{{"accentPrimary", "#AA0000"}, {"sidebarWidthWide", 300}, {"radiusMd", 6}}}};
        const QVariantMap layout{
            {"schemaVersion", 1},
            {"tokens", QVariantMap{{"accentPrimary", "#00AA00"}, {"sidebarWidthWide", 260}, {"radiusMd", 20}}}};
        QCOMPARE(themes::resolveThemeTokens(catalog, appearance, true).value("sidebarWidthWide").toInt(), 300);
        const auto combined = themes::resolveThemeTokens(catalog, appearance, true, layout);
        QCOMPARE(combined.value("accentPrimary").toString(), "#AA0000");
        QCOMPARE(combined.value("sidebarWidthWide").toInt(), 260);
        QCOMPARE(combined.value("radiusMd").toInt(), 20);
        // The appearance theme's layout tokens never leak into another layout.
        const QVariantMap plainLayout{{"schemaVersion", 1}};
        const auto plain = themes::resolveThemeTokens(catalog, appearance, true, plainLayout);
        QCOMPARE(plain.value("sidebarWidthWide"), themes::baseTokens(catalog, true).value("sidebarWidthWide"));
        QCOMPARE(plain.value("radiusMd").toInt(), 6);
        // A legacy dark-only appearance still receives the layout's tokens in light mode.
        const QVariantMap legacy{{"tokens", QVariantMap{{"accentPrimary", "#555555"}}}};
        const auto legacyLight = themes::resolveThemeTokens(catalog, legacy, false, layout);
        QCOMPARE(legacyLight.value("sidebarWidthWide").toInt(), 260);
        QCOMPARE(legacyLight.value("accentPrimary"), themes::baseTokens(catalog, false).value("accentPrimary"));
        // Themes cannot change the user's motion preference.
        const QVariantMap motion{{"schemaVersion", 1}, {"tokens", QVariantMap{{"reducedMotion", true}}}};
        QCOMPARE(themes::resolveThemeTokens(catalog, motion, true).value("reducedMotion").toBool(), false);
        QCOMPARE(themes::resolveThemeTokens(catalog, appearance, true, motion).value("reducedMotion").toBool(), false);
    }

    void themePackagesRunWithoutTrust() {
        auto manager = startManager();
        const QDir folder(m_home->filePath("trust-theme"));
        QVERIFY(QDir().mkpath(folder.path()));
        auto write = [&](const QString &name, const QByteArray &bytes) {
            QFile file(folder.filePath(name));
            return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
        };
        auto package = [&](bool withCode, const QString &zipName) {
            QJsonObject theme{{"id", "default"}, {"title", "Trust"}, {"schemaVersion", 1},
                              {"variants", QJsonObject{{"light", QJsonObject{}}, {"dark", QJsonObject{}}}}};
            if (withCode)
                theme.insert("ui", QJsonObject{{"bottomPlayer", "Player.qml"}});
            const QJsonObject manifest{{"id", "test.trust"}, {"name", "Trust"}, {"version", "1.0.0"},
                                       {"apiVersion", 1}, {"type", "theme"},
                                       {"contributes", QJsonObject{{"themes", QJsonArray{theme}}}}};
            QFile::remove(folder.filePath("Player.qml"));
            if (!write("extension.json", QJsonDocument(manifest).toJson()))
                return QString();
            if (withCode && !write("Player.qml", "import QtQuick\nItem {}\n"))
                return QString();
            zip::Writer writer;
            if (!writer.addFile("extension.json", folder.filePath("extension.json")))
                return QString();
            if (withCode && !writer.addFile("Player.qml", folder.filePath("Player.qml")))
                return QString();
            const auto path = m_home->filePath(zipName);
            return writer.save(path) ? path : QString();
        };

        // A data-only package that later gains QML keeps running but is no longer trusted.
        const auto plain = package(false, "plain.nktheme.zip");
        QVERIFY(!plain.isEmpty());
        QVERIFY(call(*manager, "extensions.install", {{"path", plain}, {"themeOnly", true}}));
        QVERIFY(call(*manager, "extensions.enable", {{"id", "test.trust"}}));
        QVERIFY(call(*manager, "extensions.enable", {{"id", "test.trust"}, {"trusted", true}}));
        QVERIFY(entry(*manager, "test.trust").value("trusted").toBool());
        const auto code = package(true, "code.nktheme.zip");
        QVERIFY(!code.isEmpty());
        const auto updated = call(*manager, "extensions.install",
                                  {{"path", code}, {"themeOnly", true}, {"replace", true}, {"expectedId", "test.trust"}});
        QVERIFY2(updated, qPrintable(errorOf(updated)));
        QTRY_COMPARE(entry(*manager, "test.trust").value("state").toString(), "running");
        QVERIFY(entry(*manager, "test.trust").value("hasCode").toBool());
        QVERIFY(!entry(*manager, "test.trust").value("trusted").toBool());

        // Code themes enable without trust, and trust can be granted while running.
        QVERIFY(call(*manager, "extensions.disable", {{"id", "test.trust"}}));
        const auto enabled = call(*manager, "extensions.enable", {{"id", "test.trust"}});
        QVERIFY2(enabled, qPrintable(errorOf(enabled)));
        QCOMPARE(entry(*manager, "test.trust").value("state").toString(), "running");
        const auto generation = entry(*manager, "test.trust").value("generation");
        QVERIFY(call(*manager, "extensions.enable", {{"id", "test.trust"}, {"trusted", true}}));
        QVERIFY(entry(*manager, "test.trust").value("trusted").toBool());
        QCOMPARE(entry(*manager, "test.trust").value("generation"), generation);
        manager.reset();
        manager = startManager();
        QVERIFY(entry(*manager, "test.trust").value("trusted").toBool());

        // Ordinary extensions still require trust.
        install(*manager, examples + "/example.lyrics");
        QVERIFY(!call(*manager, "extensions.enable", {{"id", "example.lyrics"}}));
    }

    void appearanceMigratesFromRegistry() {
        const auto directory = AppPaths::extensionsDirectory();
        QVERIFY(QDir().mkpath(directory));
        QFile registry(QDir(directory).filePath("registry.json"));
        QVERIFY(registry.open(QIODevice::WriteOnly));
        registry.write(R"({"selections":{"theme":"builtin/ocean"}})");
        registry.close();
        auto manager = startManager();
        QCOMPARE(appearance(*manager).selections().value("theme").toString(), "builtin/ocean");
        QFile migrated(QDir(directory).filePath("appearance.json"));
        QVERIFY(migrated.open(QIODevice::ReadOnly));
        QCOMPARE(QJsonDocument::fromJson(migrated.readAll()).object().value("selections").toObject()
                     .value("theme").toString(), "builtin/ocean");
        QVERIFY(registry.open(QIODevice::ReadOnly));
        QVERIFY(!QJsonDocument::fromJson(registry.readAll()).object().contains("selections"));
        const auto snapshot = manager->snapshot();
        QCOMPARE(snapshot.keys(), (QStringList{"commands", "contributions", "extensions", "runtime", "sources", "themes"}));
        QVERIFY(snapshot.value("runtime").toMap().value("ready").toBool());
    }

    void appearanceMigrationKeepsExistingChoices() {
        const auto directory = AppPaths::extensionsDirectory();
        QVERIFY(QDir().mkpath(directory));
        QFile registry(QDir(directory).filePath("registry.json"));
        QVERIFY(registry.open(QIODevice::WriteOnly));
        registry.write(R"({"selections":{"theme":"builtin/yozakura"}})");
        registry.close();
        QFile current(QDir(directory).filePath("appearance.json"));
        QVERIFY(current.open(QIODevice::WriteOnly));
        current.write(R"({"selections":{"theme":"builtin/ocean"}})");
        current.close();
        auto manager = startManager();
        QCOMPARE(appearance(*manager).selections().value("theme").toString(), "builtin/ocean");
        QVERIFY(registry.open(QIODevice::ReadOnly));
        QVERIFY(!QJsonDocument::fromJson(registry.readAll()).object().contains("selections"));
    }

    void appearanceMigrationFailureCanRetry() {
        const auto directory = AppPaths::extensionsDirectory();
        const auto path = QDir(directory).filePath("appearance.json");
        QVERIFY(QDir().mkpath(path));
        QFile registry(QDir(directory).filePath("registry.json"));
        QVERIFY(registry.open(QIODevice::WriteOnly));
        registry.write(R"({"selections":{"theme":"builtin/ocean"}})");
        registry.close();
        auto manager = startManager();
        // An unrelated registry save must retain migration input while appearance.json is unwritable.
        install(*manager, faultyDirectory);
        QVERIFY(registry.open(QIODevice::ReadOnly));
        QCOMPARE(QJsonDocument::fromJson(registry.readAll()).object().value("selections").toObject()
                     .value("theme").toString(), "builtin/ocean");
        registry.close();
        manager.reset();
        QVERIFY(QDir().rmdir(path));
        manager = startManager();
        QCOMPARE(appearance(*manager).selections().value("theme").toString(), "builtin/ocean");
        QVERIFY(registry.open(QIODevice::ReadOnly));
        QVERIFY(!QJsonDocument::fromJson(registry.readAll()).object().contains("selections"));
    }

    void appearanceEmitsOnlyCommittedChanges() {
        auto manager = startManager();
        auto &service = appearance(*manager);
        QSignalSpy changes(&service, &IAppearanceBackend::eventReady);
        QVERIFY(callAppearance(service, "appearance.select", {{"slot", "theme"}, {"contribution", "builtin/ocean"}}));
        QCOMPARE(changes.size(), 1);
        const auto event = changes.first().first().toMap();
        QCOMPARE(event.value("event").toString(), "appearance.changed");
        QCOMPARE(event.value("selections").toMap(), service.selections());
        QVERIFY(!callAppearance(service, "appearance.select",
                                {{"selections", QVariantMap{{"theme", "builtin/yozakura"},
                                                            {"shell", "missing/shell"}}}}));
        QCOMPARE(changes.size(), 1);
        QCOMPARE(callAppearance(service, "appearance.get").value().value("selections").toMap(), service.selections());
    }

    void zipPackagesAndThemes() {
        auto manager = startManager();
        const auto exported = m_home->filePath("ocean.zip");
        const auto builtin = callAppearance(appearance(*manager), "appearance.export_theme", {{"contribution", "builtin/ocean"}, {"path", exported}});
        QVERIFY2(builtin, qPrintable(errorOf(builtin)));
        const auto installed = call(*manager, "extensions.install", {{"path", exported}, {"themeOnly", true}});
        QVERIFY2(installed, qPrintable(errorOf(installed)));
        const auto theme = installed.value().value("extension").toMap();
        QCOMPARE(theme.value("type").toString(), "theme");
        QVERIFY(!theme.value("hasCode").toBool());
        const auto id = theme.value("id").toString();
        QVERIFY(theme.value("directory").toString().contains("/packages/" + id + "-1.0.0-"));
        // Data-only themes need no trust decision.
        QVERIFY(call(*manager, "extensions.enable", {{"id", id}}));
        const auto contribution = id + "/theme";
        QVERIFY(callAppearance(appearance(*manager), "appearance.select", {{"slot", "theme"}, {"contribution", contribution}}));
        const auto again = m_home->filePath("again.nktheme.zip");
        QVERIFY(callAppearance(appearance(*manager), "appearance.export_theme", {{"contribution", contribution}, {"path", again}}));
        const auto replaced = call(*manager, "extensions.install", {{"path", again}, {"replace", true}, {"expectedId", id}});
        QVERIFY2(replaced, qPrintable(errorOf(replaced)));
        QCOMPARE(entry(*manager, id).value("state").toString(), "running");
        // The replaced package directory is collected once nothing references it.
        QCOMPARE(QDir(QDir(AppPaths::extensionsDirectory()).filePath("packages")).entryList(QDir::Dirs | QDir::NoDotAndDotDot).size(), 1);

        zip::Writer writer;
        const QDir lyrics(examples + "/example.lyrics");
        QDirIterator files(lyrics.path(), QDir::Files, QDirIterator::Subdirectories);
        while (files.hasNext()) {
            const auto file = files.next();
            QVERIFY(writer.addFile(lyrics.relativeFilePath(file), file));
        }
        const auto package = m_home->filePath("lyrics.zip");
        QVERIFY(writer.save(package));
        const auto code = call(*manager, "extensions.install", {{"path", package}});
        QVERIFY2(code, qPrintable(errorOf(code)));
        enable(*manager, "example.lyrics");
        QVERIFY(!call(*manager, "extensions.install", {{"path", package}, {"themeOnly", true}}));
        // Packages produced by nekotune_add_extension (libarchive) install as well.
        const auto built = call(*manager, "extensions.install",
                                {{"path", QStringLiteral(NEKOTUNE_EXAMPLE_PACKAGES "/example.custom-ui-") + extensionPlatform() +
                                              ".nekotune.zip"}});
        QVERIFY2(built, qPrintable(errorOf(built)));
        enable(*manager, "example.custom-ui");

        zip::Writer unsafe;
        unsafe.add("../escape.txt", "x");
        QVERIFY(unsafe.save(m_home->filePath("unsafe.zip")));
        QVERIFY(!call(*manager, "extensions.install", {{"path", m_home->filePath("unsafe.zip")}}));
        QVERIFY(!QFileInfo::exists(m_home->filePath("escape.txt")));
        QCOMPARE(QDir(QDir(AppPaths::extensionsDirectory()).filePath("packages")).entryList({".staging-*"}, QDir::Dirs | QDir::Hidden).size(), 0);
    }

    void audioProxyAndDownloads() {
        auto manager = startManager();
        install(*manager, examples + "/example.test-source");
        enable(*manager, "example.test-source");
        const auto leased = wait<IMusicSourceCatalog::Playback>([&](auto done) {
            manager->musicSources().resolvePlayback("example.test-source/tones", "two", done);
        });
        QVERIFY2(leased, qPrintable(leased ? QString() : leased.error().message));
        const QUrl url = leased.value().url;
        QCOMPARE(url.host(), "127.0.0.1");
        QCOMPARE(leased.value().extension, "wav");
        QNetworkAccessManager network;
        QNetworkRequest request(url);
        request.setRawHeader("Range", "bytes=0-11");
        std::unique_ptr<QNetworkReply> reply(network.get(request));
        QTRY_VERIFY_WITH_TIMEOUT(reply->isFinished(), 10'000);
        QCOMPARE(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), 206);
        QCOMPARE(reply->rawHeader("Content-Range"), "bytes 0-11/352844");
        const auto prefix = reply->readAll();
        QVERIFY(prefix.startsWith("RIFF") && prefix.endsWith("WAVE"));
        manager->musicSources().release(url);
        reply.reset(network.get(QNetworkRequest(url)));
        QTRY_VERIFY_WITH_TIMEOUT(reply->isFinished(), 10'000);
        QCOMPARE(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), 404);

        const auto base = m_home->filePath("download");
        const auto downloaded = wait<QString>([&](auto done) {
            manager->musicSources().download("example.test-source/tones", "one", base, "t1", {}, done);
        });
        QVERIFY2(downloaded, qPrintable(downloaded ? QString() : downloaded.error().message));
        QCOMPARE(downloaded.value(), base + ".wav");
        QCOMPARE(QFileInfo(base + ".wav").size(), 352844);
        QVERIFY(QDir(m_home->path()).entryList({"download.partial-*"}).isEmpty());
    }

    void legacyNodeInstallUpgradesInPlace() {
        // A profile from the Node runtime: an enabled, trusted apiVersion 1 extension with data.
        QTemporaryDir legacy;
        QFile manifest(legacy.filePath("extension.json"));
        QVERIFY(manifest.open(QIODevice::WriteOnly));
        manifest.write(R"({"id":"test.faulty","name":"Old","version":"1.0.0","apiVersion":1,"main":"main.mjs"})");
        manifest.close();
        QVERIFY(QDir().mkpath(QDir(AppPaths::extensionsDirectory()).filePath("data/test.faulty")));
        QFile config(QDir(AppPaths::extensionsDirectory()).filePath("data/test.faulty/config.json"));
        QVERIFY(config.open(QIODevice::WriteOnly));
        config.write(R"({"mode":"ok","kept":true})");
        config.close();
        QFile registry(QDir(AppPaths::extensionsDirectory()).filePath("registry.json"));
        QVERIFY(registry.open(QIODevice::WriteOnly));
        registry.write(QJsonDocument::fromVariant(QVariantMap{
            {"bundledSeen", QVariantList{}},
            {"entries", QVariantList{QVariantMap{{"id", "test.faulty"}, {"directory", legacy.path()}, {"development", true},
                                                 {"enabled", true}, {"trusted", true}}}}}).toJson());
        registry.close();

        auto manager = startManager();
        auto old = entry(*manager, "test.faulty");
        QCOMPARE(old.value("state").toString(), "failed");
        QVERIFY(old.value("error").toString().contains("apiVersion 3"));
        // Replacing it with the native build keeps trust, enabled state and data.
        const auto replaced = call(*manager, "extensions.install", {{"path", faultyDirectory}, {"development", true}, {"replace", true}});
        QVERIFY2(replaced, qPrintable(errorOf(replaced)));
        QCOMPARE(entry(*manager, "test.faulty").value("state").toString(), "running");
        QVERIFY(entry(*manager, "test.faulty").value("trusted").toBool());
        QVERIFY(call(*manager, "extensions.get_config", {{"id", "test.faulty"}}).value().value("config").toMap().value("kept").toBool());
        // Unknown registry keys from the old runtime survive.
        QVERIFY(QFile(registry.fileName()).open(QIODevice::ReadOnly));
        QFile saved(registry.fileName());
        QVERIFY(saved.open(QIODevice::ReadOnly));
        QVERIFY(QJsonDocument::fromJson(saved.readAll()).object().contains("bundledSeen"));
    }

    void uninstallClearsData() {
        auto manager = startManager();
        install(*manager, faultyDirectory);
        enable(*manager, "test.faulty");
        QVERIFY(call(*manager, "extensions.call", {{"id", "test.faulty"}, {"service", "storage"}, {"params", QVariantMap{{"value", 1}}}}));
        const auto data = QDir(AppPaths::extensionsDirectory()).filePath("data/test.faulty");
        QVERIFY(QFileInfo::exists(data));
        QVERIFY(call(*manager, "extensions.uninstall", {{"id", "test.faulty"}, {"clearData", true}}));
        QVERIFY(entry(*manager, "test.faulty").isEmpty());
        QVERIFY(!QFileInfo::exists(data));
        QVERIFY(QFileInfo::exists(faultyDirectory + "/extension.json"));
    }
};

QTEST_GUILESS_MAIN(NativeExtensionsTest)
#include "native_extensions_test.moc"
