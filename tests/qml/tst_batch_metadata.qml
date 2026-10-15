import QtQuick
import QtTest
import "../../frontend/qt-qml/qml/dialogs" as Dialogs

Rectangle {
    id: scene
    width: 900; height: 700
    color: "#0E0D14"
    QtObject {
        id: fakeController
        property var submitted: null
        signal batchMetadataSaved(int count)
        signal requestFailed(string method, string message)
        function saveBatchMetadata(ids, patch, mode) { submitted = {ids: ids.slice(), patch: patch, mode: mode} }
    }
    Component { id: editorComponent; Dialogs.BatchMetadataEditor { controller: fakeController; translator: testTranslator } }
    TestCase {
        name: "BatchMetadata"
        when: windowShown
        property var editor: null
        function field(name) {
            function visit(item) {
                if (item.objectName === name) return item
                for (const child of item.children || []) {
                    const found = visit(child)
                    if (found) return found
                }
                return null
            }
            return visit(editor.contentItem)
        }
        function init() {
            testTranslator.language = "zh"
            fakeController.submitted = null
            editor = createTemporaryObject(editorComponent, scene)
            verify(editor !== null)
            editor.openForSongs([1, 2])
            tryCompare(editor, "opened", true)
        }
        function cleanup() { editor.close(); editor = null }
        function test_sparseFieldsFrozenTargetsAndRetry() {
            const targets = [1, 2]
            editor.openForSongs(targets)
            targets.push(3)
            editor.save()
            verify(editor.error.length > 0)
            compare(fakeController.submitted, null)
            const applyArtist = field("batchApply_artist")
            mouseClick(applyArtist)
            verify(applyArtist.checked)
            field("batchValue_artist").text = "New artist"
            // Unchecked drafts are deliberately excluded, even when not empty.
            field("batchValue_custom_title").text = "Must not overwrite titles"
            mouseClick(field("batchMetadataSave"))
            compare(fakeController.submitted.ids.join(","), "1,2")
            compare(JSON.stringify(fakeController.submitted.patch), JSON.stringify({artist: "New artist"}))
            verify(editor.pending)
            verify(!applyArtist.enabled)
            editor.openForSongs([99])
            compare(editor.songIds.join(","), "1,2")
            fakeController.requestFailed("library.update_metadata", "Storage unavailable")
            verify(!editor.pending)
            compare(editor.error, "Storage unavailable")
            verify(editor.opened)
            compare(field("batchValue_artist").text, "New artist")
            mouseClick(field("batchMetadataSave"))
            fakeController.batchMetadataSaved(2)
            tryCompare(editor, "opened", false)
        }
        function test_explicitClearTagsAndNumberValidation() {
            field("batchApply_album").checked = true
            field("batchApply_year").checked = true
            field("batchValue_year").text = "10000"
            editor.save()
            compare(fakeController.submitted, null)
            verify(editor.error.length > 0)
            field("batchValue_year").text = "2024"
            field("batchApply_tags").checked = true
            field("batchTagMode").currentIndex = 1
            editor.save()
            compare(fakeController.submitted.patch.album, "")
            compare(fakeController.submitted.patch.year, 2024)
            compare(fakeController.submitted.patch.tags.length, 0)
            compare(fakeController.submitted.mode, "replace")
            fakeController.requestFailed("library.update_metadata", "Retry")
            const path = testFixtures.screenshotPath("batch-metadata")
            if (path) { waitForRendering(scene); grabImage(editor.background.parent).save(path) }
            const tagsPath = testFixtures.screenshotPath("batch-metadata-tags")
            if (tagsPath) {
                const scroll = findChild(editor, "batchMetadataScroll").contentItem
                scroll.contentY = Math.max(0, scroll.contentHeight - scroll.height)
                waitForRendering(scene)
                grabImage(editor.background.parent).save(tagsPath)
            }
        }
    }
}
