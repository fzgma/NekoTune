#include "controllers/theme_controller.h"
#include "app_paths.h"
#include "controllers/extensions_controller.h"
#include "ipc_client.h"
#include "extensions/extension_view.h"
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStyleHints>
#include <QUrl>
#include <QElapsedTimer>
#include <QPointer>
#include <QQuickWindow>
#include <QTimer>

namespace {
bool viewsLoading(QQuickItem *item) {
  if (const auto *view = qobject_cast<ExtensionView *>(item); view && view->loading())
    return true;
  if ((item->objectName() == "themeBackground" || item->objectName() == "builtinShellLoader") &&
      item->property("loading").toBool())
    return true;
  for (auto *child : item->childItems())
    if (viewsLoading(child))
      return true;
  return false;
}
QVariantMap themeCatalog() {
  QFile file(QStringLiteral(":/themes/catalog.json"));
  return file.open(QIODevice::ReadOnly)
             ? QJsonDocument::fromJson(file.readAll()).object().toVariantMap()
             : QVariantMap{};
}
} // namespace
void ThemeController::presentWindow(QQuickWindow *window) {
  if (!window)
    return;
  auto *timer = new QTimer(window);
  timer->setInterval(16);
  QElapsedTimer elapsed;
  elapsed.start();
  const QPointer<ThemeController> guard(this);
  connect(timer, &QTimer::timeout, window, [guard, window, timer, elapsed] {
    // A caller may explicitly show the window (e.g. a test). Never reopen a
    // window later when the user has already hidden it to the tray.
    if (!guard || window->isVisible()) {
      window->setProperty("appearanceStartupPending", false);
      timer->deleteLater();
      return;
    }
    const bool timedOut = elapsed.elapsed() >= 12000;
    if (!timedOut && ((!guard->safeMode() && !guard->m_extensions.appearanceReady()) ||
                      viewsLoading(window->contentItem())))
      return;
    if (timedOut) {
      guard->m_error = "theme_startup_timeout";
      emit guard->changed();
    }
    window->setProperty("appearanceStartupPending", false);
    window->show();
    timer->deleteLater();
  });
  timer->start();
}
QVariantMap ThemeDefaults::defaults(bool dark) const {
  const auto catalog = themeCatalog();
  const auto rules = catalog.value("tokens").toMap();
  QVariantMap result;
  for (auto it = rules.cbegin(); it != rules.cend(); ++it)
    result.insert(it.key(), it.value().toMap().value("default"));
  if (!dark) {
    const auto light = catalog.value("light").toMap();
    for (auto it = light.cbegin(); it != light.cend(); ++it)
      result.insert(it.key(), it.value());
  }
  return result;
}

ThemeController::ThemeController(IpcClient &client,
                                 ExtensionsController &extensions)
    : m_client(client), m_extensions(extensions) {
  m_catalog = themeCatalog();
  m_mode = nekotune::AppPaths::setting("theme_mode").toString("dark");
  if (m_mode != "light" && m_mode != "dark" && m_mode != "system")
    m_mode = "dark";
  connect(&extensions, &ExtensionsController::changed, this,
          &ThemeController::rebuild);
  connect(&client, &IpcClient::connectedChanged, this,
          &ThemeController::changed);
  connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this,
          [this] { rebuild(); });
  connect(&extensions, &ExtensionsController::completed, this,
          [this](int id, const QVariantMap &data, const QString &error) {
            if (id != m_pending)
              return;
            m_pending = 0;
            m_error = error;
            auto completion = std::move(m_completion);
            m_completion = {};
            if (completion)
              completion(data, error);
            emit changed();
          });
  rebuild();
}
bool ThemeController::safeMode() const {
  return qEnvironmentVariable("NEKOTUNE_SAFE_MODE") == "1";
}
bool ThemeController::connected() const { return m_client.connected(); }
QString ThemeController::selectedId() const {
  const auto id = m_extensions.selections().value("theme").toString();
  return id.isEmpty() ? QStringLiteral("builtin/yozakura") : id;
}
bool ThemeController::isDark() const {
  return safeMode() || m_mode == "dark" ||
         (m_mode == "system" && QGuiApplication::styleHints()->colorScheme() !=
                                    Qt::ColorScheme::Light);
}
bool ThemeController::legacyLight() const {
  return !isDark() && !m_active.contains("schemaVersion");
}
QVariantMap ThemeController::find(const QString &id) const {
  for (const auto &value : m_items)
    if (value.toMap().value("id") == id)
      return value.toMap();
  return {};
}
QVariantMap ThemeController::resolve(const QVariantMap &theme,
                                     bool dark) const {
  QVariantMap result;
  const auto rules = m_catalog.value("tokens").toMap();
  for (auto it = rules.cbegin(); it != rules.cend(); ++it)
    result.insert(it.key(), it.value().toMap().value("default"));
  auto merge = [&](const QVariantMap &values) {
    for (auto it = values.cbegin(); it != values.cend(); ++it)
      if (rules.contains(it.key()))
        result.insert(it.key(), it.value());
  };
  if (!dark)
    merge(m_catalog.value("light").toMap());
  if (dark || theme.contains("schemaVersion")) {
    merge(theme.value("tokens").toMap());
    merge(theme.value("variants")
              .toMap()
              .value(dark ? "dark" : "light")
              .toMap()
              .value("tokens")
              .toMap());
  }
  if (!theme.value("tokens").toMap().contains("bgCard") &&
      !theme.value("variants")
           .toMap()
           .value(dark ? "dark" : "light")
           .toMap()
           .value("tokens")
           .toMap()
           .contains("bgCard"))
    result["bgCard"] = result.value("bgSurface");
  return result;
}
void ThemeController::rebuild() {
  m_items.clear();
  for (const auto &value : m_catalog.value("themes").toList()) {
    auto item = value.toMap();
    item.insert("origin", "builtin");
    item.insert("available", true);
    m_items.append(item);
  }
  for (const auto &value : m_extensions.items()) {
    const auto extension = value.toMap();
    auto themes = extension.value("themes").toList();
    if (!extension.contains("themes"))
      themes = extension.value("contributes").toMap().value("themes").toList();
    for (const auto &theme : themes) {
      auto item = theme.toMap();
      item.insert("origin", extension.value("type") == "theme" ? "imported"
                                                               : "extension");
      item.insert("available", extension.value("state") == "running");
      item.insert("extensionId", extension.value("id"));
      item.insert("version", extension.value("version"));
      item.insert("directory", extension.value("directory"));
      item.insert("generation", extension.value("generation"));
      item.insert("trusted", extension.value("trusted"));
      item.insert("hasCode", extension.value("hasCode", true));
      m_items.append(item);
    }
  }
  for (auto &value : m_items) {
    auto item = value.toMap();
    const auto palette = resolve(item, isDark());
    item.insert("swatches", QVariantList{palette.value("bgCanvas"),
                                         palette.value("bgSurface"),
                                         palette.value("accentPrimary"),
                                         palette.value("accentSecondary"),
                                         palette.value("textPrimary")});
    value = item;
  }
  m_active =
      find(safeMode() ? QStringLiteral("builtin/yozakura") : selectedId());
  if (!m_active.value("available").toBool())
    m_active = find(QStringLiteral("builtin/yozakura"));
  m_tokens = resolve(m_active, isDark());
  m_background = m_active.value("variants")
                     .toMap()
                     .value(isDark() ? "dark" : "light")
                     .toMap()
                     .value("background")
                     .toMap();
  const auto directory = m_active.value("directory").toString();
  if (!m_background.value("source").toString().isEmpty())
    m_background["source"] = QUrl::fromLocalFile(
        QDir(directory).filePath(m_background.value("source").toString()));
  m_slots.clear();
  auto add = [&](const QString &slot, const QVariant &source) {
    if (source.toString().isEmpty())
      return;
    m_slots.insert(
        slot,
        QVariantMap{{"id", activeId() + "/" + slot},
                    {"slot", slot},
                    {"source", QDir(directory).filePath(source.toString())},
                    {"extensionId", m_active.value("extensionId")},
                    {"generation", m_active.value("generation")}});
  };
  if (!safeMode()) {
    const auto ui = m_active.value("ui").toMap();
    for (const auto &key : {"background", "shell", "bottomPlayer"})
      add(QLatin1String(key), ui.value(QLatin1String(key)));
    const auto pages = ui.value("pages").toMap();
    for (auto it = pages.cbegin(); it != pages.cend(); ++it)
      add("page:" + it.key(), it.value());
  }
  emit changed();
}
void ThemeController::setMode(const QString &mode) {
  if (mode != "light" && mode != "dark" && mode != "system")
    return;
  if (nekotune::AppPaths::saveSetting("theme_mode", mode)) {
    m_mode = mode;
    m_error.clear();
    rebuild();
  } else {
    m_error = "theme_save_failed";
    emit changed();
  }
}
void ThemeController::request(const QString &method, const QVariantMap &params,
                              Completion done) {
  if (busy())
    return;
  m_error.clear();
  m_notice.clear();
  m_completion = std::move(done);
  m_pending = m_extensions.request(method, params);
  emit changed();
}
void ThemeController::selectTheme(const QString &id, bool trusted) {
  if (busy())
    return;
  const auto theme = find(id);
  if (theme.isEmpty()) {
    m_error = "theme_unavailable";
    emit changed();
    return;
  }
  if (!theme.value("available").toBool()) {
    if (theme.value("hasCode").toBool() && !theme.value("trusted").toBool() &&
        !trusted) {
      emit trustRequested(id);
      return;
    }
    request("extensions.enable",
            {{"id", theme.value("extensionId")}, {"trusted", trusted}},
            [this, id](const QVariantMap &, const QString &error) {
              if (error.isEmpty())
                selectTheme(id);
            });
    return;
  }
  request("extensions.select",
          {{"slot", "theme"},
           {"contribution", id == "builtin/yozakura" ? QString() : id}});
}
void ThemeController::importTheme(const QString &path, bool replace) {
  installTheme(path, replace);
}
void ThemeController::updateTheme(const QString &id, const QString &path,
                                  bool replace) {
  if (find(id).value("origin") != "imported")
    return;
  installTheme(path, replace, id);
}
void ThemeController::installTheme(const QString &path, bool replace,
                                   const QString &id) {
  QVariantMap params{{"path", path}, {"replace", replace}, {"themeOnly", true}};
  if (!id.isEmpty())
    params.insert("expectedId", find(id).value("extensionId"));
  request("extensions.install", params,
          [this, path, replace, id](const QVariantMap &, const QString &error) {
            if (!replace && error.contains("already installed")) {
              m_error.clear();
              emit replaceRequested(path, id);
            } else if (error.isEmpty())
              m_notice = replace ? "theme_updated" : "theme_imported";
          });
}
void ThemeController::reloadTheme(const QString &id) {
  const auto theme = find(id);
  if (safeMode() || theme.value("origin") != "imported" ||
      !theme.value("available").toBool())
    return;
  request("extensions.reload", {{"id", theme.value("extensionId")}},
          [this](const QVariantMap &, const QString &error) {
            if (error.isEmpty())
              m_notice = "theme_reloaded";
          });
}
void ThemeController::exportTheme(const QString &id, const QString &path) {
  request("extensions.export_theme", {{"contribution", id}, {"path", path}},
          [this](const QVariantMap &, const QString &error) {
            if (error.isEmpty())
              m_notice = "theme_exported";
          });
}
void ThemeController::removeTheme(const QString &id) {
  const auto theme = find(id);
  if (theme.value("origin") != "imported")
    return;
  request("extensions.uninstall",
          {{"id", theme.value("extensionId")}, {"clearData", false}});
}
void ThemeController::reset() {
  if (busy())
    return;
  request("extensions.select", {{"slot", "theme"}, {"contribution", ""}},
          [this](const QVariantMap &, const QString &error) {
            if (error.isEmpty())
              setMode("dark");
          });
}
