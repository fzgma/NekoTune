import QtQuick
import QtTest
import "../../frontend/qt-qml/qml/components" as Components

Item {
    id: scene
    width: 900
    height: 640

    Component { id: queueComponent; Components.QueuePanel { width: scene.width; height: scene.height } }
    Component { id: libraryComponent; Components.LibraryPanel { width: scene.width; height: scene.height; controller: testLibrary } }
    QtObject {
        id: queueController
        property var lastAdd: null
        signal requestSucceeded(string method)
        signal requestFailed(string method, string message)
        function addSongs(ids, next) { lastAdd = {ids: ids, next: next} }
    }

    TestCase {
        id: tests
        name: "QueueReorder"
        when: windowShown
        property var panel: null
        property bool libraryMode: false
        SignalSpy { id: moveSpy; target: tests.libraryMode ? null : tests.panel; signalName: "moveRequested" }
        SignalSpy { id: librarySpy; target: tests.libraryMode ? tests.panel : null; signalName: "queueSongsRequested" }

        function init() {
            libraryMode = false
            panel = createTemporaryObject(queueComponent, scene, {
                queueController: queueController,
                queue: [
                    {queue_id: 7, song_id: 3, title: "First", state: "current"},
                    {queue_id: 9, song_id: 3, title: "Duplicate"},
                    {queue_id: 12, song_id: 4, title: "Last"}
                ]
            })
            verify(panel !== null)
            moveSpy.clear()
            queueController.lastAdd = null
            waitForRendering(panel)
        }
        function cleanup() { panel = null }
        function row(id) {
            const item = findChild(panel, "queueRow" + id)
            verify(item !== null)
            return item
        }
        function drag(from, to) {
            const source = row(from), target = row(to)
            const handle = findChild(source, "queueDragHandle")
            const endpoint = target.mapToItem(handle, 12, target.height / 2)
            mousePress(handle, handle.width / 2, handle.height / 2)
            mouseMove(handle, handle.width / 2, endpoint.y, 60)
            mouseRelease(handle, handle.width / 2, endpoint.y)
        }
        function test_dragCurrentAndDuplicateUsesOccurrenceIds_data() {
            return [{tag: "page", compact: false}, {tag: "drawer", compact: true}]
        }
        function test_dragCurrentAndDuplicateUsesOccurrenceIds(data) {
            panel.compact = data.compact
            waitForRendering(panel)
            drag(7, 12)
            compare(moveSpy.count, 1)
            compare(moveSpy.signalArguments[0][0], 7)
            compare(moveSpy.signalArguments[0][1], 2)
            compare(panel.queue[0].queue_id, 7) // Wait for the authoritative reply.
            compare(panel.pendingMethod, "queue.move")
            panel.requestSucceeded("queue.move")
            drag(9, 7)
            compare(moveSpy.count, 2)
            compare(moveSpy.signalArguments[1][0], 9)
            compare(moveSpy.signalArguments[1][1], 0)
        }
        function test_failedReorderCanRetryAndCancelledDragDoesNotSend() {
            drag(7, 12)
            panel.requestFailed("queue.move", "Storage failed")
            compare(panel.operationError, "Storage failed")
            compare(panel.queue[0].queue_id, 7)
            drag(7, 12)
            compare(moveSpy.count, 2)
            panel.requestSucceeded("queue.move")
            const handle = findChild(row(7), "queueDragHandle")
            mousePress(handle, handle.width / 2, handle.height / 2)
            panel.connected = false
            mouseRelease(handle, handle.width / 2, handle.height / 2)
            compare(moveSpy.count, 2)
            compare(panel.draggedQueueId, 0)
        }
        function test_reorderDisabledForSearchAndPlaylists() {
            panel.searchText = "Duplicate"
            compare(panel.canReorder, false)
            panel.searchText = ""
            compare(panel.canReorder, true)
            panel.playlists = [{id: 1, name: "Saved", items: [{song_id: 3, title: "Song"}]}]
            panel.currentPlaylist = 1
            compare(panel.canReorder, false)
        }
        function test_dragScrollsLongQueue() {
            const songs = []
            for (let i = 0; i < 24; ++i) songs.push({queue_id: 100 + i, song_id: 3, title: "Song " + i})
            panel.queue = songs
            waitForRendering(panel)
            const list = findChild(panel, "queueSongList")
            const handle = findChild(row(100), "queueDragHandle")
            const endpoint = list.mapToItem(handle, 12, list.height - 8)
            const initialY = list.contentY
            mousePress(handle, handle.width / 2, handle.height / 2)
            mouseMove(handle, handle.width / 2, endpoint.y, 60)
            tryVerify(() => list.contentY > initialY + 24)
            const targetIndex = panel.dropIndex
            mouseRelease(handle, handle.width / 2, endpoint.y)
            compare(moveSpy.count, 1)
            compare(moveSpy.signalArguments[0][0], 100)
            verify(moveSpy.signalArguments[0][1] >= targetIndex)
        }
        function test_queueMenusInsertNewOccurrence() {
            row(9).openMenu(row(9))
            const menu = findChild(row(9), "trackActionMenu")
            tryCompare(menu, "opened", true)
            const next = findChild(menu.contentItem, "menu_next")
            verify(next !== null)
            mouseClick(next)
            compare(queueController.lastAdd.ids.join(","), "3")
            compare(queueController.lastAdd.next, true)
            panel.songAction("queue", panel.queue[1])
            compare(queueController.lastAdd.next, false)
        }
        function test_songAndAlbumMenusPreserveTrackOrder() {
            panel.destroy()
            panel = null
            libraryMode = true
            testFixtures.seedLibrary({songs: [
                {song_id: 1, title: "Two", album: "Album", artist: "Artist", track_number: 2, available: true},
                {song_id: 2, title: "One", album: "Album", artist: "Artist", track_number: 1, available: true},
                {song_id: 3, title: "Missing", album: "Album", artist: "Artist", track_number: 3, available: false}
            ], tags: []})
            panel = createTemporaryObject(libraryComponent, scene)
            verify(panel !== null)
            librarySpy.clear()
            waitForRendering(panel)
            const song = findChild(panel, "libraryRow1")
            verify(song !== null)
            song.openMenu(song)
            const menu = findChild(song, "trackActionMenu")
            tryCompare(menu, "opened", true)
            mouseClick(findChild(menu.contentItem, "menu_next"))
            compare(librarySpy.count, 1)
            compare(librarySpy.signalArguments[0][0].join(","), "1")
            compare(librarySpy.signalArguments[0][1], true)
            testLibrary.albumView = true
            waitForRendering(panel)
            const more = findChild(panel, "albumMore0")
            verify(more !== null)
            mouseClick(more)
            const album = findChild(panel, "libraryAlbum0")
            const albumMenu = findChild(album, "albumActionMenu")
            tryCompare(albumMenu, "opened", true)
            const append = findChild(albumMenu.contentItem, "menu_queue")
            verify(append !== null)
            mouseClick(append)
            compare(librarySpy.count, 2)
            compare(librarySpy.signalArguments[1][0].join(","), "2,1")
            compare(librarySpy.signalArguments[1][1], false)
        }
    }
}
