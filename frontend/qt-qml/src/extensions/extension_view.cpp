#include "extensions/extension_view.h"
#include "controllers/theme_controller.h"
#include <QFileInfo>
#include <QQmlComponent>
#include <QQmlContext>
#include <QTimer>
#include <qqml.h>
#include <algorithm>
#include <vector>
namespace {
std::shared_ptr<QQmlEngine> sharedEngine(QQmlEngine *host, const QVariantMap &descriptor,
                                       QObject *controllers, QObject *translator) {
    struct Entry {
        QPointer<QQmlEngine> host;
        QPointer<QObject> controllers, translator;
        QString package;
        QVariant generation;
        std::weak_ptr<QQmlEngine> engine;
    };
    static std::vector<Entry> engines;
    std::erase_if(engines, [](const auto &entry) { return entry.engine.expired(); });
    auto package = descriptor.value("extensionId").toString();
    if (package.isEmpty())
        package = QFileInfo(descriptor.value("source").toString()).absolutePath();
    const auto generation = descriptor.value("generation");
    for (const auto &entry : engines)
        if (entry.host == host && entry.controllers == controllers && entry.translator == translator &&
            entry.package == package && entry.generation == generation)
            return entry.engine.lock();
    auto engine = std::make_shared<QQmlEngine>();
    engine->addImportPath("qrc:/qt/qml");
    // QML singletons (including Builtin.Theme) are created in the root context.
    // Only host-wide values belong here; window data stays in each view context.
    engine->rootContext()->setContextProperties({
        {"controllers", QVariant::fromValue(controllers)},
        {"i18n", QVariant::fromValue(translator)},
        {"extensionBridge", QVariant::fromValue(
            controllers ? controllers->property("extensions").value<QObject *>() : nullptr)},
        {"extensionId", descriptor.value("extensionId").toString()}});
    // Share within one package generation only: directory imports, JS libraries
    // and QML singletons retain package isolation, and reloads get a fresh cache.
    // Do not add package directories as module roots (Qt 6.8 module identities).
    engines.push_back({host, controllers, translator, package, generation, engine});
    return engine;
}
bool sameComponent(const QVariantMap &a, const QVariantMap &b) {
    return a.value("source") == b.value("source") && a.value("extensionId") == b.value("extensionId") &&
           a.value("generation") == b.value("generation");
}
} // namespace
ExtensionView::ExtensionView(QQuickItem *parent) : QQuickItem(parent) {
    connect(this, &ExtensionView::contextChanged, this, &ExtensionView::schedule);
}
ExtensionView::~ExtensionView() {
    delete m_item;
}
bool ExtensionView::invokeAction(const QString &method) {
    if (!m_item || method != QStringLiteral("focusSearch") ||
        m_item->metaObject()->indexOfMethod("focusSearch()") < 0)
        return false;
    return QMetaObject::invokeMethod(m_item, "focusSearch", Qt::DirectConnection);
}
void ExtensionView::setDescriptor(const QVariantMap &descriptor) {
    if (descriptor == m_descriptor)
        return;
    m_descriptor = descriptor;
    emit descriptorChanged();
    schedule();
}
void ExtensionView::componentComplete() {
    QQuickItem::componentComplete();
    schedule();
}
void ExtensionView::schedule() {
    if (!isComponentComplete() || m_scheduled)
        return;
    m_scheduled = true;
    QTimer::singleShot(0, this, [this] {
        m_scheduled = false;
        rebuild();
    });
}
void ExtensionView::geometryChange(const QRectF &now, const QRectF &before) {
    QQuickItem::geometryChange(now, before);
    if (m_item)
        m_item->setSize(now.size());
}
void ExtensionView::rebuild() {
    if (m_context && sameComponent(m_descriptor, m_loadedDescriptor) &&
        m_engine->rootContext()->contextProperty("controllers").value<QObject *>() == controllers &&
        m_engine->rootContext()->contextProperty("i18n").value<QObject *>() == translator) {
        updateContext();
        return;
    }
    delete m_item;
    m_context.reset();
    m_engine.reset();
    m_loadedDescriptor = m_descriptor;
    m_error.clear();
    const auto path = m_descriptor.value("source").toString();
    if (path.isEmpty()) {
        emit readyChanged();
        return;
    }
    m_engine = sharedEngine(qmlEngine(this), m_descriptor, controllers, translator);
    m_context = std::make_unique<QQmlContext>(m_engine->rootContext());
    updateContext();
    QQmlComponent component(m_engine.get(), QUrl::fromLocalFile(path));
    auto *created = component.isReady() ? component.create(m_context.get()) : nullptr;
    m_item = qobject_cast<QQuickItem *>(created);
    if (!m_item) {
        QStringList errors;
        for (const auto &error : component.errors())
            errors.append(error.toString());
        m_error = errors.isEmpty() ? "Extension root must be a QtQuick Item" : errors.join('\n');
        delete created;
    } else {
        m_item->setParent(this);
        m_item->setParentItem(this);
        m_item->setSize(size());
    }
    emit readyChanged();
}
void ExtensionView::updateContext() {
    m_context->setContextProperties({
        {"controllers", QVariant::fromValue(controllers)},
        {"i18n", QVariant::fromValue(translator)},
        {"extensionBridge", QVariant::fromValue(
            controllers ? controllers->property("extensions").value<QObject *>() : nullptr)},
        {"extensionId", m_descriptor.value("extensionId").toString()},
        {"hostWindow", QVariant::fromValue(hostWindow)},
        {"wallpaperContext", QVariant::fromValue(wallpaperContext)}});
}
void registerExtensionQmlTypes() {
    static bool registered = false;
    if (!registered) {
        qmlRegisterType<ExtensionView>("NekoTune", 1, 0, "ExtensionView");
        qmlRegisterSingletonType<ThemeDefaults>("NekoTune", 1, 0, "ThemeDefaults",
            [](QQmlEngine *, QJSEngine *) -> QObject * { return new ThemeDefaults; });
        registered = true;
    }
}
