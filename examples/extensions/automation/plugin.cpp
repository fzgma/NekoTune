#include <nekotune/sdk/extension_plugin.h>
#include <QObject>
#include <QTimer>

using namespace nekotune;

class AutomationExample final : public QObject, public sdk::IExtensionPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID NEKOTUNE_EXTENSION_IID)
    Q_INTERFACES(nekotune::sdk::IExtensionPlugin)
  public:
    AutomationExample() {
        m_timer.setSingleShot(true);
        connect(&m_timer, &QTimer::timeout, this, [this] {
            if (!m_context)
                return;
            m_context->hostCall("player.pause", {}, [context = m_context](Result<sdk::Params> result) {
                if (!result)
                    context->log(QtWarningMsg, "Sleep timer could not pause: " + result.error().message);
            });
        });
    }
    void activate(sdk::IExtensionContext &context, std::function<void(Result<void>)> done) override {
        m_context = &context;
        m_command = context.registerCommand(
            "sleep-timer",
            [this](const sdk::Params &, sdk::Reply reply) {
                const int seconds = qMax(1, m_context->config().value("sleepSeconds", 30).toInt());
                m_timer.start(seconds * 1000);
                reply(sdk::Params{{"seconds", seconds}});
            },
            {{"title", "Sleep timer"}, {"shortcut", "Ctrl+Shift+S"}});
        m_trackChanged = context.on("player.track_changed", [this](const sdk::Params &event) {
            m_context->log(QtInfoMsg, "Now playing: " + event.value("song").toMap().value("title").toString());
        });
        done({});
    }
    void deactivate() override {
        m_timer.stop();
        m_command.reset();
        m_trackChanged.reset();
        m_context = nullptr;
    }

  private:
    QTimer m_timer;
    sdk::IExtensionContext *m_context = nullptr;
    sdk::RegistrationPtr m_command, m_trackChanged;
};

#include "plugin.moc"
