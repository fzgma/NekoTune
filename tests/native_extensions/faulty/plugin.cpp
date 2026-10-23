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
    void resolve(const QString &id, sdk::ResolvePurpose,
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
    void activate(sdk::ExtensionContext &context, sdk::Done done) override {
        ++m_activations;
        const auto mode = context.config().get().value("mode").toString();
        if (mode == "throw")
            throw std::runtime_error("activation exploded");
        if (mode == "fail") {
            done(failure("activation refused"));
            return;
        }
        if (mode == "hang")
            return;
        QFile tone(QDir(context.info().dataDirectory).filePath("tone.wav"));
        if (tone.open(QIODevice::WriteOnly))
            tone.write("RIFF");
        m_tones.directory = context.info().dataDirectory;
        m_registrations.push_back(context.music().add("tones", &m_tones, {.name = "Tones"}));
        m_registrations.push_back(context.services().add(
            "state", [this](const sdk::Params &, sdk::Reply reply) { reply(sdk::Params{{"activations", m_activations}}); }));
        m_registrations.push_back(context.services().add(
            "throw", [](const sdk::Params &, sdk::Reply) { throw std::runtime_error("service exploded"); }));
        m_registrations.push_back(context.services().add("thread", [](const sdk::Params &params, sdk::Reply reply) {
            std::thread([params, reply] { reply(sdk::Params{{"echo", params.value("value")}}); }).detach();
        }));
        m_registrations.push_back(context.services().add("never", [](const sdk::Params &, sdk::Reply) {}));
        m_registrations.push_back(context.services().add("storage", [&context](const sdk::Params &params, sdk::Reply reply) {
            context.storage().set("value", params.value("value"), [&context, reply](Result<void> saved) {
                if (!saved) {
                    reply(saved.error());
                    return;
                }
                context.storage().get("value", [reply](Result<QVariant> value) {
                    reply(value ? Result<sdk::Params>(sdk::Params{{"value", value.value()}})
                                : Result<sdk::Params>(value.error()));
                });
            });
        }));
        m_registrations.push_back(context.services().add("ui", [this, &context](const sdk::Params &params, sdk::Reply reply) {
            auto page = context.ui().add("pages", params.value("id").toString(),
                                           {{"title", "Runtime"}, {"source", params.value("source")}});
            const bool accepted = page != nullptr;
            if (page)
                m_registrations.push_back(std::move(page));
            reply(sdk::Params{{"accepted", accepted}});
        }));
        m_registrations.push_back(context.services().add("echo-event", [&context](const sdk::Params &, sdk::Reply reply) {
            context.events().publish("ping", {{"value", 1}});
            reply(sdk::Params{});
        }));
        m_registrations.push_back(context.services().add("host", [&context](const sdk::Params &params, sdk::Reply reply) {
            context.host().call(params.value("method").toString(), params.value("params").toMap(), reply);
        }));
        m_registrations.push_back(context.events().on("test.host", [this, &context](const sdk::Params &event) {
            m_events.append(event.value("value").toString());
            context.events().publish("seen", {{"values", m_events}});
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
