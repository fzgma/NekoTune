#pragma once
#include "domain/result.h"
#include <QObject>
#include <QVariantMap>
#include <functional>

namespace nekotune {
class IAppearanceBackend : public QObject {
    Q_OBJECT
  public:
    using Completion = std::function<void(Result<QVariantMap>)>;
    using QObject::QObject;
    virtual void request(const QString &method, const QVariantMap &params, Completion done) = 0;
    virtual QVariantMap selections() const = 0;
  signals:
    void eventReady(const QVariantMap &event);
};
} // namespace nekotune
