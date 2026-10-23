#include "app_paths.h"
#include "application/audio/pcm_service.h"
#include "infrastructure/extensions/plugin_manager.h"
#include "infrastructure/playback/ffmpeg_playback_backend.h"
#include "infrastructure/playback/pcm_capture_buffer.h"
#include "support/audio_backend.h"
#include <QDataStream>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>
#include <chrono>
#include <cmath>
#include <ctime>
#include <numbers>
#include <optional>
#include <thread>
using namespace nekotune;
class CaptureAudio final : public TestAudioBackend {
  public:
    bool enabled = false, supported = true;
    QVector<sdk::PcmBlock> pending;
    bool supportsPcmCapture() const override { return supported; }
    void setPcmCaptureEnabled(bool value) override { enabled = value; }
    QVector<sdk::PcmBlock> takePcmBlocks() override { return std::exchange(pending, {}); }
    void setSource(const QUrl &) override {}
    QUrl source() const override { return {}; }
    void play() override {}
    void pause() override {}
    void stop() override {}
    void seek(qint64) override {}
    void setVolume(double) override {}
    qint64 position() const override { return 0; }
    qint64 duration() const override { return 0; }
    double volume() const override { return 1; }
    AudioMetadata metadata() const override { return {}; }
};
class PcmCaptureTest : public QObject {
    Q_OBJECT
  private slots:
    void consumedOnlyAndVolume() {
        auto buffer = std::make_unique<PcmCaptureBuffer>();
        buffer->reset(1);
        buffer->setEnabled(true);
        std::vector<float> samples(1024, .5f), original = samples;
        buffer->append(samples, 0);
        QVERIFY(buffer->take(1).isEmpty());
        buffer->advance(256, 48000, .4, 1000);
        auto chunks = buffer->take(1);
        QCOMPARE(chunks.size(), 1);
        QCOMPARE(chunks[0].samples.size(), 512);
        QCOMPARE(chunks[0].framePosition, 0);
        for (auto value : chunks[0].samples)
            QCOMPARE(value, .2f);
        QCOMPARE(samples, original); // Observation never alters playback samples.
        buffer->advance(512, 48000, 0, 1005);
        chunks = buffer->take(1);
        QCOMPARE(chunks.size(), 1);
        for (auto value : chunks[0].samples)
            QCOMPARE(value, 0.f);
    }
    void resetAndDisabled() {
        auto buffer = std::make_unique<PcmCaptureBuffer>();
        buffer->reset(1);
        buffer->setEnabled(true);
        std::vector<float> samples(1024, 1);
        buffer->append(samples, 0);
        buffer->advance(512, 48000, 1, 10);
        buffer->reset(2);
        QVERIFY(buffer->take(1).isEmpty());
        buffer->append(samples, 0);
        buffer->advance(512, 48000, 1, 10);
        auto chunks = buffer->take(2);
        QCOMPARE(chunks.size(), 1);
        QVERIFY(chunks[0].generation > 1);
        buffer->setEnabled(false);
        buffer->append(samples, 512);
        buffer->advance(1024, 48000, 1, 20);
        QVERIFY(buffer->take(2).isEmpty());
    }
    void overflowNeverWaitsForConsumer() {
        auto buffer = std::make_unique<PcmCaptureBuffer>();
        buffer->reset(1);
        buffer->setEnabled(true);
        std::vector<float> samples(1024, .1f);
        QElapsedTimer timer;
        timer.start();
        for (int i = 0; i < 400; ++i) {
            buffer->append(samples, i * 512);
            buffer->advance((i + 1) * 512, 48000, 1, i * 10);
        }
        QVERIFY(timer.elapsed() < 1000);
        QVERIFY(buffer->droppedFrames() > 0);
        QCOMPARE(buffer->take(1).size(), qsizetype(PcmCaptureBuffer::queueSlots));
    }
    void concurrentProducerConsumer() {
        auto buffer = std::make_unique<PcmCaptureBuffer>();
        buffer->setEnabled(true);
        std::atomic<bool> finished{false};
        std::thread producer([&] {
            buffer->reset(3);
            std::vector<float> samples(1024, .25f);
            for (int i = 0; i < 2000; ++i) {
                buffer->append(samples, i * 512);
                buffer->advance((i + 1) * 512, 48000, 1, i * 10);
            }
            finished = true;
        });
        qint64 previous = -1;
        int observed = 0;
        bool valid = true;
        while (!finished.load()) {
            for (const auto &block : buffer->take(3)) {
                valid &= block.framePosition > previous;
                previous = block.framePosition;
                for (auto value : block.samples)
                    valid &= value == .25f;
                ++observed;
            }
        }
        producer.join();
        QVERIFY(valid);
        QVERIFY(observed > 0);
    }
    void nativeMutedPlayback() {
        if (qEnvironmentVariable("NEKOTUNE_TEST_NATIVE_PCM") != "1")
            QSKIP("Explicit native audio acceptance only (muted)");
        QTemporaryDir directory;
        QFile fixture(directory.filePath("tone.wav"));
        QVERIFY(fixture.open(QIODevice::WriteOnly));
        QDataStream writer(&fixture);
        writer.setByteOrder(QDataStream::LittleEndian);
        constexpr int rate = 48000, frames = rate * 3;
        fixture.write("RIFF", 4);
        writer << quint32(36 + frames * 4);
        fixture.write("WAVEfmt ", 8);
        writer << quint32(16) << quint16(1) << quint16(2) << quint32(rate) << quint32(rate * 4) << quint16(4)
               << quint16(16);
        fixture.write("data", 4);
        writer << quint32(frames * 4);
        for (int i = 0; i < frames; ++i) {
            const auto value = qint16(8192 * std::sin(2 * std::numbers::pi * 997 * i / rate));
            writer << value << value;
        }
        fixture.close();
        if (const auto path = qEnvironmentVariable("NEKOTUNE_TEST_PCM_FIXTURE"); !path.isEmpty()) {
            QFile::remove(path);
            QVERIFY(QFile::copy(fixture.fileName(), path));
        }
        FFmpegPlaybackBackend audio;
        const auto outputs = audio.audioOutputs();
        QVERIFY(!outputs.isEmpty());
        const auto output =
            std::find_if(outputs.begin(), outputs.end(), [](const auto &device) { return device.isDefault; });
        QVERIFY(output != outputs.end());
        audio.setVolume(0);
        audio.setAudioOutput(output->id);
        PcmService service(audio);
        QVERIFY(service.capabilities().available);
        int received = 0;
        bool silent = true;
        qint64 previousFrame = -1;
        quint64 previousGeneration = 0;
        QElapsedTimer elapsed;
        elapsed.start();
        qint64 firstBlock = -1;
        std::unique_ptr<PluginManager> analyzer;
        qint64 firstSpectrum = -1;
        const auto extensionDirectory = qEnvironmentVariable("NEKOTUNE_TEST_SPECTRUM_EXTENSION_DIR");
        if (!extensionDirectory.isEmpty()) {
            qputenv("NEKOTUNE_HOME", directory.filePath("home").toUtf8());
            qunsetenv("NEKOTUNE_SAFE_MODE");
            analyzer = std::make_unique<PluginManager>();
            analyzer->setPcmService(&service);
            analyzer->start([](const QString &, const QVariantMap &, IExtensionBackend::Completion done) {
                done(QVariantMap{{"state", "playing"}});
            });
            QTRY_VERIFY(analyzer->snapshot().value("runtime").toMap().value("startupComplete").toBool());
            auto invoke = [&](const QString &method, const QVariantMap &params) -> bool {
                std::optional<Result<QVariantMap>> reply;
                analyzer->request(method, params, [&](Result<QVariantMap> value) { reply = value; });
                QElapsedTimer timer;
                timer.start();
                while (!reply && timer.elapsed() < 10000)
                    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
                return reply && bool(*reply);
            };
            QVERIFY(invoke("extensions.install", {{"path", extensionDirectory}, {"development", true}}));
            QVERIFY(invoke("extensions.enable", {{"id", "nekotune.spectrum"}, {"trusted", true}}));
            QVERIFY(invoke("extensions.call", {{"id", "nekotune.spectrum"}, {"service", "acquire"}}));
            connect(analyzer.get(), &PluginManager::eventReady, analyzer.get(), [&](const QVariantMap &event) {
                if (event.value("event") == "extension.nekotune.spectrum.frame" && firstSpectrum < 0)
                    firstSpectrum = elapsed.elapsed();
            });
            connect(&audio, &IPlaybackBackend::stateChanged, analyzer.get(), [&](PlayerState state) {
                analyzer->publish({{"event", "player.state_changed"}, {"state", toString(state)}});
            });
        }
        elapsed.restart();
        const auto cpuStart = std::clock();
        auto subscription = service.subscribe([&](const sdk::PcmBlock &block) {
            if (firstBlock < 0)
                firstBlock = elapsed.elapsed();
            for (auto value : block.samples)
                silent &= value == 0;
            if (previousGeneration != block.generation) {
                previousFrame = -1;
                previousGeneration = block.generation;
            }
            QVERIFY(block.framePosition > previousFrame);
            previousFrame = block.framePosition;
            ++received;
        });
        connect(&audio, &IPlaybackBackend::stateChanged, &audio,
                [](auto state) { qInfo() << "Native audio state" << int(state); });
        connect(&audio, &IPlaybackBackend::decoderFailure, &audio,
                [](const QString &message, bool) { qWarning() << message; });
        audio.setSource(QUrl::fromLocalFile(fixture.fileName()));
        audio.finishAfterCurrent();
        audio.play();
        QTRY_VERIFY_WITH_TIMEOUT(received > 0, 10000);
        QTest::qWait(1000);
        audio.pause();
        QTest::qWait(100);
        const int paused = received;
        QTest::qWait(100);
        QCOMPARE(received, paused);
        QVERIFY(silent);
        qInfo() << "Native muted PCM: first consumed block ms" << firstBlock << "blocks" << received
                << "first spectrum event ms" << firstSpectrum << "process CPU ms"
                << (std::clock() - cpuStart) * 1000. / CLOCKS_PER_SEC << "wall ms" << elapsed.elapsed();
        if (analyzer)
            QVERIFY(firstSpectrum >= 0);
        audio.seek(500);
        QTest::qWait(50);
        audio.play();
        QTRY_VERIFY(received > paused);
        subscription.reset();
        if (analyzer)
            analyzer->shutdown();
        audio.shutdown();
    }
    void nativeSpectrumExtension() {
        const auto directory = qEnvironmentVariable("NEKOTUNE_TEST_SPECTRUM_EXTENSION_DIR");
        if (directory.isEmpty())
            QSKIP("Set NEKOTUNE_TEST_SPECTRUM_EXTENSION_DIR for native spectrum integration");
        QTemporaryDir home;
        qputenv("NEKOTUNE_HOME", home.path().toUtf8());
        qunsetenv("NEKOTUNE_SAFE_MODE");
        CaptureAudio audio;
        PcmService service(audio);
        PluginManager manager;
        manager.setPcmService(&service);
        manager.start([](const QString &, const QVariantMap &, IExtensionBackend::Completion done) {
            done(QVariantMap{{"state", "playing"}});
        });
        QTRY_VERIFY(manager.snapshot().value("runtime").toMap().value("startupComplete").toBool());
        auto call = [&](const QString &method, const QVariantMap &params) -> Result<QVariantMap> {
            std::optional<Result<QVariantMap>> reply;
            manager.request(method, params, [&](Result<QVariantMap> value) { reply = value; });
            QElapsedTimer timer;
            timer.start();
            while (!reply && timer.elapsed() < 10000)
                QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
            return reply ? *reply : Result<QVariantMap>(failure("timeout"));
        };
        auto installed = call("extensions.install", {{"path", directory}, {"development", true}});
        QVERIFY2(installed, qPrintable(installed ? QString() : installed.error().message));
        QVERIFY(call("extensions.enable", {{"id", "nekotune.spectrum"}, {"trusted", true}}));
        QVERIFY(call("extensions.call", {{"id", "nekotune.spectrum"}, {"service", "acquire"}}));
        QCOMPARE(service.subscriberCount(), 1);
        int frames = 0;
        connect(&manager, &PluginManager::eventReady, &manager, [&](const QVariantMap &event) {
            if (event.value("event") == "extension.nekotune.spectrum.frame") {
                ++frames;
                QCOMPARE(event.value("data").toMap().value("bands").toList().size(), 48);
            }
        });
        for (int chunk = 0; chunk < 32; ++chunk) {
            sdk::PcmBlock block;
            block.sampleRate = 48000;
            block.generation = 1;
            block.framePosition = chunk * 512;
            block.samples.resize(1024);
            for (int i = 0; i < 512; ++i)
                block.samples[i * 2] = block.samples[i * 2 + 1] =
                    float(.5 * std::sin(2 * std::numbers::pi * 1000 * (chunk * 512 + i) / 48000));
            audio.pending.append(block);
            QTest::qWait(12);
        }
        QTRY_VERIFY(frames > 0);
        QVERIFY(call("extensions.disable", {{"id", "nekotune.spectrum"}}));
        QCOMPARE(service.subscriberCount(), 0);
        QVERIFY(!audio.enabled);
        const auto themePackage = qEnvironmentVariable("NEKOTUNE_TEST_LUMEN_PACKAGE");
        if (!themePackage.isEmpty()) {
            const auto theme = call("extensions.install", {{"path", themePackage}});
            QVERIFY2(theme, qPrintable(theme ? QString() : theme.error().message));
            QVERIFY(call("extensions.enable", {{"id", "nekotune.theme-lumen"}, {"trusted", true}}));
        }
        manager.shutdown();
    }
    void generationsSurviveBackendRecreation() {
        quint64 previous = 0;
        for (int i = 0; i < 2; ++i) {
            auto buffer = std::make_unique<PcmCaptureBuffer>();
            buffer->reset(1);
            buffer->setEnabled(true);
            const std::vector<float> samples(1024, .25f);
            buffer->append(samples, 0);
            buffer->advance(512, 48000, 1, 10);
            const auto chunks = buffer->take(1);
            QCOMPARE(chunks.size(), 1);
            QVERIFY(chunks[0].generation > previous);
            previous = chunks[0].generation;
        }
    }
    void subscriptionsAndBackendFallback() {
        CaptureAudio audio;
        PcmService service(audio);
        QVERIFY(!audio.enabled);
        int first = 0, second = 0;
        auto a = service.subscribe([&](const auto &) { ++first; });
        auto b = service.subscribe([&](const auto &) { ++second; });
        QVERIFY(a && b && audio.enabled);
        QCOMPARE(service.subscriberCount(), 2);
        audio.pending.append(sdk::PcmBlock{});
        QTRY_COMPARE(first, 1);
        QCOMPARE(second, 1);
        a.reset();
        audio.pending.append(sdk::PcmBlock{});
        QTRY_COMPARE(second, 2);
        QCOMPARE(first, 1);
        audio.supported = false;
        emit audio.backendChanged();
        QVERIFY(!audio.enabled);
        QVERIFY(!service.capabilities().available);
        QCOMPARE(service.capabilities().reason, QString("playback_backend_unsupported"));
        audio.supported = true;
        emit audio.backendChanged();
        QVERIFY(audio.enabled);
        b.reset();
        QVERIFY(!audio.enabled);
        QCOMPARE(service.subscriberCount(), 0);
    }
};
QTEST_GUILESS_MAIN(PcmCaptureTest)
#include "pcm_capture_test.moc"
