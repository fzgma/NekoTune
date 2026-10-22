#include "app_paths.h"
#include "application/playback/shuffle_strategy_service.h"
#include "application/playback/playback_order_service.h"
#include "infrastructure/extensions/crash_guard.h"
#include "infrastructure/extensions/extension_manifest.h"
#include "infrastructure/extensions/plugin_manager.h"
#include "infrastructure/extensions/semver.h"
#include "infrastructure/extensions/theme_package.h"
#include "infrastructure/extensions/zip_archive.h"
#include "themes/theme_resolution.h"
#include <QDirIterator>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <memory>
#include <optional>
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

Result<QVariantMap> call(PluginManager &manager, const QString &method, const QVariantMap &params = {},
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
QVariantMap entry(const PluginManager &manager, const QString &id) {
    for (const auto &value : manager.snapshot().value("extensions").toList())
        if (value.toMap().value("id") == id)
            return value.toMap();
    return {};
}
QString errorOf(const Result<QVariantMap> &result) { return result ? QString() : result.error().message; }
} // namespace

class NativeExtensionsTest final : public QObject {
    Q_OBJECT
    std::unique_ptr<QTemporaryDir> m_home;
    QList<QPair<QString, QVariantMap>> m_hostCalls;

    std::unique_ptr<PluginManager> startManager() {
        auto manager = std::make_unique<PluginManager>();
        manager->start([this](const QString &method, const QVariantMap &params, IExtensionBackend::Completion done) {
            m_hostCalls.append({method, params});
            done(QVariantMap{});
        });
        [&] { QTRY_VERIFY(manager->snapshot().value("startupComplete").toBool()); }();
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
        QVERIFY(manifest.error().message.contains("apiVersion 2"));

        write(R"({"id":"x.qt","name":"Qt","version":"1.0.0","apiVersion":2,"qt":"5.15",)"
              R"("library":{")" + extensionPlatform().toUtf8() + R"(":"lib/x.so"}})");
        manifest = readManifest(directory.path());
        QVERIFY(manifest);
        QVERIFY(manifest.value().unavailable.contains("Qt 5.15"));

        write(R"({"id":"x.ui","name":"UI","version":"1.0.0","apiVersion":2,)"
              R"("contributes":{"pages":[{"id":"p","source":"../escape.qml"}]}})");
        QVERIFY(!readManifest(directory.path()));
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
        const auto page = extension.value("contributes").toMap().value("pages").toList().value(0).toMap();
        QCOMPARE(page.value("id").toString(), "example.custom-ui/hello");
        QVERIFY(QDir::isAbsolutePath(page.value("source").toString()));
        const auto commands = extension.value("registrations").toMap().value("commands").toList();
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
        QCOMPARE(entry(*manager, "example.custom-ui").value("contributes").toMap(), QVariantMap{});
        QVERIFY(!call(*manager, "extensions.call", {{"id", "example.custom-ui"}, {"service", "hello"}}));
    }

    void shuffleRegistrationsAndGenerations() {
        auto manager = startManager();
        install(*manager, QStringLiteral(NEKOTUNE_TEST_EXTENSIONS "/test.shuffle"));
        enable(*manager, "test.shuffle");
        auto catalogue = manager->shuffleStrategies();
        QCOMPARE(catalogue.size(), 1);
        QCOMPARE(catalogue.first().id, "test.shuffle/test");
        const auto generation = catalogue.first().generation;
        const auto validation = call(*manager, "extensions.call", {{"id", "test.shuffle"}, {"service", "validate"}});
        QVERIFY(validation);
        for (const auto &value : validation.value()) QVERIFY(!value.toBool());
        sdk::ShuffleRequest request{{{11, 7, "Title", "Artist"}, {12, 7, "Duplicate", "Artist"}}, 11, 42, {}};
        const auto first = manager->selectShuffle("test.shuffle/test", generation, request);
        QVERIFY(first);
        QCOMPARE(first.value().queueId, 11);
        QCOMPARE(first.value().state.value("step").toInt(), 1);
        QCOMPARE(manager->selectShuffle("test.shuffle/test", generation, request).value().queueId, 11);
        request.state = first.value().state;
        QCOMPARE(manager->selectShuffle("test.shuffle/test", generation, request).value().queueId, 12);
        QVERIFY(call(*manager, "extensions.call", {{"id", "test.shuffle"}, {"service", "release"}}));
        QVERIFY(manager->shuffleStrategies().isEmpty());
        QVERIFY(!manager->selectShuffle("test.shuffle/test", generation, request));
        QVERIFY(call(*manager, "extensions.call", {{"id", "test.shuffle"}, {"service", "register"}}));
        catalogue = manager->shuffleStrategies();
        QCOMPARE(catalogue.size(), 1);
        QVERIFY(catalogue.first().generation > generation);
        QVERIFY(!manager->selectShuffle("test.shuffle/test", generation, request));
        const auto beforeReload = catalogue.first().generation;
        QVERIFY(call(*manager, "extensions.reload", {{"id", "test.shuffle"}}));
        QCOMPARE(manager->shuffleStrategies().size(), 1);
        QVERIFY(manager->shuffleStrategies().first().generation > beforeReload);
        QVERIFY(!manager->selectShuffle("test.shuffle/test", beforeReload, request));
        QVERIFY(call(*manager, "extensions.disable", {{"id", "test.shuffle"}}));
        QVERIFY(manager->shuffleStrategies().isEmpty());
    }
    void sdk20BinaryContextRemainsCompatible() {
        auto manager = startManager();
        install(*manager, QStringLiteral(NEKOTUNE_TEST_EXTENSIONS "/test.legacy"));
        enable(*manager, "test.legacy");
        const auto result = call(*manager, "extensions.call", {{"id", "test.legacy"}, {"service", "ping"}});
        QVERIFY(result);
        QCOMPARE(result.value().value("value").toString(), "works");
        QVERIFY(call(*manager, "extensions.disable", {{"id", "test.legacy"}}));
        enable(*manager, "test.legacy");
        QVERIFY(call(*manager, "extensions.call", {{"id", "test.legacy"}, {"service", "ping"}}));
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
            service.setCatalogue(manager->shuffleStrategies(),
                [&](const QString &id, quint64 generation, const sdk::ShuffleRequest &request) {
                    return manager->selectShuffle(id, generation, request);
                });
        };
        connect(manager.get(), &PluginManager::shuffleCatalogueChanged, &service, sync);
        sync();
        QCOMPARE(service.effective(), "test.shuffle/test");
        const auto generation = manager->shuffleStrategies().first().generation;
        PlaybackOrderService order(service.createStrategy(), PlaybackMode::Shuffle);
        PlayerQueue queue;
        queue.add("/first.wav", {1, "hash"}); queue.add("/second.wav", {1, "hash"});
        queue.setCurrentIndex(0); order.reset(queue);
        QCOMPARE(order.propose(queue, PlaybackAdvance::Next).queueId, 2); // Immediate safe fallback.
        QVERIFY(manager->shuffleStrategies().isEmpty());
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
        QVERIFY(manager->shuffleStrategies().first().generation > generation);
    }

    void artistShuffleExample() {
        auto manager = startManager();
        install(*manager, examples + "/example.shuffle");
        enable(*manager, "example.shuffle");
        const auto generation = manager->shuffleStrategies().first().generation;
        sdk::ShuffleRequest request{{{11, 1, "One", "Artist A"}, {12, 2, "Two", "artist a"},
                                     {13, 3, "Three", "Artist B"}}, 11, 42, {}};
        for (quint32 seed = 0; seed < 20; ++seed) {
            request.seed = seed;
            const auto chosen = manager->selectShuffle("example.shuffle/artist", generation, request);
            QVERIFY(chosen); QCOMPARE(chosen.value().queueId, 13);
        }
        request.queue.removeLast();
        QCOMPARE(manager->selectShuffle("example.shuffle/artist", generation, request).value().queueId, 12);
        request.queue.removeLast();
        QCOMPARE(manager->selectShuffle("example.shuffle/artist", generation, request).value().queueId, 11);
    }

    void lyricsAndAutomationExamples() {
        auto manager = startManager();
        install(*manager, examples + "/example.lyrics");
        install(*manager, examples + "/example.automation");
        enable(*manager, "example.lyrics");
        enable(*manager, "example.automation");

        const auto lyrics = call(*manager, "extensions.provider_call",
                                 {{"source", "example.lyrics/demo"}, {"kind", "lyrics"}, {"operation", "search"},
                                  {"params", QVariantMap{{"title", "Song"}}}});
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
        QTRY_COMPARE(entry(*manager, "test.faulty").value("contributes").toMap().value("pages").toList().size(), 1);

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

        const auto search = call(*manager, "extensions.provider_call",
                                 {{"source", "test.faulty/tones"}, {"kind", "music"}, {"operation", "search"},
                                  {"params", QVariantMap{{"query", "A"}}}});
        QVERIFY2(search, qPrintable(errorOf(search)));
        QCOMPARE(search.value().value("tracks").toList().value(0).toMap().value("title").toString(), "Tone A");
        QCOMPARE(search.value().value("tracks").toList().value(0).toMap().value("duration_ms").toInt(), 8000);
        QCOMPARE(search.value().value("cursor").toString(), "next");
        const auto audio = call(*manager, "extensions.resolve_audio", {{"source", "test.faulty/tones"}, {"id", "one"}});
        QVERIFY2(audio, qPrintable(errorOf(audio)));
        QVERIFY(QUrl(audio.value().value("url").toString()).toLocalFile().endsWith("/data/test.faulty/tone.wav"));
        QVERIFY(!call(*manager, "extensions.resolve_audio", {{"source", "test.faulty/tones"}, {"id", "outside"}}));
        const auto music = entry(*manager, "test.faulty").value("registrations").toMap().value("music").toList();
        QCOMPARE(music.value(0).toMap().value("name").toString(), "Tones");
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

    void safeModeSkipsExtensionCode() {
        {
            auto manager = startManager();
            install(*manager, examples + "/example.lyrics");
            enable(*manager, "example.lyrics");
        }
        qputenv("NEKOTUNE_SAFE_MODE", "1");
        auto manager = startManager();
        QVERIFY(manager->snapshot().value("safeMode").toBool());
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

    void zipPackagesAndThemes() {
        auto manager = startManager();
        const auto exported = m_home->filePath("ocean.zip");
        const auto builtin = call(*manager, "extensions.export_theme", {{"contribution", "builtin/ocean"}, {"path", exported}});
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
        QVERIFY(call(*manager, "extensions.select", {{"slot", "theme"}, {"contribution", contribution}}));
        const auto again = m_home->filePath("again.nktheme.zip");
        QVERIFY(call(*manager, "extensions.export_theme", {{"contribution", contribution}, {"path", again}}));
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
        const auto leased = call(*manager, "extensions.resolve_audio", {{"source", "example.test-source/tones"}, {"id", "two"}});
        QVERIFY2(leased, qPrintable(errorOf(leased)));
        const QUrl url(leased.value().value("url").toString());
        QCOMPARE(url.host(), "127.0.0.1");
        QCOMPARE(leased.value().value("extension").toString(), "wav");
        QNetworkAccessManager network;
        QNetworkRequest request(url);
        request.setRawHeader("Range", "bytes=0-11");
        std::unique_ptr<QNetworkReply> reply(network.get(request));
        QTRY_VERIFY_WITH_TIMEOUT(reply->isFinished(), 10'000);
        QCOMPARE(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), 206);
        QCOMPARE(reply->rawHeader("Content-Range"), "bytes 0-11/352844");
        const auto prefix = reply->readAll();
        QVERIFY(prefix.startsWith("RIFF") && prefix.endsWith("WAVE"));
        QVERIFY(call(*manager, "extensions.release_audio", {{"url", url.toString()}}));
        reply.reset(network.get(QNetworkRequest(url)));
        QTRY_VERIFY_WITH_TIMEOUT(reply->isFinished(), 10'000);
        QCOMPARE(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), 404);

        QSignalSpy events(manager.get(), &IExtensionBackend::eventReady);
        const auto base = m_home->filePath("download");
        const auto downloaded = call(*manager, "extensions.download_audio",
                                     {{"source", "example.test-source/tones"}, {"id", "one"}, {"base", base}, {"task", "t1"}});
        QVERIFY2(downloaded, qPrintable(errorOf(downloaded)));
        QCOMPARE(downloaded.value().value("path").toString(), base + ".wav");
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
        QVERIFY(old.value("error").toString().contains("apiVersion 2"));
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
