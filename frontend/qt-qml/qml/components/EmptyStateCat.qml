import QtQuick

// Decorative empty-state illustration (ui-design-guidelines Section 6.1).
// A calm cat wearing headphones, drawn in muted lavender/rose so it reads as
// brand texture rather than competing with the message text below it.
// Purely presentational: it carries no interaction and no product copy.
Item {
    id: root

    property color inkColor: Theme.accentPrimary
    property color accentColor: Theme.accentSecondary
    property real lineWidth: 2

    implicitWidth: 132
    implicitHeight: 132
    opacity: 0.82

    // Soft ambient halo behind the character.
    Rectangle {
        anchors.centerIn: parent
        width: parent.width * 0.92
        height: width
        radius: width / 2
        opacity: 0.9
        gradient: Gradient {
            GradientStop { position: 0.0; color: Qt.rgba(root.inkColor.r, root.inkColor.g, root.inkColor.b, 0.16) }
            GradientStop { position: 1.0; color: "transparent" }
        }
    }

    Canvas {
        anchors.fill: parent
        antialiasing: true

        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            const w = width, h = height
            const cx = w * 0.5
            const cy = h * 0.54
            const r = w * 0.26

            ctx.lineCap = "round"
            ctx.lineJoin = "round"
            ctx.lineWidth = root.lineWidth * (w / 132)

            const stroke = root.inkColor
            const fill = root.accentColor

            // Ears: two rounded triangles on top of the head.
            ctx.fillStyle = fill
            ctx.beginPath()
            ctx.moveTo(cx - r * 0.86, cy - r * 0.56)
            ctx.lineTo(cx - r * 0.72, cy - r * 1.5)
            ctx.lineTo(cx - r * 0.18, cy - r * 0.9)
            ctx.closePath()
            ctx.fill()
            ctx.beginPath()
            ctx.moveTo(cx + r * 0.86, cy - r * 0.56)
            ctx.lineTo(cx + r * 0.72, cy - r * 1.5)
            ctx.lineTo(cx + r * 0.18, cy - r * 0.9)
            ctx.closePath()
            ctx.fill()

            // Head.
            ctx.fillStyle = Qt.rgba(stroke.r, stroke.g, stroke.b, 0.10)
            ctx.strokeStyle = stroke
            ctx.beginPath()
            ctx.ellipse(cx - r, cy - r, r * 2, r * 2, 0, 0, Math.PI * 2)
            ctx.fill()
            ctx.stroke()

            // Closed, content eyes: two gentle arcs.
            ctx.beginPath()
            ctx.arc(cx - r * 0.42, cy - r * 0.05, r * 0.22, Math.PI * 1.1, Math.PI * 1.9)
            ctx.arc(cx + r * 0.42, cy - r * 0.05, r * 0.22, Math.PI * 1.1, Math.PI * 1.9)
            ctx.stroke()

            // Nose and mouth.
            ctx.beginPath()
            ctx.moveTo(cx, cy + r * 0.28)
            ctx.lineTo(cx, cy + r * 0.42)
            ctx.moveTo(cx, cy + r * 0.42)
            ctx.arc(cx - r * 0.14, cy + r * 0.42, r * 0.14, 0, Math.PI * 0.9)
            ctx.moveTo(cx, cy + r * 0.42)
            ctx.arc(cx + r * 0.14, cy + r * 0.42, r * 0.14, Math.PI * 0.1, Math.PI)
            ctx.stroke()

            // Headphones: band over the head plus two ear cups.
            ctx.strokeStyle = stroke
            ctx.beginPath()
            ctx.arc(cx, cy, r * 1.32, Math.PI * 1.16, Math.PI * 1.84)
            ctx.stroke()

            ctx.fillStyle = fill
            const cupR = r * 0.34
            ctx.beginPath()
            ctx.ellipse(cx - r * 1.34, cy - cupR * 0.1, cupR, cupR * 1.4, 0, 0, Math.PI * 2)
            ctx.fill()
            ctx.stroke()
            ctx.beginPath()
            ctx.ellipse(cx + r * 1.34, cy - cupR * 0.1, cupR, cupR * 1.4, 0, 0, Math.PI * 2)
            ctx.fill()
            ctx.stroke()

            // Whiskers.
            ctx.beginPath()
            ctx.moveTo(cx - r * 0.9, cy + r * 0.3); ctx.lineTo(cx - r * 1.28, cy + r * 0.24)
            ctx.moveTo(cx - r * 0.9, cy + r * 0.55); ctx.lineTo(cx - r * 1.26, cy + r * 0.62)
            ctx.moveTo(cx + r * 0.9, cy + r * 0.3); ctx.lineTo(cx + r * 1.28, cy + r * 0.24)
            ctx.moveTo(cx + r * 0.9, cy + r * 0.55); ctx.lineTo(cx + r * 1.26, cy + r * 0.62)
            ctx.stroke()
        }
    }
}
