import QtQuick
import QtTest
import "../../frontend/qt-qml/qml/pages" as Pages
import "../../frontend/qt-qml/qml/components" as Components

Rectangle {
    id: scene
    width: 1000; height: 640; color: "#0E0D14"
    QtObject { id: transportStub; property bool connected: true }
    QtObject { id: shellStub; property var theme: Components.Theme }
    QtObject {
        id: historyStub
        property var records: []
        property var stats: ({})
        property bool loading: false
        property bool loaded: true
        property string error: ""
        property string statsError: ""
        property int offset: 0
        property int total: 0
        property var lastFilter: []
        property int clearCount: 0
        signal requestSucceeded(string method)
        signal requestFailed(string method, string message)
        function filter(days, search, complete) { lastFilter = [days, search, complete]; offset = 0 }
        function refresh() {}
        function goToPage(value) { offset = value }
        function clearHistory() { clearCount++ }
    }
    QtObject { id: app; property var history: historyStub }
    Component { id: historyPage; Pages.HistoryPage { shell: shellStub; controllers: app; translator: testTranslator; transport: transportStub; width: scene.width; height: scene.height } }
    Component { id: summaryComponent; Components.ListeningSummary { history: historyStub; translator: testTranslator; transport: transportStub; width: 850; height: implicitHeight; x: 32; y: 32 } }
    TestCase {
        name: "ListeningHistory"
        when: windowShown
        property var page
        function init() {
            transportStub.connected = true; historyStub.loading = false; historyStub.error = ""; historyStub.statsError = ""
            historyStub.offset = 0; historyStub.total = 60; historyStub.clearCount = 0
            historyStub.records = [
                {id: "first", title: "夜に聴く音楽 — a very long song title", artist: "猫のラジオ", started_at: "2026-10-19T12:10:00Z", listened_ms: 186000, completed: true, end_reason: "ended"},
                {id: "second", title: "<b>Plain text title</b>", artist: "Artist", started_at: "2026-10-19T11:50:00Z", listened_ms: 32000, completed: false, end_reason: "skipped"}
            ]
            const days = []
            for (let i = 0; i < 30; i++) {
                const date = new Date(2026, 8, 20 + i, 12)
                days.push({date: Qt.formatDate(date, "yyyy-MM-dd"), listened_ms: i % 5 === 0 ? 0 : i * 300000})
            }
            historyStub.stats = {days: days, week_ms: 11250000, total_sessions: 60, top_songs: [
                {title: "夜に聴く音楽", artist: "猫のラジオ", listened_ms: 7200000},
                {title: "Moonlight", artist: "Artist", listened_ms: 4800000}
            ]}
            page = createTemporaryObject(historyPage, scene)
            verify(page !== null)
            waitForRendering(page)
        }
        function cleanup() { page = null }
        function child(name) { const item = findChild(page, name); verify(item !== null, name); return item }
        function test_filtersAndPagination() {
            const search = child("historySearch")
            mouseClick(search); keyClick(Qt.Key_N); keyClick(Qt.Key_I)
            tryCompare(historyStub, "lastFilter", [30, "ni", false])
            mouseClick(child("historyCompleted"))
            compare(historyStub.lastFilter, [30, "ni", true])
            const period = child("historyPeriod")
            period.currentIndex = 0; period.activated(0)
            compare(historyStub.lastFilter[0], 1)
            mouseClick(child("historyNext")); compare(historyStub.offset, 50)
            verify(!child("historyNext").enabled)
            mouseClick(child("historyPrevious")); compare(historyStub.offset, 0)
        }
        function test_clearWaitsForBackendAndPreservesFailure() {
            mouseClick(child("historyClear"))
            const dialog = child("historyClearDialog")
            tryCompare(dialog, "opened", true)
            mouseClick(findChild(dialog, "confirmActionButton"))
            compare(historyStub.clearCount, 1); verify(dialog.busy); verify(dialog.opened)
            historyStub.requestFailed("history.clear", "Storage unavailable")
            verify(!dialog.busy); verify(dialog.opened); compare(dialog.errorText, "Storage unavailable")
            mouseClick(findChild(dialog, "confirmActionButton"))
            historyStub.requestSucceeded("history.clear")
            tryCompare(dialog, "opened", false)
        }
        function test_historyAndHeatmapScreenshots() {
            let path = testFixtures.screenshotPath("listening-history")
            if (path) grabImage(scene).save(path)
            page.visible = false
            const summary = createTemporaryObject(summaryComponent, scene)
            verify(summary !== null)
            tryVerify(() => summary.height > 200)
            wait(100); waitForRendering(summary)
            verify(findChild(summary, "homeListeningHeatmap") !== null)
            verify(!findChild(summary, "homeHistoryEmpty").visible)
            path = testFixtures.screenshotPath("listening-summary")
            if (path) grabImage(scene).save(path)
            historyStub.stats = {days: historyStub.stats.days, total_sessions: 0, top_songs: [], week_ms: 0}
            tryCompare(findChild(summary, "homeHistoryEmpty"), "visible", true)
        }
        function test_offlineAndEmpty() {
            historyStub.records = []; historyStub.total = 0
            compare(child("historyList").count, 0)
            transportStub.connected = false
            verify(!child("historyClear").enabled)
            verify(!child("historyNext").enabled)
        }
    }
}
