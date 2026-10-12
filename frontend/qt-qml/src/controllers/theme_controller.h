#pragma once
#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include <QQuickWindow>
#include <functional>

class ExtensionsController;
class IpcClient;

// Resource-backed defaults also serve recovery windows and isolated QML tests.
class ThemeDefaults final : public QObject {
  Q_OBJECT
public:
  using QObject::QObject;
  Q_INVOKABLE QVariantMap defaults(bool dark) const;
};

class ThemeController final : public QObject {
  Q_OBJECT
  Q_PROPERTY(QVariantList items READ items NOTIFY changed)
  Q_PROPERTY(QString selectedId READ selectedId NOTIFY changed)
  Q_PROPERTY(QString activeId READ activeId NOTIFY changed)
  Q_PROPERTY(QString mode READ mode NOTIFY changed)
  Q_PROPERTY(bool isDark READ isDark NOTIFY changed)
  Q_PROPERTY(QVariantMap tokens READ tokens NOTIFY changed)
  Q_PROPERTY(QVariantMap background READ background NOTIFY changed)
  Q_PROPERTY(QVariantMap uiSlots READ uiSlots NOTIFY changed)
  Q_PROPERTY(bool legacyLight READ legacyLight NOTIFY changed)
  Q_PROPERTY(bool unavailable READ unavailable NOTIFY changed)
  Q_PROPERTY(bool safeMode READ safeMode CONSTANT)
  Q_PROPERTY(bool connected READ connected NOTIFY changed)
  Q_PROPERTY(bool busy READ busy NOTIFY changed)
  Q_PROPERTY(QString error READ error NOTIFY changed)
  Q_PROPERTY(QString notice READ notice NOTIFY changed)
public:
  ThemeController(IpcClient &client, ExtensionsController &extensions);
  QVariantList items() const { return m_items; }
  QString selectedId() const;
  QString activeId() const { return m_active.value("id").toString(); }
  QString mode() const { return m_mode; }
  bool isDark() const;
  QVariantMap tokens() const { return m_tokens; }
  QVariantMap background() const { return m_background; }
  QVariantMap uiSlots() const { return m_slots; }
  bool legacyLight() const;
  bool unavailable() const { return selectedId() != activeId() && !safeMode(); }
  bool safeMode() const;
  bool connected() const;
  bool busy() const { return m_pending != 0; }
  QString error() const { return m_error; }
  QString notice() const { return m_notice; }
  Q_INVOKABLE void setMode(const QString &mode);
  Q_INVOKABLE void selectTheme(const QString &id, bool trusted = false);
  Q_INVOKABLE void importTheme(const QString &path, bool replace = false);
  Q_INVOKABLE void updateTheme(const QString &id, const QString &path,
                              bool replace = false);
  Q_INVOKABLE void reloadTheme(const QString &id);
  Q_INVOKABLE void exportTheme(const QString &id, const QString &path);
  Q_INVOKABLE void removeTheme(const QString &id);
  Q_INVOKABLE void reset();
  Q_INVOKABLE void presentWindow(QQuickWindow *window);
signals:
  void changed();
  void trustRequested(const QString &id);
  void replaceRequested(const QString &path, const QString &themeId);

private:
  using Completion = std::function<void(const QVariantMap &, const QString &)>;
  void request(const QString &method, const QVariantMap &params,
               Completion done = {});
  void rebuild();
  void installTheme(const QString &path, bool replace, const QString &id = {});
  QVariantMap find(const QString &id) const;
  QVariantMap resolve(const QVariantMap &theme, bool dark) const;
  IpcClient &m_client;
  ExtensionsController &m_extensions;
  QVariantMap m_catalog, m_active, m_tokens, m_background, m_slots;
  QVariantList m_items;
  QString m_mode, m_error, m_notice;
  int m_pending = 0;
  Completion m_completion;
};
