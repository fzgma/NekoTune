#pragma once
#include "domain/playback/listening_history.h"
#include "storage/database_session.h"
namespace nekotune {
class ListeningHistoryRepository final : public IListeningHistoryRepository {
  public:
    explicit ListeningHistoryRepository(DatabaseSession &database) : m_database(database) {}
    Result<void> recover() override;
    Result<void> save(const ListeningSession &, const QVector<ListeningInterval> &) override;
    Result<HistoryPage> list(const HistoryFilter &) const override;
    Result<ListeningStats> statistics(const QVector<ListeningDay> &, qint64 weekStart) const override;
    Result<void> clear() override;

  private:
    DatabaseSession &m_database;
};
} // namespace nekotune
