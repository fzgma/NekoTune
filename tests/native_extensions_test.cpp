#include "app_paths.h"
#include "infrastructure/extensions/crash_guard.h"
#include "infrastructure/extensions/extension_manifest.h"
#include "infrastructure/extensions/plugin_manager.h"
#include "infrastructure/extensions/semver.h"
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
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
