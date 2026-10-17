import QtQuick
import QtTest
import "../../frontend/qt-qml/qml/components" as PlayerComponents

Item {
    id: scene
    // Leave enough capture area when Qt's software grab reports a unit DPR.
    width: 640 * Math.max(1, Screen.devicePixelRatio)
    height: 160 * Math.max(1, Screen.devicePixelRatio)

    Component {
        id: sliderComponent
        PlayerComponents.PlayerSlider {
            x: 20
            y: 20
            width: 400
            height: 40
            from: 0
            to: 1
            value: 0.2
        }
    }

    TestCase {
        id: testCase
        name: "PlayerSlider"
        when: windowShown

        property var slider: null

        SignalSpy {
            id: movedSpy
            target: testCase.slider
            signalName: "moved"
        }

        function init() {
            slider = createTemporaryObject(sliderComponent, scene)
            verify(slider !== null)
            mouseMove(scene, 600, 120)
            wait(120)
            movedSpy.clear()
        }

        function cleanup() {
            if (slider && slider.pressed)
                mouseRelease(slider, slider.width / 2, slider.height / 2)
            slider = null
            movedSpy.clear()
        }

        function test_trackMeetsHandle_data() {
            return [
                {tag: "start", fraction: 0, width: 400, mirrored: false},
                {tag: "early", fraction: 0.03, width: 400, mirrored: false},
                {tag: "middle", fraction: 0.5, width: 400, mirrored: false},
                {tag: "late", fraction: 0.97, width: 400, mirrored: false},
                {tag: "end", fraction: 1, width: 400, mirrored: false},
                {tag: "compact-volume", fraction: 0.25, width: 72, mirrored: false},
                {tag: "rtl-start", fraction: 0, width: 400, mirrored: true},
                {tag: "rtl-early", fraction: 0.03, width: 400, mirrored: true},
                {tag: "rtl-middle", fraction: 0.5, width: 400, mirrored: true},
                {tag: "rtl-late", fraction: 0.97, width: 400, mirrored: true},
                {tag: "rtl-end", fraction: 1, width: 400, mirrored: true},
                {tag: "rtl-compact-volume", fraction: 0.25, width: 72, mirrored: true}
            ]
        }

        function test_trackMeetsHandle(data) {
            slider.width = data.width
            slider.value = data.fraction
            slider.LayoutMirroring.enabled = data.mirrored
            slider.activeColor = "#cc99ff"
            slider.baseColor = "#333333"
            verify(waitForRendering(slider))

            const centerX = slider.handle.x + slider.handle.width / 2
            const centerY = Math.floor(slider.height / 2)
            const direction = data.mirrored ? -1 : 1
            // Grab the origin-aligned scene: Qt's software renderer can crop
            // child snapshots incorrectly at the 0.5 scale used by Windows CI.
            const snapshot = grabImage(scene)
            const pixelScale = scene.Screen.devicePixelRatio
            function pixelAt(x, y) {
                const point = slider.mapToItem(scene, x, y)
                return snapshot.pixel(Math.floor(point.x * pixelScale), Math.floor(point.y * pixelScale))
            }
            // Sample just outside the resting dot: no gap before it or fill after it.
            if (data.fraction > 0)
                compare(pixelAt(centerX - direction * 7, centerY), slider.activeColor)
            if (data.fraction < 1)
                compare(pixelAt(centerX + direction * 7, centerY), slider.baseColor)
        }

        // Volume uses the base slider's live updates rather than the seek control's release-only behavior.
        // At least one moved signal must occur while the pointer is still pressed.
        // Release retains the last visible volume instead of restoring the pre-drag value.
        function test_volumeChangesContinuouslyWhileDragging() {
            const startX = slider.leftPadding + slider.handle.width / 2
                    + 0.2 * (slider.availableWidth - slider.handle.width)
            mousePress(slider, startX, slider.height / 2)
            movedSpy.clear()
            mouseMove(slider, slider.width * 0.75, slider.height / 2)
            verify(slider.pressed)
            verify(slider.value > 0.7)
            verify(movedSpy.count > 0)
            mouseRelease(slider, slider.width * 0.75, slider.height / 2)
            verify(slider.value > 0.7)
        }

        // Hover and pressed styling may change color without changing the handle's hit geometry.
        // Resizing the handle would alter the pointer-to-value mapping during an interaction.
        // The test checks width and position across both states to prevent that discontinuity.
        function test_handleGeometryStaysStableWhileHoveringAndPressed() {
            const originalWidth = slider.handle.width
            const originalX = slider.handle.x
            const centerX = originalX + originalWidth / 2
            mouseMove(slider, centerX, slider.height / 2)
            wait(150)
            compare(slider.handle.width, originalWidth)
            compare(slider.handle.x, originalX)
            mousePress(slider, centerX, slider.height / 2)
            wait(150)
            compare(slider.handle.width, originalWidth)
            mouseRelease(slider, centerX, slider.height / 2)
            wait(150)
            compare(slider.handle.width, originalWidth)
        }
    }
}
