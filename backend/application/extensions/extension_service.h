#pragma once
#include "domain/extensions/extension_backend.h"

namespace nekotune {
class ExtensionService final : public QObject {
    Q_OBJECT
  public:
    explicit ExtensionService(IExtensionBackend &backend) : m_backend(backend) {
        connect(&backend, &IExtensionBackend::changed, this, &ExtensionService::changed);
        connect(&backend, &IExtensionBackend::eventReady, this, &ExtensionService::eventReady);
    }
    QVariantMap snapshot() const { return m_backend.snapshot(); }
    void request(const QString &method, const QVariantMap &params, IExtensionBackend::Completion done) {
        m_backend.request(method, params, std::move(done));
    }
  signals:
    void changed();
    void eventReady(const QVariantMap &event);

  private:
    IExtensionBackend &m_backend;
};
} // namespace nekotune
