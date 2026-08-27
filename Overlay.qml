import QtQuick
import QtQuick.Window

Window {
    id: root
    objectName: "bongocatOverlay"
    visible: false
    width: overlayWidth
    height: overlayHeight
    color: "transparent"
    flags: Qt.FramelessWindowHint | Qt.WindowDoesNotAcceptFocus
        | Qt.WindowTransparentForInput
    title: "BongoCat Overlay"

    property bool leftDown: false
    property bool rightDown: false
    property real handX: 0
    property real handY: 0
    property real targetHandX: 0
    property real targetHandY: 0
    property int keypressDuration: 95
    readonly property string frameFile: leftDown && rightDown
        ? "bongo-cat-both-down.png"
        : leftDown ? "bongo-cat-left-down.png"
        : rightDown ? "bongo-cat-right-down.png"
        : "bongo-cat-both-up.png"

    function triggerLeft() { console.info("QML -> triggerLeft"); leftDown = true; leftTimer.restart() }
    function triggerRight() { console.info("QML -> triggerRight"); rightDown = true; rightTimer.restart() }
    function moveMouse(dx, dy) {
        targetHandX = Math.max(-26, Math.min(26, targetHandX + dx * 0.55))
        targetHandY = Math.max(-18, Math.min(18, targetHandY + dy * 0.55))
        handX = targetHandX
        handY = targetHandY
        mouseRest.restart()
    }

    Connections {
        target: inputBridge
        function onLeftTriggered() { if (debugInput) console.info("QML -> triggerLeft"); root.triggerLeft() }
        function onRightTriggered() { if (debugInput) console.info("QML -> triggerRight"); root.triggerRight() }
        function onMouseMoved(dx, dy) { if (debugInput) console.info("QML -> mouseMoved", dx, dy); root.moveMouse(dx, dy) }
        function onMouseButton(button) { /* mouse buttons never drive keyboard paws */ }
    }
    Timer { id: leftTimer; interval: root.keypressDuration; onTriggered: root.leftDown = false }
    Timer { id: rightTimer; interval: root.keypressDuration; onTriggered: root.rightDown = false }
    Timer {
        id: mouseRest
        interval: 150
        onTriggered: {
            root.targetHandX = 0
            root.targetHandY = 0
            root.handX = 0
            root.handY = 0
        }
    }
    Behavior on handX { NumberAnimation { duration: 45; easing.type: Easing.OutCubic } }
    Behavior on handY { NumberAnimation { duration: 45; easing.type: Easing.OutCubic } }

    Image {
        id: frame
        anchors.fill: parent
        fillMode: Image.Stretch
        smooth: true
        mipmap: true
        source: Qt.resolvedUrl("assets/" + root.frameFile)
        // Keep the complete composition visible while mouse decomposition is
        // disabled; REL events must never blank the cat.
        visible: true
        onStatusChanged: if (debugInput && status === Image.Ready) console.info("FRAME", root.frameFile)
    }
}
