#include <nekotune/sdk/extension_plugin.h>
#include <QDir>
#include <QFile>
#include <QObject>
#include <QTimer>
#include <QUrl>
#include <stdexcept>
#include <thread>

using namespace nekotune;

namespace {
class Tones final : public sdk::IMusicProvider {
  public:
    QString directory;
    void search(const QString &query, const QString &cursor,
                std::function<void(Result<sdk::SearchPage>)> done) override {
        sdk::SearchPage page;
        page.tracks.append({"one", "Tone " + query, "NekoTune", {}, {}, 8000});
        page.cursor = cursor.isEmpty() ? "next" : QString();
        done(page);
    }
    void track(const QString &id, std::function<void(Result<sdk::Track>)> done) override {
        done(sdk::Track{id, "Tone", {}, {}, {}, 0});
    }
    void resolve(const QString &id, const QString &,
                 std::function<void(Result<sdk::ResolvedAudio>)> done) override {
        sdk::ResolvedAudio audio;
        audio.url = id == "outside" ? QUrl::fromLocalFile(QDir::tempPath())
                                    : QUrl::fromLocalFile(QDir(directory).filePath("tone.wav"));
        done(audio);
    }
};
} // namespace

/// Behaviour is selected through config.mode: ok, fail, throw or hang.
class FaultyExtension final : public QObject, public sdk::IExtensionPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID NEKOTUNE_EXTENSION_IID)
    Q_INTERFACES(nekotune::sdk::IExtensionPlugin)
  public:
    void activate(sdk::IExtensionContext &context, std::function<void(Result<void>)> done) override {
        ++m_activations;
        const auto mode = context.config().value("mode").toString();
        if (mode == "throw")
            throw std::runtime_error("activation exploded");
        if (mode == "fail") {
            done(failure("activation refused"));
            return;
        }
        if (mode == "hang")
            return;
        QFile tone(QDir(context.dataDirectory()).filePath("tone.wav"));
        if (tone.open(QIODevice::WriteOnly))
            tone.write("RIFF");
        m_tones.directory = context.dataDirectory();
        m_registrations.push_back(context.registerMusicProvider("tones", &m_tones, {{"name", "Tones"}}));
        m_registrations.push_back(context.registerService(
            "state", [this](const sdk::Params &, sdk::Reply reply) { reply(sdk::Params{{"activations", m_activations}}); }));
        m_registrations.push_back(context.registerService(
            "throw", [](const sdk::Params &, sdk::Reply) { throw std::runtime_error("service exploded"); }));
        m_registrations.push_back(context.registerService("thread", [](const sdk::Params &params, sdk::Reply reply) {
            std::thread([params, reply] { reply(sdk::Params{{"echo", params.value("value")}}); }).detach();
        }));
        m_registrations.push_back(context.registerService("never", [](const sdk::Params &, sdk::Reply) {}));
        m_registrations.push_back(context.registerService("storage", [&context](const sdk::Params &params, sdk::Reply reply) {
            context.storageSet("value", params.value("value"), [&context, reply](Result<void> saved) {
                if (!saved) {
                    reply(saved.error());
                    return;
                }
                context.storageGet("value", [reply](Result<QVariant> value) {
                    reply(value ? Result<sdk::Params>(sdk::Params{{"value", value.value()}})
                                : Result<sdk::Params>(value.error()));
                });
            });
        }));
        m_registrations.push_back(context.registerService("ui", [this, &context](const sdk::Params &params, sdk::Reply reply) {
            auto page = context.registerUi("pages", params.value("id").toString(),
                                           {{"title", "Runtime"}, {"source", params.value("source")}});
            const bool accepted = page != nullptr;
            if (page)
                m_registrations.push_back(std::move(page));
            reply(sdk::Params{{"accepted", accepted}});
        }));
        m_registrations.push_back(context.registerService("echo-event", [&context](const sdk::Params &, sdk::Reply reply) {
            context.publish("ping", {{"value", 1}});
            reply(sdk::Params{});
        }));
        m_registrations.push_back(context.registerService("host", [&context](const sdk::Params &params, sdk::Reply reply) {
            context.hostCall(params.value("method").toString(), params.value("params").toMap(), reply);
        }));
        m_registrations.push_back(context.on("test.host", [this, &context](const sdk::Params &event) {
            m_events.append(event.value("value").toString());
            context.publish("seen", {{"values", m_events}});
        }));
        done({});
    }
    void deactivate() override {
        m_registrations.clear();
        m_events.clear();
    }

  private:
    Tones m_tones;
    std::vector<sdk::RegistrationPtr> m_registrations;
    QStringList m_events;
    int m_activations = 0;
};

#include "plugin.moc"
