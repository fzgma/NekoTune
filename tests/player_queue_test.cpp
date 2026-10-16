#include "domain/playback/player_queue.h"
#include "ipc/serialization/serialization.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QtTest/QtTest>

class PlayerQueueTest final : public QObject {
    Q_OBJECT

  private slots:
    void addAndRemoveQueueItems();
    void moveKeepsOccurrenceAndSelection();
    void insertKeepsSelectionAndDuplicates();
};

// A song may occur more than once in the playback queue.
// Each occurrence needs its own stable ID while sharing the same song metadata.
// The serialized title must prefer the stored custom value over the source filename.
// Updating lyrics and metadata must reach every occurrence of that song.
// Removing an earlier row shifts the index without changing the active occurrence identity.
// This is why queue commands cannot substitute a library song ID for a queue ID.
void PlayerQueueTest::addAndRemoveQueueItems() {
    nekotune::PlayerQueue queue;

    const nekotune::SongMetadata firstMetadata{
        1, QStringLiteral("aaaaaaaa"), QStringLiteral("/music/first.mp3"), {}, {}, {},
    };
    const nekotune::SongMetadata secondMetadata{
        2,
        QStringLiteral("bbbbbbbb"),
        QStringLiteral("/music/second.wav"),
        QStringLiteral("Custom Second"),
        QStringLiteral("Second Artist"),
        QStringLiteral("second lyrics"),
    };

    const int firstId = queue.add(QStringLiteral("/music/first.mp3"), firstMetadata);
    const int secondId = queue.add(QStringLiteral("/music/second.wav"), secondMetadata);
    const int repeatedSecondId = queue.add(QStringLiteral("/music/second-copy.wav"), secondMetadata);

    QJsonArray items = nekotune::queueJson(queue);
    QCOMPARE(items.size(), 3);
    QCOMPARE(items.at(0).toObject().value(QStringLiteral("title")).toString(), QStringLiteral("first"));
    QCOMPARE(items.at(1).toObject().value(QStringLiteral("title")).toString(),
             QStringLiteral("Custom Second"));
    QCOMPARE(items.at(0).toObject().value(QStringLiteral("song_id")).toInt(), 1);
    QCOMPARE(items.at(0).toObject().value(QStringLiteral("song_hash")).toString(), firstMetadata.hash);
    QCOMPARE(items.at(1).toObject().value(QStringLiteral("song_id")).toInt(), 2);
    QCOMPARE(items.at(1).toObject().value(QStringLiteral("song_hash")).toString(), secondMetadata.hash);
    QCOMPARE(items.at(2).toObject().value(QStringLiteral("song_id")).toInt(), 2);
    QCOMPARE(items.at(2).toObject().value(QStringLiteral("song_hash")).toString(), secondMetadata.hash);
    QCOMPARE(queue.indexById(secondId), 1);

    QVERIFY(queue.setCurrentIndex(1));
    queue.markCurrent();
    QCOMPARE(nekotune::currentSongJson(queue).value(QStringLiteral("id")).toInt(), secondId);
    QCOMPARE(nekotune::currentSongJson(queue).value(QStringLiteral("queue_id")).toInt(), secondId);
    QCOMPARE(nekotune::currentSongJson(queue).value(QStringLiteral("song_id")).toInt(), 2);
    QCOMPARE(nekotune::currentSongJson(queue).value(QStringLiteral("song_hash")).toString(),
             secondMetadata.hash);
    QCOMPARE(nekotune::currentSongJson(queue).value(QStringLiteral("lyrics")).toString(),
             secondMetadata.lyrics);

    nekotune::SongMetadata updatedSecond = secondMetadata;
    updatedSecond.customTitle = QStringLiteral("Updated Second");
    queue.updateSongMetadata(updatedSecond);
    QCOMPARE(nekotune::currentSongJson(queue).value(QStringLiteral("title")).toString(),
             QStringLiteral("Updated Second"));

    QVERIFY(queue.removeAt(queue.indexById(firstId)));
    QCOMPARE(queue.currentIndex(), 0);

    items = nekotune::queueJson(queue);
    QCOMPARE(items.size(), 2);
    QCOMPARE(items.at(0).toObject().value(QStringLiteral("title")).toString(),
             QStringLiteral("Updated Second"));
    QCOMPARE(items.at(1).toObject().value(QStringLiteral("id")).toInt(), repeatedSecondId);

    QCOMPARE(queue.indexById(firstId), -1);
}

void PlayerQueueTest::moveKeepsOccurrenceAndSelection() {
    nekotune::PlayerQueue queue;
    const nekotune::SongMetadata song{17, "duplicate"};
    const int first = queue.add("a", song), current = queue.add("a", song), last = queue.add("a", song);
    QVERIFY(queue.setCurrentIndex(1));
    QVERIFY(queue.move(current, 2));
    QCOMPARE(queue.currentIndex(), 2);
    QCOMPARE(queue.at(2).id, current);
    QVERIFY(queue.move(current, 0));
    QCOMPARE(queue.currentIndex(), 0);
    QVERIFY(queue.move(last, 0)); // Cross the current item from behind.
    QCOMPARE(queue.currentIndex(), 1);
    QVERIFY(queue.move(last, 2)); // Cross it in the other direction.
    QCOMPARE(queue.currentIndex(), 0);
    QCOMPARE(queue.at(1).id, first);
    QCOMPARE(queue.at(2).id, last);
    QVERIFY(!queue.move(17, 0)); // Library identity is not an occurrence identity.
    QVERIFY(!queue.move(first, -1));
    QVERIFY(!queue.move(first, queue.size()));
    QCOMPARE(queue.at(queue.currentIndex()).id, current);
    queue.setCurrentIndex(-1);
    QVERIFY(queue.move(first, 0));
    QCOMPARE(queue.currentIndex(), -1);
}

void PlayerQueueTest::insertKeepsSelectionAndDuplicates() {
    nekotune::PlayerQueue queue;
    const nekotune::SongMetadata song{17, "duplicate"};
    const int current = queue.add("a", song);
    QVERIFY(queue.setCurrentIndex(0));
    QVERIFY(!queue.insert(-1, "a", song));
    QVERIFY(!queue.insert(2, "a", song));
    const int before = queue.insert(0, "a", song);
    QCOMPARE(before, current + 1);
    QCOMPARE(queue.currentIndex(), 1);
    const int next = queue.insertAfterCurrent("a", song);
    QCOMPARE(queue.at(2).id, next);
    QVERIFY(queue.at(2).playNext);
    QCOMPARE(queue.at(queue.currentIndex()).id, current);
    queue.markCurrent();
    QCOMPARE(queue.at(1).state, QString("current"));
    QVERIFY(queue.setCurrentIndex(2));
    QVERIFY(!queue.at(2).playNext);
    queue.clear();
    const int first = queue.insertAfterCurrent("a", song);
    QCOMPARE(queue.at(0).id, first);
    QCOMPARE(queue.currentIndex(), -1);
    QVERIFY(queue.records().at(0).playNext);
}

QTEST_APPLESS_MAIN(PlayerQueueTest)

#include "player_queue_test.moc"
