#include <nekotune/sdk/extension_plugin.h>
#include <QCoreApplication>
#include <QDebug>
#include <QPluginLoader>

int main(int argc, char **argv) {
    QCoreApplication application(argc, argv);
    if (application.arguments().size() != 2)
        return 1;
    QPluginLoader loader(application.arguments().at(1));
    if (loader.metaData().value("IID").toString() != QStringLiteral(NEKOTUNE_EXTENSION_IID)
        || !qobject_cast<nekotune::sdk::IExtensionPlugin *>(loader.instance())) {
        qCritical().noquote() << loader.errorString();
        return 1;
    }
    // The host keeps native libraries mapped until exit as well.
    return 0;
}
