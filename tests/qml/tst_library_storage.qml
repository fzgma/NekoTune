import QtQuick
import QtTest
import "../../frontend/qt-qml/qml/components" as Components
Item {
    id: scene
    width: 900; height: 900
    QtObject {
        id: storage
        property var status: ({songs: 2, pending_migration: 1, missing: 0, root: "/library", rollback_copies: []})
        property var task: ({})
        property bool busy: false
        property string message: ""
        property int migrations: 0
        property int verifications: 0
        property int cancellations: 0
        property int restorations: 0
        signal restorePreviewReady(var preview)
        function refresh() {}
        function migrate() { ++migrations }
        function verify() { ++verifications }
        function cancel() { ++cancellations }
        function restore() { ++restorations }
        function backup(directory) {}
        function previewRestore(directory) {}
        function removeRollback(id) {}
    }
    Components.LibraryStorageSettings {
        id: panel
        x: 10; y: 10; width: scene.width - 20; height: implicitHeight
        controller: storage; translator: i18n
    }
    TestCase {
        name: "LibraryStorage"
        when: windowShown
        function init() {
            storage.task = ({}); storage.busy = false; storage.message = ""
            storage.migrations = 0; storage.verifications = 0; storage.cancellations = 0; storage.restorations = 0
            panel.connected = true
        }
        function test_actionsAndBusyState() {
            mouseClick(findChild(panel, "storageMigrate"))
            compare(storage.migrations, 1)
            mouseClick(findChild(panel, "storageVerify"))
            compare(storage.verifications, 1)
            storage.busy = true; storage.task = {task: "a", state: "running", files: 1, total_files: 2}
            verify(!findChild(panel, "storageBackup").enabled)
            mouseClick(findChild(panel, "storageCancel"))
            compare(storage.cancellations, 1)
            storage.task = {task: "a", state: "switching"}
            verify(!findChild(panel, "storageCancel").enabled)
            panel.connected = false
            verify(!findChild(panel, "storageRestore").enabled)
        }
        function test_restoreRequiresConfirmation() {
            compare(storage.restorations, 0)
            storage.restorePreviewReady({songs: 12, missing: []})
            const dialog = findChild(panel, "storageRestoreConfirmation")
            verify(dialog !== null)
            tryCompare(dialog, "visible", true)
            compare(storage.restorations, 0)
            dialog.close()
            compare(storage.restorations, 0)
            storage.restorePreviewReady({songs: 12, missing: []})
            tryCompare(dialog, "visible", true)
            mouseClick(findChild(dialog, "confirmActionButton"))
            compare(storage.restorations, 1)
        }
        function test_layout() {
            for (const width of [640, 900]) {
                scene.width = width
                waitForRendering(panel)
                verify(panel.implicitHeight > 100)
                for (const name of ["storageMigrate", "storageVerify", "storageBackup", "storageRestore"]) {
                    const button = findChild(panel, name)
                    const point = button.mapToItem(panel, 0, 0)
                    verify(point.x >= 0 && point.x + button.width <= panel.width)
                }
            }
            const originalLanguage = i18n.language
            for (const language of ["zh", "en"]) {
                const path = testFixtures.screenshotPath("library-storage-" + language)
                if (path) {
                    i18n.language = language
                    waitForRendering(panel)
                    grabImage(panel).save(path)
                }
            }
            i18n.language = originalLanguage
        }
    }
}
