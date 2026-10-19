#include "application/playback/audio_settings_service.h"
#include "infrastructure/library/audio_duration.h"
#include "infrastructure/library/audio_reference.h"
#include "infrastructure/library/loudness_analysis.h"
#include "infrastructure/playback/pcm_decoder.h"
#include "infrastructure/playback/pcm_dsp.h"
#include "infrastructure/playback/pcm_renderer.h"
#include "infrastructure/playback/playback_backend_supervisor.h"
#include "ipc/serialization/audio_serialization.h"
#include "runtime/backend_runtime.h"
#include "support/audio_backend.h"
#include "support/store_fixture.h"
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocalSocket>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtEndian>
#include <QtTest>
#include <bit>
#include <numbers>
extern "C" {
#include <libavutil/dict.h>
}
using namespace nekotune;
namespace {
bool wavSamples(const QString &path, const std::vector<float> &samples) {
    const int frames = int(samples.size() / 2);
    QByteArray bytes;
    auto u16 = [&](quint16 n) {
        auto value = qToLittleEndian(n);
        bytes.append(reinterpret_cast<const char *>(&value), 2);
    };
    auto u32 = [&](quint32 n) {
        auto value = qToLittleEndian(n);
        bytes.append(reinterpret_cast<const char *>(&value), 4);
    };
    bytes.append("RIFF");
    u32(quint32(frames * 8 + 36));
    bytes.append("WAVEfmt ");
    u32(16);
    u16(3);
    u16(2);
    u32(48000);
    u32(48000 * 8);
    u16(8);
    u16(32);
    bytes.append("data");
    u32(quint32(frames * 8));
    for (float value : samples)
        u32(std::bit_cast<quint32>(value));
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
bool wav(const QString &path, double amplitude, int frames = 96000, bool constant = false) {
    std::vector<float> samples(size_t(frames) * 2);
    for (int i = 0; i < frames; ++i)
        samples[size_t(i) * 2] = samples[size_t(i) * 2 + 1] =
            float(amplitude * (constant ? 1 : std::sin(2 * std::numbers::pi * 997 * i / 48000)));
    return wavSamples(path, samples);
}
QByteArray hash(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    QCryptographicHash digest(QCryptographicHash::Sha256);
    digest.addData(&file);
    return digest.result();
}
std::vector<float> render(PcmRenderer &renderer) {
    std::vector<float> output;
    renderer.finishAfterCurrent();
    renderer.releasePlanWait();
    QElapsedTimer time;
    time.start();
    while (!renderer.finished() && time.elapsed() < 10000) {
        auto chunk = renderer.render(257);
        output.insert(output.end(), chunk.begin(), chunk.end());
        if (chunk.empty())
            QTest::qSleep(1);
    }
    return output;
}
double rms(const std::vector<float> &samples) {
    double sum = 0;
    for (float sample : samples)
        sum += double(sample) * sample;
    return std::sqrt(sum / std::max<size_t>(1, samples.size()));
}
class Peer {
  public:
    QLocalSocket socket;
    QByteArray buffer;
    QList<QJsonObject> events;
    int id = 0;
    QJsonObject call(const QString &method, const QJsonObject &params = {}) {
        socket.write(QJsonDocument(QJsonObject{{"id", ++id}, {"method", method}, {"params", params}})
                         .toJson(QJsonDocument::Compact) +
                     '\n');
        socket.flush();
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < 5000) {
            buffer += socket.readAll();
            while (buffer.contains('\n')) {
                const auto end = buffer.indexOf('\n');
                const auto reply = QJsonDocument::fromJson(buffer.left(end)).object();
                buffer.remove(0, end + 1);
                if (reply.value("id").toInt(-1) == id)
                    return reply;
                events.append(reply);
            }
            socket.waitForReadyRead(20);
        }
        return {};
    }
};
class Audio final : public TestAudioBackend {
  public:
    bool supported = true;
    QUrl url;
    ReplayGainSettings gainSettings;
    ReplayGainData gainData;
    double level = .8;
    QString backendName() const override { return supported ? "ffmpeg" : "qt"; }
    BackendCapabilities backendCapabilities() const override { return {supported, supported, supported, supported}; }
    void setSource(const QUrl &source) override { url = source; }
    QUrl source() const override { return url; }
    void play() override {}
    void pause() override {}
    void stop() override {}
    void seek(qint64) override {}
    void setVolume(double volume) override { level = volume; }
    double volume() const override { return level; }
    qint64 position() const override { return 0; }
    qint64 duration() const override { return 0; }
    AudioMetadata metadata() const override { return {}; }
    void setReplayGain(const ReplayGainSettings &settings) override { gainSettings = settings; }
    ReplayGainData replayGainData() const override { return gainData; }
};
} // namespace
class ReplayGainTest : public QObject {
    Q_OBJECT
    QTemporaryDir home;
  private slots:
    void initTestCase() {
        QVERIFY(home.isValid());
        qputenv("NEKOTUNE_HOME", home.path().toUtf8());
    }
    void tagsAndMissingMode() {
        AVDictionary *container = nullptr, *stream = nullptr;
        av_dict_set(&container, "REPLAYGAIN_TRACK_GAIN", "-6.0 dB", 0);
        av_dict_set(&container, "REPLAYGAIN_ALBUM_GAIN", "-3.0 dB", 0);
        av_dict_set(&stream, "replaygain_track_gain", " +2.0 dB ", 0);
        av_dict_set(&stream, "REPLAYGAIN_TRACK_PEAK", "0.5", 0);
        av_dict_set(&stream, "REPLAYGAIN_ALBUM_GAIN", "nan", 0);
        auto data = readReplayGainTags(container, stream);
        QCOMPARE(data.trackGainDb, std::optional(2.0));
        QCOMPARE(data.albumGainDb, std::optional(-3.0));
        ReplayGainData cache{9, 8, .2, .2, "analysis", "analysis"};
        QCOMPARE(withReplayGainFallback(data, cache), data);
        QCOMPARE(resolveReplayGain({"track", 1}, data).gainDb, 3.0);
        QCOMPARE(resolveReplayGain({"album", 1}, data).gainDb, -2.0);
        const auto limited = resolveReplayGain({"track", 12}, data);
        QVERIFY(limited.peakLimited);
        QVERIFY(std::abs(limited.gainDb - 6.0206) < .001);
        data.albumGainDb.reset();
        QCOMPARE(resolveReplayGain({"album", 12}, data).gainDb, 0.0);
        QCOMPARE(resolveReplayGain({"album", 12}, data).source, QString("missing"));
        QCOMPARE(resolveReplayGain({"off", 12}, data).gainDb, 0.0);
        av_dict_set(&stream, "REPLAYGAIN_TRACK_GAIN", "1000 dB", 0);
        av_dict_set(&container, "REPLAYGAIN_TRACK_GAIN", "-inf", 0);
        av_dict_set(&stream, "REPLAYGAIN_TRACK_PEAK", "-1", 0);
        data = readReplayGainTags(container, stream);
        QVERIFY(!data.trackGainDb);
        QVERIFY(!data.trackPeak);
        av_dict_free(&container);
        av_dict_free(&stream);
    }
    void taggedDecoderAndProbeAgree() {
        const auto path = QStringLiteral(NEKOTUNE_SOURCE_DIR) + "/tests/fixtures/audio-pipeline/replaygain.flac";
        const auto before = hash(path);
        std::atomic_bool cancel{false};
        const auto probe = readAudioFileInfo(path, cancel);
        QCOMPARE(probe.replayGain.trackGainDb, std::optional(-4.5));
        QCOMPARE(probe.replayGain.albumGainDb, std::optional(-2.0));
        auto decoder = std::make_shared<PcmDecoder>(QUrl::fromLocalFile(path));
        QTRY_VERIFY(decoder->info().eof);
        QCOMPARE(decoder->info().replayGain, probe.replayGain);
        decoder->updateReplayGainData({10, 10, .1, .1, "analysis", "analysis"});
        QCOMPARE(decoder->info().replayGain, probe.replayGain);
        PcmRenderer renderer(48000, decoder);
        renderer.setReplayGain({"track", 0});
        auto pcm = render(renderer);
        auto unmodified = std::make_shared<PcmDecoder>(QUrl::fromLocalFile(path));
        QTRY_VERIFY(unmodified->info().eof);
        PcmRenderer plain(48000, unmodified);
        QVERIFY(std::abs(20 * std::log10(rms(pcm) / rms(render(plain))) + 4.5) < .01);
        QCOMPARE(hash(path), before);
    }
    void analysisConvergesPreservesAlbumAndFiles() {
        QTemporaryDir dir;
        const auto a = dir.filePath("quiet.wav"), b = dir.filePath("loud.wav"), silent = dir.filePath("silent.wav");
        QVERIFY(wav(a, .03));
        QVERIFY(wav(b, .12));
        QVERIFY(wav(silent, 0));
        const auto beforeA = hash(a), beforeB = hash(b), beforeSilent = hash(silent);
        LoudnessAnalysis analysis;
        QVERIFY(analysis.start({{{a, b}, true}, {{silent}, false}}));
        QVERIFY(!analysis.start({{{a}, false}}));
        QTRY_VERIFY_WITH_TIMEOUT(!analysis.status().running, 15000);
        QVERIFY2(analysis.status().error.isEmpty(), qPrintable(analysis.status().error));
        QCOMPARE(analysis.status().completed, 3);
        const auto ad = readLoudnessCache(a), bd = readLoudnessCache(b);
        QVERIFY(ad.trackGainDb);
        QVERIFY(bd.trackGainDb);
        QVERIFY(ad.albumGainDb);
        QVERIFY(bd.albumGainDb);
        QCOMPARE(ad.albumGainDb, bd.albumGainDb);
        QCOMPARE(ad.albumPeak, bd.albumPeak);
        QVERIFY(std::abs((*ad.trackGainDb - *bd.trackGainDb) - 20 * std::log10(4)) < .1);
        QVERIFY(!readLoudnessCache(silent).trackGainDb);
        auto output = [&](const QString &path, const QString &mode) {
            auto decoder = std::make_shared<PcmDecoder>(QUrl::fromLocalFile(path));
            PcmRenderer renderer(48000, decoder);
            renderer.setReplayGain({mode, 0});
            PcmDsp dsp(48000);
            auto samples = dsp.process(render(renderer));
            auto tail = dsp.finish();
            samples.insert(samples.end(), tail.begin(), tail.end());
            return samples;
        };
        const auto normalizedA = output(a, "track"), normalizedB = output(b, "track");
        QVERIFY(std::abs(20 * std::log10(rms(normalizedA) / rms(normalizedB))) < 1);
        QVERIFY(std::abs(20 * std::log10(rms(output(b, "album")) / rms(output(a, "album"))) - 20 * std::log10(4)) < .1);
        QCOMPARE(rms(output(silent, "track")), 0.0);
        const auto outA = dir.filePath("out-a.wav"), outB = dir.filePath("out-b.wav");
        QVERIFY(wavSamples(outA, normalizedA));
        QVERIFY(wavSamples(outB, normalizedB));
        QVERIFY(analysis.start({{{outA}, false}, {{outB}, false}}));
        QTRY_VERIFY_WITH_TIMEOUT(!analysis.status().running, 10000);
        const auto normalizedGainA = readLoudnessCache(outA).trackGainDb,
                   normalizedGainB = readLoudnessCache(outB).trackGainDb;
        QVERIFY(normalizedGainA);
        QVERIFY(normalizedGainB);
        QVERIFY(std::abs(*normalizedGainA) < 1);
        QVERIFY(std::abs(*normalizedGainB) < 1);
        QCOMPARE(hash(a), beforeA);
        QCOMPARE(hash(b), beforeB);
        QCOMPARE(hash(silent), beforeSilent);
        // A replaced album member invalidates album compensation for the other members too.
        QVERIFY(wav(b, .2, 48000));
        QVERIFY(!readLoudnessCache(b).trackGainDb);
        QVERIFY(!readLoudnessCache(a).albumGainDb);
        QVERIFY(readLoudnessCache(a).trackGainDb);
    }
    void analysisCacheFollowsAudioSymlinks() {
        QTemporaryDir dir;
        const auto a = dir.filePath("a.wav"), b = dir.filePath("b.wav");
        const auto linkA = dir.filePath("managed-a.wav"), linkB = dir.filePath("managed-b.wav");
        QVERIFY(wav(a, .04));
        QVERIFY(wav(b, .08));
        if (!createAudioSymlink(a, linkA) || !createAudioSymlink(b, linkB))
            QSKIP("Native audio symlinks are unavailable");
        QVERIFY(QFileInfo(linkA).isSymLink());
        QVERIFY(QFileInfo(linkB).isSymLink());
        LoudnessAnalysis analysis;
        QVERIFY(analysis.start({{{linkA, linkB}, true}}));
        QTRY_VERIFY_WITH_TIMEOUT(!analysis.status().running, 10000);
        QVERIFY2(analysis.status().error.isEmpty(), qPrintable(analysis.status().error));
        const auto original = readLoudnessCache(a);
        QVERIFY(original.trackGainDb);
        QVERIFY(original.albumGainDb);
        QCOMPARE(readLoudnessCache(linkA), original);
        QCOMPARE(readLoudnessCache(linkB), readLoudnessCache(b));
        QCOMPARE(readLoudnessCache(b).albumGainDb, original.albumGainDb);
        QVERIFY(wav(b, .12, 48000));
        QVERIFY(!readLoudnessCache(linkB).trackGainDb);
        QVERIFY(!readLoudnessCache(linkA).albumGainDb);
        QVERIFY(readLoudnessCache(linkA).trackGainDb);
    }
    void cancellationAndPlaybackThrottle() {
        QTemporaryDir dir;
        const auto path = dir.filePath("long.wav");
        QVERIFY(wav(path, .01, 48000 * 10));
        LoudnessAnalysis analysis;
        analysis.setPlaying(true);
        QVERIFY(analysis.start({{{path}, false}}));
        QTest::qWait(120);
        QVERIFY(analysis.status().running);
        analysis.cancel();
        QVERIFY(analysis.status().cancelling);
        QTRY_VERIFY_WITH_TIMEOUT(!analysis.status().running, 1500);
        QVERIFY(analysis.status().cancelled);
        QVERIFY(!readLoudnessCache(path).trackGainDb);
        analysis.setPlaying(false);
        QVERIFY(analysis.start({{{path}, false}}));
        QTRY_VERIFY_WITH_TIMEOUT(!analysis.status().running, 10000);
        QVERIFY(!analysis.status().cancelled);
        QVERIFY(readLoudnessCache(path).trackGainDb);
    }
    void crossfadeUsesEachSourcesGainAndFinalLimiter() {
        QTemporaryDir dir;
        const auto a = dir.filePath("a.wav"), b = dir.filePath("b.wav");
        QVERIFY(wav(a, .1, 4800, true));
        QVERIFY(wav(b, .2, 4800, true));
        auto old = std::make_shared<PcmDecoder>(QUrl::fromLocalFile(a));
        auto next = std::make_shared<PcmDecoder>(QUrl::fromLocalFile(b));
        QTRY_VERIFY(old->info().eof && next->info().eof);
        old->updateReplayGainData({20 * std::log10(2), {}, {}, {}, "analysis", {}});
        next->updateReplayGainData({0, {}, {}, {}, "analysis", {}});
        PcmRenderer renderer(48000, old);
        renderer.setReplayGain({"track", 0});
        renderer.preload(next, {QUrl::fromLocalFile(b), 1, TransitionStrategy::Crossfade, .05});
        renderer.authorize(1);
        const auto pcm = render(renderer);
        QCOMPARE(pcm.size(), size_t(7200 * 2));
        for (int i = 0; i < 2400; ++i) {
            const double phase = double(i) / 2399 * std::numbers::pi / 2;
            QVERIFY(std::abs(pcm[size_t(i + 2400) * 2] - .2 * (std::cos(phase) + std::sin(phase))) < .0001);
        }
        EqualizerSettings eq;
        eq.enabled = true;
        eq.autoHeadroom = false;
        eq.preampDb = 12;
        eq.gains[5] = 12;
        PcmDsp dsp(48000);
        dsp.setSettings(eq);
        auto loud = pcm;
        for (auto &sample : loud)
            sample *= 20;
        auto protectedPcm = dsp.process(loud);
        const auto tail = dsp.finish();
        protectedPcm.insert(protectedPcm.end(), tail.begin(), tail.end());
        QVERIFY(dsp.error().isEmpty());
        QVERIFY(!protectedPcm.empty());
        for (float sample : protectedPcm)
            QVERIFY(std::isfinite(sample) && std::abs(sample) <= 1.00001f);
    }
    void smoothGainChangesAndMissingTrackReset() {
        PcmGainEnvelope gain;
        gain.setGain(-12, 48000, true);
        const float start = gain.next();
        gain.setGain(12, 48000);
        float previous = start;
        for (int i = 0; i < 960; ++i) {
            float value = gain.next();
            QVERIFY(std::abs(value - previous) < .004);
            previous = value;
        }
        QVERIFY(std::abs(previous - std::pow(10., .6)) < .00001);
        QTemporaryDir dir;
        const auto a = dir.filePath("a.wav"), b = dir.filePath("b.wav");
        QVERIFY(wav(a, .1, 4800, true));
        QVERIFY(wav(b, .1, 4800, true));
        auto old = std::make_shared<PcmDecoder>(QUrl::fromLocalFile(a));
        auto next = std::make_shared<PcmDecoder>(QUrl::fromLocalFile(b));
        QTRY_VERIFY(old->info().eof && next->info().eof);
        old->updateReplayGainData({6, {}, {}, {}, "analysis", {}});
        PcmRenderer renderer(48000, old);
        renderer.setReplayGain({"track", 6});
        renderer.preload(next, {QUrl::fromLocalFile(b), 1, TransitionStrategy::Gapless, 0});
        renderer.authorize(1);
        const auto pcm = render(renderer);
        QCOMPARE(pcm.size(), size_t(9600 * 2));
        QVERIFY(std::abs(pcm[4700 * 2] - float(.1 * std::pow(10., .6))) < .00001);
        QVERIFY(std::abs(pcm[6000 * 2] - .1f) < .00001);
        QVERIFY(std::abs(pcm[4800 * 2] - pcm[4799 * 2]) < .001);
    }
    void settingsPersistenceAndBackendFallback() {
        QVERIFY(!parseAudioSettings({{"replay_gain", QJsonObject{{"mode", "bogus"}}}}));
        QVERIFY(!parseAudioSettings({{"replay_gain", QJsonObject{{"preamp_db", 13}}}}));
        QVERIFY(!parseAudioSettings({{"replay_gain", QJsonObject{{"preamp_db", "3"}}}}));
        auto parsed = parseAudioSettings({{"replay_gain", QJsonObject{{"mode", "album"}, {"preamp_db", 2}}}});
        QVERIFY(parsed);
        QCOMPARE(parseAudioSettings(audioSettingsJson(parsed.value())).value(), parsed.value());
        QTemporaryDir dir;
        StoreFixture store(dir.filePath("state.db"));
        Audio *active = nullptr;
        PlaybackBackendSupervisor backend(
            "ffmpeg",
            [&] {
                auto audio = std::make_unique<Audio>();
                active = audio.get();
                audio->gainData = {-3, -5, {}, {}, "tag", "tag"};
                return audio;
            },
            [] {
                auto audio = std::make_unique<Audio>();
                audio->supported = false;
                return audio;
            });
        QueueService queue(store.queueRepo, store.songRepo, store.db);
        PlayerEngine player(backend, queue);
        bool save = false;
        AudioSettingsService settings(backend, player, {}, [&](const AudioSettings &) { return save; });
        QVERIFY(!settings.update(parsed.value()));
        QCOMPARE(active->gainSettings.mode, QString("off"));
        save = true;
        QVERIFY(settings.update(parsed.value()));
        QCOMPARE(settings.replayGainStatus().gainDb, -3.0);
        QCOMPARE(backend.volume(), .8);
        backend.setSource(QUrl::fromLocalFile(dir.filePath("a.wav")));
        emit active->decoderFailure("fallback", true);
        QTRY_COMPARE(backend.backendName(), QString("qt"));
        QCOMPARE(settings.replayGainStatus().source, QString("unavailable"));
        QCOMPARE(settings.replayGainStatus().gainDb, 0.0);
        QCOMPARE(settings.settings().replayGain.mode, QString("album"));
        backend.setSource(QUrl::fromLocalFile(dir.filePath("b.wav")));
        QCOMPARE(active->gainSettings.mode, QString("album"));
        QCOMPARE(active->gainSettings.preampDb, 2.0);
    }
    void ipcValidationAlbumExpansionAndRestart() {
        QTemporaryDir dir;
        qputenv("NEKOTUNE_DB_PATH", dir.filePath("ipc.db").toUtf8());
        const auto a = dir.filePath("a.wav"), b = dir.filePath("b.wav");
        QVERIFY(wav(a, .04));
        QVERIFY(wav(b, .08));
        const auto beforeA = hash(a), beforeB = hash(b);
        BackendRuntime runtime([] { return std::make_unique<Audio>(); });
        QVERIFY2(runtime.start(), qPrintable(runtime.errorString()));
        Peer first, second;
        first.socket.connectToServer(runtime.serverName());
        second.socket.connectToServer(runtime.serverName());
        QVERIFY(first.socket.waitForConnected(3000));
        QVERIFY(second.socket.waitForConnected(3000));
        auto data = first.call("player.audio_settings").value("data").toObject().value("audio_settings").toObject();
        QCOMPARE(data.value("replay_gain").toObject().value("mode").toString(), QString("off"));
        QVERIFY(data.value("replay_gain_supported").toBool());
        QCOMPARE(first.call("player.set_replay_gain", {{"mode", "invalid"}}).value("status").toString(), QString("error"));
        QCOMPARE(first.call("player.analyze_loudness", {{"ids", QJsonArray{1.5}}}).value("status").toString(), QString("error"));
        QCOMPARE(first.call("player.analyze_loudness", {{"ids", QJsonArray{}}}).value("status").toString(), QString("error"));
        QCOMPARE(first.call("player.analyze_loudness", {{"ids", QJsonArray{999999}}}).value("status").toString(), QString("error"));
        QVERIFY(first.call("player.set_replay_gain", {{"mode", "album"}, {"preamp_db", -2}}).value("status").toString() == "ok");
        data = second.call("player.audio_settings").value("data").toObject().value("audio_settings").toObject();
        QCOMPARE(data.value("replay_gain").toObject().value("preamp_db").toDouble(), -2.0);
        QVERIFY(std::any_of(second.events.begin(), second.events.end(), [](const auto &event) {
            return event.value("event").toString() == "player.audio_settings_changed";
        }));
        const int aId = first.call("library.import", {{"path", a}}).value("data").toObject().value("song_id").toInt();
        const int bId = first.call("library.import", {{"path", b}}).value("data").toObject().value("song_id").toInt();
        QVERIFY(aId > 0);
        QVERIFY(bId > 0);
        for (int id : {aId, bId})
            QVERIFY(first
                        .call("song.update_metadata",
                              {{"song_id", id}, {"album", id == aId ? "Album" : "album"}, {"album_artist", "Artist"}})
                        .value("status")
                        .toString() == "ok");
        const auto started = first.call("player.analyze_loudness", {{"ids", QJsonArray{aId}}});
        QVERIFY(started.value("status").toString() == "ok");
        QCOMPARE(started.value("data")
                     .toObject()
                     .value("audio_settings")
                     .toObject()
                     .value("loudness_analysis")
                     .toObject()
                     .value("total")
                     .toInt(),
                 2);
        QTRY_VERIFY_WITH_TIMEOUT(readLoudnessCache(b).albumGainDb.has_value(), 10000);
        QCOMPARE(readLoudnessCache(a).albumGainDb, readLoudnessCache(b).albumGainDb);
        QCOMPARE(hash(a), beforeA);
        QCOMPARE(hash(b), beforeB);
        first.socket.abort();
        second.socket.abort();
        runtime.stop();
        QVERIFY(runtime.start());
        Peer restored;
        restored.socket.connectToServer(runtime.serverName());
        QVERIFY(restored.socket.waitForConnected(3000));
        data = restored.call("player.audio_settings").value("data").toObject().value("audio_settings").toObject();
        QCOMPARE(data.value("replay_gain").toObject().value("mode").toString(), QString("album"));
        QCOMPARE(data.value("replay_gain").toObject().value("preamp_db").toDouble(), -2.0);
        QCOMPARE(data.value("loudness_analysis").toObject().value("running").toBool(), false);
        restored.socket.abort();
        runtime.stop();
        qunsetenv("NEKOTUNE_DB_PATH");
    }
};
QTEST_GUILESS_MAIN(ReplayGainTest)
#include "replay_gain_test.moc"
