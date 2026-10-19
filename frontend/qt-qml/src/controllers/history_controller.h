#pragma once
#include "controllers/feature_controller.h"
#include "models/record_model.h"

class HistoryController final : public FeatureController {
    Q_OBJECT
    Q_PROPERTY(RecordModel *records READ records CONSTANT)
    Q_PROPERTY(QVariantMap stats READ stats NOTIFY changed)
    Q_PROPERTY(bool loading READ loading NOTIFY changed)
    Q_PROPERTY(bool loaded READ loaded NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(QString statsError READ statsError NOTIFY changed)
    Q_PROPERTY(int offset READ offset NOTIFY changed)
    Q_PROPERTY(qint64 total READ total NOTIFY changed)
  public:
    explicit HistoryController(IpcClient &client);
    RecordModel *records() { return &m_records; }
    QVariantMap stats() const { return m_stats; }
    bool loading() const { return m_loading; }
    bool loaded() const { return m_loaded; }
    QString error() const { return m_error; }
    QString statsError() const { return m_statsError; }
    int offset() const { return m_offset; }
    qint64 total() const { return m_total; }
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void filter(int days, const QString &search, bool completedOnly);
    Q_INVOKABLE void goToPage(int offset);
    Q_INVOKABLE void clearHistory();
  signals:
    void changed();

  private:
    void loadPage();
    RecordModel m_records{"id"};
    QVariantMap m_stats;
    QString m_search, m_error, m_statsError;
    bool m_completedOnly = false, m_loading = false, m_loaded = false;
    int m_days = 30, m_offset = 0;
    qint64 m_total = 0;
    quint64 m_generation = 0, m_statsGeneration = 0;
    QTimer m_refreshTimer, m_dateTimer;
};
