#include "infrastructure/playback/pcm_decoder.h"
#include "infrastructure/playback/pcm_renderer.h"
#include "infrastructure/playback/pcm_dsp.h"
#include "infrastructure/playback/pcm_output_clock.h"
#include "infrastructure/playback/playback_backend_supervisor.h"
#include "application/playback/audio_settings_service.h"
#include "domain/playback/transition_policy.h"
#include "ipc/serialization/audio_serialization.h"
#include "support/audio_backend.h"
#include "support/store_fixture.h"
#include <QFile>
#include <QSignalSpy>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtEndian>
#include <QtTest>
#include <bit>
#include <cmath>
#include <numbers>
#include <limits>
using namespace nekotune;
namespace {
std::vector<float> wave(int frames, int rate, double frequency = 997, double amplitude = .25, int offset = 0) {
    std::vector<float> result(size_t(frames) * 2);
    for (int i = 0; i < frames; ++i) {
        const float sample = float(amplitude * std::sin(2 * std::numbers::pi * frequency * (i + offset) / rate));
        result[size_t(i) * 2] = sample;
        result[size_t(i) * 2 + 1] = sample;
    }
    return result;
}
bool wav(const QString &path, int rate, const std::vector<float> &samples, int channels = 2) {
    QByteArray bytes;
    auto u16 = [&](quint16 value) {
        auto v = qToLittleEndian(value);
        bytes.append(reinterpret_cast<const char *>(&v), 2);
    };
    auto u32 = [&](quint32 value) {
        auto v = qToLittleEndian(value);
        bytes.append(reinterpret_cast<const char *>(&v), 4);
    };
    const auto size = quint32(samples.size() * sizeof(float));
    bytes.append("RIFF", 4);
    u32(size + 36);
    bytes.append("WAVEfmt ", 8);
    u32(16);
    u16(3);
    u16(quint16(channels));
    u32(quint32(rate));
    u32(quint32(rate * channels * 4));
    u16(quint16(channels * 4));
    u16(32);
    bytes.append("data", 4);
    u32(size);
    for (float sample : samples)
        u32(std::bit_cast<quint32>(sample));
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
std::vector<float> render(PcmRenderer &renderer, std::vector<PcmBoundary> *boundaries = nullptr) {
    std::vector<float> output;
    renderer.releasePlanWait();
    renderer.finishAfterCurrent();
    QElapsedTimer timer;
    timer.start();
    while (!renderer.finished() && timer.elapsed() < 5000) {
        auto chunk = renderer.render(257);
        output.insert(output.end(), chunk.begin(), chunk.end());
        auto events = renderer.takeBoundaries();
        for (const auto &event : events)
            if (event.token)
                renderer.finishAfterCurrent();
        if (boundaries)
            boundaries->insert(boundaries->end(), events.begin(), events.end());
        if (chunk.empty())
            QTest::qSleep(1);
    }
    return output;
}
double rms(std::span<const float> samples) {
    double energy = 0;
    for (float sample : samples)
        energy += double(sample) * sample;
    return std::sqrt(energy / std::max<size_t>(1, samples.size()));
}
class DeferredResolver : public ISourceResolver {
  public:
    std::vector<Completion> pending;
    QList<QUrl> released;
    void resolve(const SongMetadata &, Completion done) override { pending.push_back(std::move(done)); }
    void release(const QUrl &url) override { released.push_back(url); }
    void complete(size_t index, const QUrl &url) {
        auto callback = std::move(pending.at(index));
        callback(url);
    }
};
class TransitionAudio : public TestAudioBackend {
  public:
    QUrl url;
    PreloadRequest request;
    int loads = 0, stops = 0, plays = 0;
    QString name = "ffmpeg";
    qint64 clock = 0;
    bool preloading = false;
    TransitionAudio() { rateSupported = false; }
    QString backendName() const override { return name; }
    BackendCapabilities backendCapabilities() const override { return {true, true, true}; }
    void setSource(const QUrl &value) override {
        url = value;
        ++loads;
        clock = 0;
        if (!value.isEmpty())
            emit metadataChanged();
    }
    QUrl source() const override { return url; }
    void play() override {
        ++plays;
        emit stateChanged(PlayerState::Playing);
    }
    void pause() override { emit stateChanged(PlayerState::Paused); }
    void stop() override {
        ++stops;
        emit stateChanged(PlayerState::Stopped);
    }
    void seek(qint64 value) override {
        clock = value;
        emit seeked(value);
    }
    bool seekable() const override { return true; }
    void setVolume(double) override {}
    double volume() const override { return .8; }
    qint64 position() const override { return clock; }
    qint64 duration() const override { return 1000; }
    AudioMetadata metadata() const override { return {url.fileName(), "artist", "album"}; }
    void preloadNext(const PreloadRequest &value) override {
        request = value;
        preloading = true;
    }
    void cancelPreload() override { preloading = false; }
    void start() {
        url = request.source;
        preloading = false;
        emit nextTrackStarted(request.token);
    }
};
} // namespace
class AudioPipelineTest : public QObject {
    Q_OBJECT
  private slots:
    void compressedPadding_data() {
        QTest::addColumn<QString>("name");
        for (const auto &name : {"trim.flac", "trim.mp3", "trim.m4a", "trim.opus"})
            QTest::newRow(name) << QString::fromUtf8(name);
    }
    void compressedPadding() {
        QFETCH(QString, name);
        const auto url =
            QUrl::fromLocalFile(QStringLiteral(NEKOTUNE_SOURCE_DIR) + "/tests/fixtures/audio-pipeline/" + name);
        auto decoder = std::make_shared<PcmDecoder>(url);
        QTRY_VERIFY_WITH_TIMEOUT(decoder->info().eof, 3000);
        QVERIFY2(decoder->info().error.isEmpty(), qPrintable(decoder->info().error));
        QCOMPARE(decoder->info().sampleRate, 48000);
        QCOMPARE(decoder->available(), size_t(16800));
        PcmRenderer renderer(48000, decoder);
        const auto samples = render(renderer);
        QCOMPARE(samples.size(), size_t(33600));
        QVERIFY(rms(samples) > .01);
        PcmDecoder seek(url, 100);
        QTRY_VERIFY_WITH_TIMEOUT(seek.info().eof, 3000);
        QVERIFY2(seek.info().error.isEmpty(), qPrintable(seek.info().error));
        QCOMPARE(seek.available(), size_t(12000));
    }
    void capturedOutputClockDefersTrackIdentity() {
        QTemporaryDir dir;
        QVERIFY(wav(dir.filePath("a.wav"), 48000, wave(2048, 48000)));
        QVERIFY(wav(dir.filePath("b.wav"), 48000, wave(2048, 48000, 997, .25, 2048)));
        auto a = std::make_shared<PcmDecoder>(QUrl::fromLocalFile(dir.filePath("a.wav")));
        auto b = std::make_shared<PcmDecoder>(QUrl::fromLocalFile(dir.filePath("b.wav")));
        QTRY_VERIFY_WITH_TIMEOUT(a->info().eof && b->info().eof, 3000);
        PcmRenderer renderer(48000, a);
        renderer.preload(b, {b->source, 42, TransitionStrategy::Gapless, 0});
        renderer.authorize(42);
        PcmDsp dsp(48000);
        PcmOutputClock clock;
        std::vector<float> captured;
        renderer.finishAfterCurrent();
        while (!renderer.finished()) {
            auto pcm = dsp.process(renderer.render(511));
            captured.insert(captured.end(), pcm.begin(), pcm.end());
            auto events = renderer.takeBoundaries();
            for (const auto &event : events)
                if (event.token)
                    renderer.finishAfterCurrent();
            clock.append(std::move(events));
        }
        auto tail = dsp.finish();
        captured.insert(captured.end(), tail.begin(), tail.end());
        QCOMPARE(captured.size(), size_t(4096 * 2));
        QVERIFY(clock.advance(0).empty());
        QVERIFY(clock.advance(2047).empty());
        auto started = clock.advance(2048);
        QCOMPARE(started.size(), size_t(2));
        QCOMPARE(started.front().token, quint64(42));
        QVERIFY(clock.advance(1000).empty()); // A paused/backward device clock cannot replay a boundary.
        auto ended = clock.advance(4096);
        QCOMPARE(ended.size(), size_t(1));
        QVERIFY(ended.front().ended);
    }
    void controlledFallbackAndNewSourceRetry() {
        TransitionAudio *preferred = nullptr, *fallback = nullptr;
        int preferredInstances = 0, fallbackInstances = 0;
        PlaybackBackendSupervisor backend(
            "ffmpeg",
            [&]() -> std::unique_ptr<IPlaybackBackend> {
                auto audio = std::make_unique<TransitionAudio>();
                preferred = audio.get();
                ++preferredInstances;
                return audio;
            },
            [&]() -> std::unique_ptr<IPlaybackBackend> {
                auto audio = std::make_unique<TransitionAudio>();
                audio->name = "qt";
                fallback = audio.get();
                ++fallbackInstances;
                return audio;
            });
        backend.setAudioOutput("c3BlYWtlcg==");
        backend.setVolume(.3);
        const QUrl url = QUrl::fromLocalFile("/music/unsupported.audio");
        backend.setSource(url);
        backend.play();
        QSignalSpy errors(&backend, &IPlaybackBackend::failed), released(&backend, &IPlaybackBackend::sourceReleased);
        emit preferred->decoderFailure("unsupported", true);
        QTRY_COMPARE(backend.backendName(), QString("qt"));
        QCOMPARE(fallbackInstances, 1);
        QCOMPARE(backend.source(), url);
        QCOMPARE(fallback->plays, 1);
        QCOMPARE(errors.size(), 0);
        emit fallback->decoderFailure("also unsupported", true);
        QCOMPARE(errors.size(), 1);
        QCOMPARE(fallbackInstances, 1);
        backend.setSource(QUrl::fromLocalFile("/music/new.wav"));
        QCOMPARE(preferredInstances, 2);
        QCOMPARE(backend.backendName(), QString("ffmpeg"));
        emit preferred->decoderFailure("network error", false);
        QCOMPARE(errors.size(), 2);
        QCOMPARE(fallbackInstances, 1);
        QVERIFY(!released.isEmpty());
    }
    void nativeDecodeAndSeek() {
        QTemporaryDir dir;
        const auto samples = wave(48000, 48000);
        QVERIFY(wav(dir.filePath("wave.wav"), 48000, samples));
        PcmDecoder decoder(QUrl::fromLocalFile(dir.filePath("wave.wav")));
        QTRY_VERIFY_WITH_TIMEOUT(decoder.info().eof, 3000);
        QVERIFY2(decoder.info().error.isEmpty(), qPrintable(decoder.info().error));
        QCOMPARE(decoder.available(), size_t(48000));
        std::vector<float> decoded(samples.size());
        QCOMPARE(decoder.read(decoded.data(), 48000), size_t(48000));
        QVERIFY(decoded == samples);
        auto seek = std::make_shared<PcmDecoder>(QUrl::fromLocalFile(dir.filePath("wave.wav")), 500);
        QTRY_VERIFY_WITH_TIMEOUT(seek->info().eof, 3000);
        PcmRenderer renderer(48000, seek);
        const auto tail = render(renderer);
        QCOMPARE(tail.size(), size_t(24000 * 2));
        QVERIFY(std::equal(tail.begin(), tail.end(), samples.begin() + 48000));
    }
    void continuousResampling_data() {
        QTest::addColumn<int>("inputRate");
        QTest::newRow("native") << 48000;
        QTest::newRow("fractional") << 44100;
        QTest::newRow("upsample") << 32000;
    }
    void continuousResampling() {
        QFETCH(int, inputRate);
        QTemporaryDir dir;
        const int split = inputRate / 3 + 7;
        const auto all = wave(inputRate + 133, inputRate);
        const std::vector<float> a(all.begin(), all.begin() + split * 2), b(all.begin() + split * 2, all.end());
        QVERIFY(wav(dir.filePath("a.wav"), inputRate, a));
        QVERIFY(wav(dir.filePath("b.wav"), inputRate, b));
        QVERIFY(wav(dir.filePath("all.wav"), inputRate, all));
        auto first = std::make_shared<PcmDecoder>(QUrl::fromLocalFile(dir.filePath("a.wav")));
        auto next = std::make_shared<PcmDecoder>(QUrl::fromLocalFile(dir.filePath("b.wav")));
        auto full = std::make_shared<PcmDecoder>(QUrl::fromLocalFile(dir.filePath("all.wav")));
        QTRY_VERIFY_WITH_TIMEOUT(first->info().eof && next->info().eof && full->info().eof, 3000);
        PcmRenderer joined(48000, first), reference(48000, full);
        joined.preload(next, {next->source, 81, TransitionStrategy::Gapless, 0});
        joined.authorize(81);
        std::vector<PcmBoundary> events;
        const auto actual = render(joined, &events), expected = render(reference);
        QCOMPARE(actual.size(), expected.size());
        double error = 0;
        for (size_t i = 0; i < expected.size(); ++i)
            error = std::max(error, std::abs(double(actual[i]) - expected[i]));
        QVERIFY2(error < 1e-6, qPrintable(QString::number(error, 'g', 10)));
        const auto boundary =
            std::find_if(events.begin(), events.end(), [](const auto &event) { return event.token == 81; });
        QVERIFY(boundary != events.end());
        QCOMPARE(boundary->frame, qint64(std::llround(double(split) * 48000 / inputRate)));
    }
    void threeShortTracksPreserveResamplerState() {
        QTemporaryDir dir;
        constexpr int rate = 44100, count = 3007;
        const auto whole = wave(count * 3, rate);
        std::array<std::shared_ptr<PcmDecoder>, 3> tracks;
        for (int i = 0; i < 3; ++i) {
            const auto path = dir.filePath(QString::number(i) + ".wav");
            QVERIFY(wav(path, rate, {whole.begin() + i * count * 2, whole.begin() + (i + 1) * count * 2}));
            tracks[size_t(i)] = std::make_shared<PcmDecoder>(QUrl::fromLocalFile(path));
        }
        QVERIFY(wav(dir.filePath("whole.wav"), rate, whole));
        auto full = std::make_shared<PcmDecoder>(QUrl::fromLocalFile(dir.filePath("whole.wav")));
        QTRY_VERIFY_WITH_TIMEOUT(
            tracks[0]->info().eof && tracks[1]->info().eof && tracks[2]->info().eof && full->info().eof, 3000);
        PcmRenderer renderer(48000, tracks[0]), reference(48000, full);
        renderer.preload(tracks[1], {tracks[1]->source, 1, TransitionStrategy::Gapless, 0});
        renderer.authorize(1);
        std::vector<float> actual;
        QElapsedTimer timer;
        timer.start();
        while (!renderer.finished() && timer.elapsed() < 3000) {
            auto pcm = renderer.render(257);
            actual.insert(actual.end(), pcm.begin(), pcm.end());
            for (const auto &boundary : renderer.takeBoundaries()) {
                if (boundary.token == 1) {
                    renderer.preload(tracks[2], {tracks[2]->source, 2, TransitionStrategy::Gapless, 0});
                    renderer.authorize(2);
                } else if (boundary.token == 2)
                    renderer.finishAfterCurrent();
            }
        }
        QVERIFY(renderer.finished());
        const auto expected = render(reference);
        QCOMPARE(actual.size(), expected.size());
        for (size_t i = 0; i < actual.size(); ++i)
            QVERIFY(std::abs(actual[i] - expected[i]) < 1e-6);
    }
    void differentRatesAndMonoLayoutHaveNoExtraFrames() {
        QTemporaryDir dir;
        QVERIFY(wav(dir.filePath("stereo.wav"), 44100, wave(11025, 44100)));
        const auto stereo = wave(4800, 48000);
        std::vector<float> mono;
        for (size_t i = 0; i < stereo.size(); i += 2)
            mono.push_back(stereo[i]);
        QVERIFY(wav(dir.filePath("mono.wav"), 48000, mono, 1));
        auto a = std::make_shared<PcmDecoder>(QUrl::fromLocalFile(dir.filePath("stereo.wav")));
        auto b = std::make_shared<PcmDecoder>(QUrl::fromLocalFile(dir.filePath("mono.wav")));
        QTRY_VERIFY_WITH_TIMEOUT(a->info().eof && b->info().eof, 3000);
        PcmRenderer renderer(48000, a);
        renderer.preload(b, {b->source, 8, TransitionStrategy::Gapless, 0});
        renderer.authorize(8);
        std::vector<PcmBoundary> boundaries;
        const auto samples = render(renderer, &boundaries);
        QCOMPARE(samples.size(), size_t((12000 + 4800) * 2));
        const auto boundary =
            std::find_if(boundaries.begin(), boundaries.end(), [](const auto &event) { return event.token == 8; });
        QVERIFY(boundary != boundaries.end());
        QCOMPARE(boundary->frame, qint64(12000));
        QVERIFY(rms(std::span(samples).subspan(24000)) > .05);
    }
    void crossfadeAndShortTracks() {
        QTemporaryDir dir;
        std::vector<float> first(48000, .5f), next(48000, .5f);
        QVERIFY(wav(dir.filePath("a.wav"), 48000, first));
        QVERIFY(wav(dir.filePath("b.wav"), 48000, next));
        auto a = std::make_shared<PcmDecoder>(QUrl::fromLocalFile(dir.filePath("a.wav")));
        auto b = std::make_shared<PcmDecoder>(QUrl::fromLocalFile(dir.filePath("b.wav")));
        QTRY_VERIFY_WITH_TIMEOUT(a->info().eof && b->info().eof, 3000);
        PcmRenderer renderer(48000, a);
        renderer.preload(b, {b->source, 19, TransitionStrategy::Crossfade, 12});
        renderer.authorize(19);
        std::vector<PcmBoundary> boundaries;
        const auto samples = render(renderer, &boundaries);
        QCOMPARE(samples.size(), size_t((24000 * 2 - 12000) * 2));
        const auto boundary =
            std::find_if(boundaries.begin(), boundaries.end(), [](const auto &event) { return event.token == 19; });
        QVERIFY(boundary != boundaries.end());
        QCOMPARE(boundary->frame, qint64(12001));
        QCOMPARE(samples[12000 * 2], .5f);
        QVERIFY(std::abs(samples[18000 * 2] - float(std::sqrt(.5))) < .0001);
        QCOMPARE(samples[24000 * 2], .5f);
    }
    void failedPreloadPreservesCurrentAndCancelUnblocksDecoder() {
        QTemporaryDir dir;
        const auto original = wave(12000, 48000);
        QVERIFY(wav(dir.filePath("ok.wav"), 48000, original));
        auto a = std::make_shared<PcmDecoder>(QUrl::fromLocalFile(dir.filePath("ok.wav")));
        auto bad = std::make_shared<PcmDecoder>(QUrl::fromLocalFile(dir.filePath("absent.wav")));
        QTRY_VERIFY_WITH_TIMEOUT(a->info().eof && bad->info().eof, 3000);
        PcmRenderer renderer(48000, a);
        renderer.preload(bad, {bad->source, 1, TransitionStrategy::Gapless, 0});
        renderer.authorize(1);
        QCOMPARE(render(renderer), original);
        const auto big = wave(48000 * 3, 48000);
        QVERIFY(wav(dir.filePath("big.wav"), 48000, big));
        QElapsedTimer timer;
        timer.start();
        {
            PcmDecoder bounded(QUrl::fromLocalFile(dir.filePath("big.wav")), 0, 32768);
            QTRY_VERIFY_WITH_TIMEOUT(bounded.available() > 0, 3000);
            bounded.cancel();
        }
        QVERIFY(timer.elapsed() < 3000);
    }
    void dspResponseBypassAndLimiter() {
        PcmDsp dsp(48000);
        QVERIFY2(dsp.error().isEmpty(), qPrintable(dsp.error()));
        EqualizerSettings eq;
        eq.enabled = true;
        eq.autoHeadroom = false;
        eq.gains[5] = 6;
        dsp.setSettings(eq);
        const auto input = wave(48000, 48000, 1000, .05);
        auto boosted = dsp.process(input);
        QVERIFY2(dsp.error().isEmpty(), qPrintable(dsp.error()));
        QVERIFY(boosted.size() > 40000);
        const double gain =
            20 * std::log10(rms(std::span(boosted).subspan(10000)) / rms(std::span(input).subspan(10000)));
        QVERIFY2(std::abs(gain - 6) < .25, qPrintable(QString::number(gain)));
        eq.enabled = false;
        dsp.setSettings(eq);
        auto bypassed = dsp.process(input);
        QVERIFY(std::abs(20 * std::log10(rms(std::span(bypassed).subspan(10000)) /
                                         rms(std::span(input).subspan(10000)))) < .1);
        auto loud = wave(12000, 48000, 1000, 2);
        auto limited = dsp.process(loud);
        auto tail = dsp.finish();
        limited.insert(limited.end(), tail.begin(), tail.end());
        for (float sample : limited)
            QVERIFY(std::isfinite(sample) && std::abs(sample) <= 1.00001f);
        QCOMPARE(boosted.size() + bypassed.size() + limited.size(), input.size() * 2 + loud.size());
    }
    void strictAlbumPolicyAndSettings() {
        SongMetadata a, b;
        a.album = b.album = "Album";
        a.albumArtist = b.albumArtist = "Artist";
        a.trackNumber = 1;
        b.trackNumber = 2;
        QCOMPARE(transitionStrategyFor(a, b, PlaybackMode::Sequential, false, 5), TransitionStrategy::Gapless);
        QCOMPARE(transitionStrategyFor(a, b, PlaybackMode::Shuffle, false, 5), TransitionStrategy::Crossfade);
        QCOMPARE(transitionStrategyFor(a, b, PlaybackMode::Sequential, true, 5), TransitionStrategy::Crossfade);
        b.trackNumber = 0;
        QCOMPARE(transitionStrategyFor(a, b, PlaybackMode::Sequential, false, 5), TransitionStrategy::Crossfade);
        auto settings = parseAudioSettings(
            {{"equalizer", QJsonObject{{"preset", "bass"}, {"enabled", true}}}, {"crossfade_seconds", 5}});
        QVERIFY(settings);
        QCOMPARE(settings.value().equalizer.gains[0], 6.0);
        QVERIFY(!parseAudioSettings({{"crossfade_seconds", 13}}));
        QVERIFY(!parseAudioSettings({{"equalizer", QJsonObject{{"gains", QJsonArray{1, 2}}}}}));
        auto config = settings.value();
        config.equalizer.preampDb = std::numeric_limits<double>::quiet_NaN();
        QVERIFY(!validateAudioSettings(config));
        QCOMPARE(parseAudioSettings(audioSettingsJson(settings.value())).value(), settings.value());
    }
    void engineCommitsOnlyAtBoundaryAndRollsBackStorageFailure() {
        QTemporaryDir dir;
        StoreFixture store(dir.filePath("queue.db"));
        QVERIFY(store.isReady());
        const auto song = store.getOrCreateSong("same", "/music/test.wav");
        QVERIFY(song);
        QueueService queue(store.queueRepo, store.songRepo, store.db);
        TransitionAudio audio;
        PlayerEngine player(audio, queue);
        PlayerQueue items;
        const int first = items.add("/music/a.wav", *song), second = items.add("/music/b.wav", *song);
        items.setCurrentIndex(0);
        QSignalSpy counted(&player, &PlayerEngine::songStarted);
        QVERIFY(player.replaceQueue(items));
        QCOMPARE(counted.size(), 1);
        QVERIFY(audio.preloading);
        QCOMPARE(player.snapshot().song->id, first);
        QCOMPARE(store.loadQueue().currentIndex, 0);
        const int loads = audio.loads, stops = audio.stops;
        audio.start();
        QCOMPARE(player.snapshot().song->id, second);
        QCOMPARE(store.loadQueue().currentIndex, 1);
        QCOMPARE(audio.loads, loads);
        QCOMPARE(audio.stops, stops);
        QCOMPARE(counted.size(), 2);
        QVERIFY(player.setPlaybackMode(PlaybackMode::RepeatAll));
        QVERIFY(audio.preloading);
        QSqlQuery query(store.db.database());
        QVERIFY(query.exec(
            "CREATE TRIGGER reject_navigation BEFORE DELETE ON queue_items BEGIN SELECT RAISE(FAIL, 'blocked'); END"));
        QSignalSpy errors(&player, &PlayerEngine::errorOccurred);
        audio.start();
        QCOMPARE(player.snapshot().song->id, second);
        QCOMPARE(store.loadQueue().currentIndex, 1);
        QCOMPARE(counted.size(), 2);
        QCOMPARE(errors.size(), 1);
        QVERIFY(player.snapshot().state == PlayerState::Error);
    }
    void asyncPreloadCancellationAndLeases() {
        QTemporaryDir dir;
        StoreFixture store(dir.filePath("remote.db"));
        SongMetadata a;
        a.providerId = "fixture/music";
        a.providerTrackId = "a";
        a.customTitle = "A";
        SongMetadata b = a;
        b.providerTrackId = "b";
        const auto first = store.songRepo.getOrCreateRemote(a), next = store.songRepo.getOrCreateRemote(b);
        QVERIFY(first);
        QVERIFY(next);
        QueueService queue(store.queueRepo, store.songRepo, store.db);
        DeferredResolver resolver;
        TransitionAudio audio;
        PlayerEngine player(audio, queue);
        player.setSourceResolver(&resolver);
        PlayerQueue items;
        items.add("", *first);
        items.add("", *next);
        items.setCurrentIndex(0);
        QVERIFY(player.replaceQueue(items));
        QCOMPARE(resolver.pending.size(), size_t(1));
        const QUrl active("http://127.0.0.1/active"), stale("http://127.0.0.1/stale"),
            replacement("http://127.0.0.1/new");
        resolver.complete(0, active);
        QCOMPARE(resolver.pending.size(), size_t(2));
        QVERIFY(player.next());
        QCOMPARE(resolver.pending.size(), size_t(3));
        resolver.complete(1, stale);
        QVERIFY(resolver.released.contains(stale));
        QVERIFY(!audio.preloading);
        resolver.complete(2, replacement);
        QCOMPARE(audio.source(), replacement);
        QCOMPARE(player.snapshot().song->metadata.id, next->id);
        player.shutdown();
        QVERIFY(resolver.released.contains(active));
        QVERIFY(resolver.released.contains(replacement));
    }
    void repeatOneUsesNewTokensAndRetiresAfterTail() {
        QTemporaryDir dir;
        StoreFixture store(dir.filePath("repeat.db"));
        SongMetadata remote;
        remote.providerId = "fixture/music";
        remote.providerTrackId = "repeat";
        const auto song = store.songRepo.getOrCreateRemote(remote);
        QVERIFY(song);
        QueueService queue(store.queueRepo, store.songRepo, store.db);
        DeferredResolver resolver;
        TransitionAudio audio;
        PlayerEngine player(audio, queue, std::make_unique<ShuffleBagStrategy>(), PlaybackMode::RepeatOne);
        player.setSourceResolver(&resolver);
        PlayerQueue items;
        const int id = items.add("", *song);
        items.setCurrentIndex(0);
        QSignalSpy starts(&player, &PlayerEngine::songStarted);
        QVERIFY(player.replaceQueue(items));
        const QUrl current("http://127.0.0.1/current"), incoming("http://127.0.0.1/incoming");
        resolver.complete(0, current);
        resolver.complete(1, incoming);
        const auto firstToken = audio.request.token;
        QCOMPARE(audio.request.strategy, TransitionStrategy::Gapless);
        audio.start();
        QCOMPARE(player.snapshot().song->id, id);
        QCOMPARE(starts.size(), 2);
        QVERIFY(!resolver.released.contains(current));
        emit audio.sourceReleased(current);
        QVERIFY(resolver.released.contains(current));
        QCOMPARE(resolver.pending.size(), size_t(3));
        resolver.complete(2, QUrl("http://127.0.0.1/third"));
        QVERIFY(audio.request.token != firstToken);
        player.shutdown();
    }
    void pausedModeChangeAndTopologyInvalidateReservation() {
        QTemporaryDir dir;
        StoreFixture store(dir.filePath("mode.db"));
        const auto song = store.getOrCreateSong("same", "/music/test.wav");
        QVERIFY(song);
        QueueService queue(store.queueRepo, store.songRepo, store.db);
        TransitionAudio audio;
        PlayerEngine player(audio, queue);
        PlayerQueue items;
        items.add("/music/a.wav", *song);
        items.add("/music/b.wav", *song);
        items.setCurrentIndex(0);
        QVERIFY(player.replaceQueue(items));
        QVERIFY(audio.preloading);
        const auto token = audio.request.token;
        auto changed = queue.queue();
        changed.add("/music/c.wav", *song);
        QVERIFY(queue.commit(changed));
        QVERIFY(audio.request.token != token);
        QVERIFY(audio.preloading);
        QVERIFY(player.pause());
        QVERIFY(player.setPlaybackMode(PlaybackMode::RepeatOne));
        QVERIFY(!audio.preloading);
        QVERIFY(player.play());
        QVERIFY(audio.preloading);
        QCOMPARE(audio.request.source, audio.source());
        const int loads = audio.loads;
        emit audio.nextFailed(audio.request.token, "failed preload");
        QCOMPARE(audio.loads, loads);
        QCOMPARE(queue.queue().size(), 3);
        QCOMPARE(player.snapshot().song->id, items.at(0).id);
    }
    void outputDisconnectClearsReservationWithoutResuming() {
        QTemporaryDir dir;
        StoreFixture store(dir.filePath("devices.db"));
        const auto song = store.getOrCreateSong("same", "/music/test.wav");
        QVERIFY(song);
        QueueService queue(store.queueRepo, store.songRepo, store.db);
        TransitionAudio audio;
        PlayerEngine player(audio, queue);
        PlayerQueue items;
        items.add("/music/a.wav", *song);
        items.add("/music/b.wav", *song);
        items.setCurrentIndex(0);
        QVERIFY(player.replaceQueue(items));
        QVERIFY(audio.preloading);
        audio.setDevices({});
        QVERIFY(!audio.preloading);
        QVERIFY(!player.playing());
        audio.setDevices({{"c3BlYWtlcg==", "Speaker", true}});
        audio.setAudioOutput("c3BlYWtlcg==");
        QVERIFY(!player.playing());
        QVERIFY(player.play());
        QVERIFY(audio.preloading);
    }
    void settingsPersistenceFailureAndRestartPreference() {
        QTemporaryDir dir;
        StoreFixture store(dir.filePath("settings.db"));
        QueueService queue(store.queueRepo, store.songRepo, store.db);
        TransitionAudio audio;
        PlayerEngine player(audio, queue);
        bool save = false;
        AudioSettingsService service(audio, player, {}, [&](const AudioSettings &) { return save; });
        auto settings = service.settings();
        settings.backend = "qt";
        settings.crossfadeSeconds = 4;
        QVERIFY(!service.update(settings));
        QVERIFY(!service.restartRequired());
        QCOMPARE(service.settings().backend, QString("ffmpeg"));
        save = true;
        QVERIFY(service.update(settings));
        QVERIFY(service.restartRequired());
        QCOMPARE(service.activeBackend(), QString("ffmpeg"));
        settings.backend = "ffmpeg";
        QVERIFY(service.update(settings));
        QVERIFY(!service.restartRequired());
    }
};
QTEST_GUILESS_MAIN(AudioPipelineTest)
#include "audio_pipeline_test.moc"
