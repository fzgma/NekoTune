import QtQuick
import QtTest
import "../../frontend/qt-qml/qml/components" as PlayerComponents

Rectangle {
    color: "#0E0D14"
    id: scene
    width: 900
    height: 640

    QtObject {
        id: i18n
        property string language: "en"
        function text(key, language) { return key.replace(/_/g, " ") }
        function countText(key, count, language) { return count + " tracks" }
    }

    Component {
        id: panelComponent
        PlayerComponents.LibraryPanel { width: scene.width; height: scene.height }
    }

    TestCase {
        id: tests
        name: "Library"
        when: windowShown
        property var panel: null
        SignalSpy { id: playSpy; target: tests.panel; signalName: "playRequested" }
        SignalSpy { id: playlistSpy; target: tests.panel; signalName: "playlistRequested" }
        SignalSpy { id: deleteSpy; target: tests.panel; signalName: "deleteRequested" }

        function init() {
            testFixtures.seedLibrary({
                    tags: [{id: 1, name: "Rock"}, {id: 2, name: "Live"}],
                    songs: [
                        {song_id: 1, title: "Both", available: true, tags: [{id: 1, name: "Rock"}, {id: 2, name: "Live"}]},
                        {song_id: 2, title: "Rock only", available: true, tags: [{id: 1, name: "Rock"}]},
                        {song_id: 3, title: "Missing", available: false, tags: [{id: 1, name: "Rock"}, {id: 2, name: "Live"}]}
                    ]
            })
            panel = createTemporaryObject(panelComponent, scene, {
                controller: testLibrary,
                playlists: [{id: 8, name: "Favorites", items: []}]
            })
            verify(panel !== null)
            playSpy.clear()
            playlistSpy.clear()
            deleteSpy.clear()
        }

        function cleanup() { panel = null }

        function visibleIds() { return panel.filteredSongs.map(song => song.song_id).join(",") }

        function choose(name, value) {
            const picker = findChild(panel, name)
            const index = picker.model.findIndex(option => option.value === value)
            verify(index >= 0)
            mouseClick(picker)
            tryCompare(picker.popup, "opened", true)
            keyClick(Qt.Key_Home)
            for (let i = 0; i < index; ++i) keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Return)
            tryCompare(picker.popup, "opened", false)
            compare(picker.currentValue, value)
        }

        function test_recentlyPlayedSortAndTimeFilters() {
            const now = Date.now()
            const day = 24 * 60 * 60 * 1000
            const stamp = days => new Date(now - days * day).toISOString()
            testFixtures.seedLibrary({tags: [{id: 1, name: "Rock"}], songs: [
                {song_id: 1, title: "Never recorded", play_count: 8, last_played_at: "", available: true},
                {song_id: 2, title: "Yesterday", play_count: 1, last_played_at: stamp(1), available: true, tags: [{id: 1}]},
                {song_id: 3, title: "Two weeks", play_count: 1, last_played_at: stamp(14), available: true, tags: [{id: 1}]},
                {song_id: 4, title: "Older", play_count: 1, last_played_at: stamp(31), available: true},
                {song_id: 5, title: "Invalid time", play_count: 1, last_played_at: "invalid", available: true},
                {song_id: 6, title: "Same instant", play_count: 2, last_played_at: stamp(1), available: true},
                {song_id: 7, title: "Different offset", play_count: 1, last_played_at: new Date(now - 2 * day).toISOString().replace("Z", "+00:00"), available: true}
            ]})
            const original = testLibrary.songs.items.map(song => song.last_played_at).join(",")
            choose("librarySortPicker", "recently_played")
            compare(visibleIds(), "2,6,7,3,4,1,5")
            choose("libraryPlayFilterPicker", "last_7_days")
            compare(visibleIds(), "2,6,7")
            choose("libraryPlayFilterPicker", "last_30_days")
            compare(visibleIds(), "2,6,7,3")
            testLibrary.searchText = "weeks"
            compare(visibleIds(), "3")
            testLibrary.searchText = ""
            testLibrary.toggleTag(1)
            compare(visibleIds(), "2,3")
            compare(testLibrary.songs.items.map(song => song.last_played_at).join(","), original)
        }
        function test_recentFiltersExcludeOutOfWindowAndFutureTimes() {
            const now = Date.now(), day = 24 * 60 * 60 * 1000
            testFixtures.seedLibrary({tags: [], songs: [
                {song_id: 1, title: "Within seven", last_played_at: new Date(now - 7 * day + 60000).toISOString()},
                {song_id: 2, title: "Outside seven", last_played_at: new Date(now - 7 * day - 60000).toISOString()},
                {song_id: 3, title: "Within thirty", last_played_at: new Date(now - 30 * day + 60000).toISOString()},
                {song_id: 4, title: "Outside thirty", last_played_at: new Date(now - 30 * day - 60000).toISOString()},
                {song_id: 5, title: "Future", last_played_at: new Date(now + day).toISOString()}
            ]})
            testLibrary.playFilter = "last_7_days"
            compare(visibleIds(), "1")
            testLibrary.playFilter = "last_30_days"
            compare(visibleIds(), "1,2,3")
        }
        function test_sortingAndCombinedFilters() {
            testFixtures.seedLibrary({tags: [{id: 1, name: "Rock"}], songs: [
                {song_id: 3, title: "Track 10", artist: "Beta", album: "B", duration_ms: 100000, play_count: 7, import_source: "demo", available: true, tags: [{id: 1}]},
                {song_id: 1, title: "Track 2", artist: "Alpha", album: "A", duration_ms: 200000, play_count: 7, import_source: "local", available: true, tags: [{id: 1}]},
                {song_id: 2, title: "Track 3", artist: "", album: "", duration_ms: 0, play_count: 0, import_source: "demo", available: false, tags: [{id: 1}]},
                {song_id: 4, title: "Other", duration_ms: 50000, play_count: 0, available: true}
            ]})
            compare(visibleIds(), "1,2,3,4")
            testLibrary.selectRow(1)
            choose("librarySortPicker", "most_played")
            compare(visibleIds(), "1,3,2,4")
            compare(testLibrary.selectedSongIds.join(","), "1")
            testLibrary.selectRow(2, false, true)
            compare(testLibrary.selectedSongIds.join(","), "1,3,2")
            choose("librarySortPicker", "added_desc")
            compare(visibleIds(), "4,3,2,1")
            choose("librarySortPicker", "title")
            compare(visibleIds(), "4,1,2,3")
            choose("librarySortPicker", "artist")
            compare(visibleIds(), "1,3,2,4")
            choose("librarySortPicker", "album")
            compare(visibleIds(), "1,3,2,4")
            choose("librarySortPicker", "duration_asc")
            compare(visibleIds(), "4,3,1,2")
            choose("librarySortPicker", "duration_desc")
            compare(visibleIds(), "1,3,4,2")
            choose("librarySortPicker", "least_played")
            compare(visibleIds(), "2,4,1,3")
            choose("librarySourcePicker", "demo")
            compare(visibleIds(), "2,3")
            compare(testLibrary.selectedSongIds.join(","), "3,2")
            choose("libraryPlayFilterPicker", "never_played")
            compare(visibleIds(), "2")
            compare(panel.playableCount, 0)
            verify(!findChild(panel, "playFilteredButton").enabled)
            testLibrary.selectedTagIds = [1]
            testLibrary.searchText = "track 3"
            compare(visibleIds(), "2")
            // Available source choices come from the whole library, even with no matches.
            choose("librarySourcePicker", "local")
            compare(visibleIds(), "")
            compare(testLibrary.importSources.join(","), "demo,local")
            compare(findChild(panel, "libraryEmptyMessage").text, "library no filter matches")
            tryCompare(findChild(panel, "libraryEmptyResetButton"), "visible", true)
            mouseClick(findChild(panel, "libraryEmptyResetButton"))
            compare(visibleIds(), "1,2,3,4")
            compare(testLibrary.sortOrder, "added_asc")
            compare(testLibrary.importSource, "")
            compare(testLibrary.searchText, "")
            choose("libraryPlayFilterPicker", "played")
            compare(visibleIds(), "1,3")
            compare(testLibrary.songs.items.map(song => song.song_id).join(","), "3,1,2,4")
            compare(playSpy.count, 0)
        }

        function test_sourceFallbackAndInvalidOptions() {
            testFixtures.seedLibrary({tags: [], songs: [
                {song_id: 1, title: "Downloaded", import_source: "demo", source: {kind: "local"}, play_count: 3000000000},
                {song_id: 2, title: "Legacy remote", source: {provider_id: "demo/tracks"}, play_count: 2},
                {song_id: 3, title: "Legacy local"}
            ]})
            testLibrary.importSource = "demo"
            testLibrary.sortOrder = "most_played"
            compare(visibleIds(), "1,2")
            testLibrary.sortOrder = "invalid"
            testLibrary.playFilter = "invalid"
            compare(testLibrary.sortOrder, "most_played")
            compare(testLibrary.playFilter, "all")
            testLibrary.importSource = "removed.provider"
            compare(visibleIds(), "")
            compare(findChild(panel, "librarySourcePicker").currentValue, "removed.provider")
            testLibrary.importSource = "local"
            testLibrary.playFilter = "never_played"
            compare(visibleIds(), "3")
        }

        function test_albumGroupingAndTrackOrder() {
            testFixtures.seedLibrary({tags: [], songs: [
                {song_id: 1, title: "Finale", artist: "Singer A", album: "Moon", album_artist: "Various", disc_number: 2, track_number: 1, available: true},
                {song_id: 2, title: "Opening", artist: "Singer B", album: "moon", album_artist: "Various", disc_number: 1, track_number: 8, available: true},
                {song_id: 3, title: "Different record", artist: "Other", album: "Moon", disc_number: 1, track_number: 1, available: true},
                {song_id: 4, title: "Untitled album", available: false}
            ]})
            testLibrary.albumView = true
            compare(testLibrary.albums.count, 3)
            verify(findChild(panel, "libraryAlbumGrid").visible)
            const album = testLibrary.albums.items.find(item => item.artist === "Various")
            compare(album.song_count, 2)
            testLibrary.openAlbum(album.album_id)
            compare(visibleIds(), "2,1")
            compare(testLibrary.sortOrder, "track")
            verify(!findChild(panel, "libraryAlbumGrid").visible)
            testLibrary.selectRow(2)
            waitForRendering(panel)
            mouseClick(findChild(panel, "libraryAlbumsBack"))
            compare(testLibrary.selectedAlbumId, "")
            compare(testLibrary.selectedSongIds.length, 0)
            compare(visibleIds(), "3,2,1,4")
            testLibrary.searchText = "Moon"
            compare(testLibrary.albums.count, 2)
            const grid = findChild(panel, "libraryAlbumGrid")
            waitForRendering(panel)
            tryVerify(() => grid.itemAtIndex(0) !== null)
            mouseClick(grid.itemAtIndex(0))
            verify(testLibrary.selectedAlbumId !== "")
            compare(playSpy.count, 0)
            testLibrary.albumView = false
            compare(testLibrary.selectedAlbumId, "")
            compare(testLibrary.albums.count, 2)
        }

        function test_focusRingIsNotClippedByList() {
            waitForRendering(panel)
            const list = findChild(panel, "librarySongList")
            const row = findChild(panel, "libraryRow1")
            verify(row !== null)
            row.forceActiveFocus()
            const ring = findChild(row, "trackFocusRing")
            verify(ring.visible)
            const topLeft = ring.mapToItem(list, 0, 0)
            const bottomRight = ring.mapToItem(list, ring.width, ring.height)
            verify(topLeft.x >= 0 && topLeft.y >= 0, "Focus ring must fit at the first row's top/left edge")
            verify(bottomRight.x <= list.width && bottomRight.y <= list.height)
            const path = testFixtures.screenshotPath("library-keyboard-focus")
            if (path) { waitForRendering(panel); grabImage(scene).save(path) }
        }

        // Multiple selected tags filter by intersection, including unavailable visible songs.
        // The playable count and the submitted visible context are therefore different quantities.
        // Starting a row uses its song identity, not its index in the filtered array.
        // The backend receives the context needed to skip unavailable entries consistently.
        function test_intersectionAndPlaybackIdentity() {
            compare(panel.filteredSongs.length, 3)
            testLibrary.selectedTagIds = [1, 2]
            compare(panel.filteredSongs.length, 2)
            compare(panel.filteredSongs[0].song_id, 1)
            compare(panel.playableCount, 1)
            waitForRendering(panel)
            mouseClick(findChild(panel, "playFilteredButton"))
            compare(playSpy.count, 1)
            compare(playSpy.signalArguments[0][0].length, 2)
            compare(playSpy.signalArguments[0][1], 0)
            testLibrary.selectRow(1)
            waitForRendering(panel)
            mouseClick(findChild(panel, "playLibrarySongButton1"))
            compare(playSpy.count, 2)
            compare(playSpy.signalArguments[1][1], 1)
        }

        // Selection and playback are separate desktop interactions.
        // The complete double-click sequence must produce one play command despite its press events.
        // The selection checkbox must not bubble into the row's playback action.
        // The wait separates the initial click from the later double-click gesture.
        function test_clickSelectsAndDoubleClickPlaysOnce() {
            waitForRendering(panel)
            const row = findChild(panel, "libraryRow1")
            verify(row !== null)
            mouseClick(row, 180, 32)
            compare(playSpy.count, 0)
            compare(panel.selectedSongIds.length, 1)
            compare(panel.selectedSongIds[0], 1)
            wait(500)
            mouseDoubleClickSequence(row, 180, 32)
            compare(playSpy.count, 1)
            compare(playSpy.signalArguments[0][1], 1)
            mouseClick(findChild(panel, "librarySelectSong1"))
            compare(playSpy.count, 1)
        }

        // Toggle and range selection operate on the current visible order.
        // Changing search or tag filters removes selections that no longer belong to that scope.
        // Trying to select a hidden song must not reintroduce an invisible selected ID.
        // Search is case-insensitive and combines with the selected-tag intersection.
        function test_searchAndDesktopSelection() {
            testLibrary.selectRow(1)
            testLibrary.selectRow(2, true)
            compare(panel.selectedSongIds.length, 2)
            testLibrary.selectRow(3, false, true)
            compare(panel.selectedSongIds.length, 2)
            compare(panel.selectedSongIds[0], 2)
            compare(panel.selectedSongIds[1], 3)
            testLibrary.searchText = "ROCK ONLY"
            compare(panel.filteredSongs.length, 1)
            compare(panel.filteredSongs[0].song_id, 2)
            compare(panel.selectedSongIds.length, 0)
            testLibrary.selectRow(1)
            compare(panel.selectedSongIds.length, 0)
            testLibrary.searchText = ""
            testLibrary.selectedTagIds = [1, 2]
            testLibrary.searchText = "both"
            compare(panel.filteredSongs.length, 1)
            testLibrary.searchText = "not present"
            compare(panel.filteredSongs.length, 0)
            compare(panel.playableCount, 0)
        }

        // Select-all targets the filtered list, including entries whose files are unavailable.
        // Opening confirmation must not send the deletion before the user confirms.
        // A database failure retains selection and the dialog for retry.
        // Only successful completion clears selection and closes the confirmation.
        function test_selectFilteredAndDeleteConfirmation() {
            testLibrary.selectedTagIds = [1, 2]
            compare(panel.filteredSongs.length, 2)
            waitForRendering(panel)
            mouseClick(findChild(panel, "librarySelectAllButton"))
            compare(panel.selectedSongIds.length, 2)
            compare(panel.selectedSongIds[0], 1)
            compare(panel.selectedSongIds[1], 3)
            mouseClick(findChild(panel, "librarySelectSong3"))
            compare(panel.selectedSongIds.length, 1)
            compare(panel.selectedSongIds[0], 1)
            mouseClick(findChild(panel, "libraryDeleteSelectedButton"))
            const popup = findChild(panel, "libraryDeletePopup")
            tryCompare(popup, "opened", true)
            compare(deleteSpy.count, 0)
            verify(findChild(panel, "libraryCleanFilesCheckBox").checked)
            mouseClick(findChild(panel, "libraryConfirmDeleteButton"))
            compare(deleteSpy.count, 1)
            compare(deleteSpy.signalArguments[0][0].length, 1)
            compare(deleteSpy.signalArguments[0][0][0], 1)
            compare(deleteSpy.signalArguments[0][1], true)
            compare(panel.deletePending, true)
            panel.deleteFailed("Database error")
            compare(panel.selectedSongIds.length, 1)
            compare(panel.deleteError, "Database error")
            panel.deleteSucceeded()
            compare(panel.selectedSongIds.length, 0)
            tryCompare(popup, "opened", false)
        }

        // The file-cleanup choice is frozen while the deletion request is pending.
        // A committed database deletion may still report an unlink cleanup warning.
        // That partial success closes confirmation while making the remaining staged path visible.
        // Opening another deletion resets both the warning and the default cleanup choice.
        // The database-only option is sent as a Boolean with the selected target snapshot.
        function test_cleanupChoiceAndPartialFailure() {
            const actions = panel.songActions(panel.filteredSongs[0])
            const deletionActions = actions.filter(action => action.destructive)
            compare(deletionActions.length, 1)
            compare(deletionActions[0].key, "delete")
            compare(deletionActions[0].label, "delete")
            panel.songAction("delete", panel.filteredSongs[0])
            const popup = findChild(panel, "libraryDeletePopup")
            tryCompare(popup, "opened", true)
            const choice = findChild(panel, "libraryCleanFilesCheckBox")
            verify(choice.checked)
            mouseClick(findChild(panel, "libraryConfirmDeleteButton"))
            compare(deleteSpy.count, 1)
            compare(deleteSpy.signalArguments[0][1], true)
            verify(!choice.enabled)
            panel.deleteSucceeded(["/managed/.song.removing"])
            tryCompare(popup, "opened", false)
            verify(findChild(panel, "libraryCleanupWarning").visible)
            verify(panel.cleanupWarning.indexOf("/managed/.song.removing") !== -1)
            panel.songAction("delete", panel.filteredSongs[0])
            tryCompare(popup, "opened", true)
            compare(panel.deletingSongIds.length, 1)
            verify(findChild(panel, "libraryConfirmDeleteButton").enabled)
            verify(choice.checked)
            compare(panel.cleanupWarning, "")
            mouseClick(choice)
            verify(!panel.cleanManagedFiles)
            verify(!panel.deletePending)
            verify(findChild(panel, "libraryConfirmDeleteButton").enabled)
            mouseClick(findChild(panel, "libraryConfirmDeleteButton"))
            compare(deleteSpy.count, 2)
            compare(deleteSpy.signalArguments[1][1], false)
            panel.deleteSucceeded()
            tryCompare(popup, "opened", false)
            panel.songAction("delete", panel.filteredSongs[0])
            tryCompare(popup, "opened", true)
            verify(choice.checked)
            const path = testFixtures.screenshotPath("library-cleanup-confirmation")
            if (path) {
                wait(160)
                waitForRendering(panel)
                grabImage(popup.parent.parent).save(path)
            }
        }

        // Rows display stored durations even when they have never been selected or played.
        // Only the active song may temporarily use the live backend duration.
        // Switching active identity restores the previous row's persisted value.
        // Unknown durations use the placeholder rather than an invented zero-length time.
        function test_persistedDurationDoesNotDependOnSelectionOrPlayback() {
            testFixtures.seedLibrary({tags: [], songs: [
                {song_id: 1, title: "夜空", artist: "示例歌手", available: true, duration_ms: 192000},
                {song_id: 2, title: "Magical Lights", artist: "示例歌手", available: true, duration_ms: 245000},
                {song_id: 3, title: "未知时长", available: false, duration_ms: 0}
            ]})
            waitForRendering(panel)
            const first = findChild(findChild(panel, "libraryRow1"), "trackDuration")
            const second = findChild(findChild(panel, "libraryRow2"), "trackDuration")
            const missing = findChild(findChild(panel, "libraryRow3"), "trackDuration")
            compare(first.text, "3:12")
            compare(second.text, "4:05")
            compare(missing.text, "--:--")
            const path = testFixtures.screenshotPath("library-persisted-durations")
            if (path) {
                waitForRendering(panel)
                wait(150)
                grabImage(scene).save(path)
            }
            testLibrary.selectRow(2)
            compare(first.text, "3:12")
            compare(second.text, "4:05")
            panel.currentSong = {song_id: 1}
            panel.currentDuration = 193000
            compare(first.text, "3:13")
            compare(second.text, "4:05")
            panel.currentSong = {song_id: 2}
            panel.currentDuration = 245000
            compare(first.text, "3:12")
        }

        // A library row has a song ID and need not have any queue occurrence.
        // Adding it to a playlist must submit that song identity with the target playlist ID.
        // A failed save retains the popup and target so the user can retry.
        // Closing the popup is deferred until the matching successful operation completes.
        function test_addFromLibraryUsesSongId() {
            waitForRendering(panel)
            panel.songAction("playlist", panel.filteredSongs[0])
            const popup = findChild(panel, "libraryPlaylistPopup")
            tryCompare(popup, "opened", true)
            panel.addSongToPlaylist(8)
            compare(playlistSpy.count, 1)
            compare(playlistSpy.signalArguments[0][0], "add")
            compare(playlistSpy.signalArguments[0][1].id, 8)
            compare(playlistSpy.signalArguments[0][1].song_id, 1)
            compare(panel.addPending, true)
            panel.playlistFailed("Storage unavailable")
            compare(panel.operationError, "Storage unavailable")
            compare(popup.opened, true)
            panel.addSongToPlaylist(8)
            panel.playlistSucceeded()
            tryCompare(popup, "opened", false)
        }
    }
}
