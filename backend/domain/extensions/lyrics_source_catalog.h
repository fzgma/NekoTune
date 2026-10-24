#pragma once
#include "domain/result.h"
#include <QObject>
#include <QVariantMap>

namespace nekotune {
/// Lyrics sources registered by running extensions; SDK 2 lyrics use JSON-compatible maps.
class ILyricsSourceCatalog : public QObject {
    Q_OBJECT
  public:
    using QObject::QObject;
    using Reply = std::function<void(Result<QVariantMap>)>;
    virtual QVariantList sources() const = 0;
    virtual void search(const QString &source, const QVariantMap &query, Reply done) = 0;
    virtual void resolve(const QString &source, const QVariantMap &candidate, Reply done) = 0;
  signals:
    void sourcesChanged();
};
} // namespace nekotune
