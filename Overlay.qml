import QtQuick
import QtQuick.Window

Window {
    id: root
    objectName: "bongocatOverlay"
    visible: false
    width: positionBridge.overlayWidth
    height: positionBridge.overlayHeight
    color: "transparent"
    flags: Qt.FramelessWindowHint | Qt.WindowDoesNotAcceptFocus
    title: "BongoCat Overlay"

    property bool selected: false
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
        // Keep the sprite at its natural aspect ratio and centered. This also
        // makes the content rectangle independent of any transparent padding
        // that has been trimmed from the source PNGs.
        fillMode: Image.PreserveAspectFit
        smooth: true
        mipmap: true
        source: Qt.resolvedUrl("assets/" + root.frameFile)
        // Keep the complete composition visible while mouse decomposition is
        // disabled; REL events must never blank the cat.
        visible: true
        onStatusChanged: if (debugInput && status === Image.Ready) console.info("FRAME", root.frameFile)
        // Flip around the visible body's center so the selection frame keeps
        // hugging the cat in both orientations.
        transform: Scale {
            origin.x: root.width * (positionBridge.contentLeft + positionBridge.contentWidth / 2)
            origin.y: root.height * (positionBridge.contentTop + positionBridge.contentHeight / 2)
            xScale: positionBridge.mirrored ? -1 : 1
        }
    }

    Rectangle {
        id: selectionOutline
        x: root.width * positionBridge.contentLeft
        y: root.height * positionBridge.contentTop
        width: root.width * positionBridge.contentWidth
        height: root.height * positionBridge.contentHeight
        z: 1
        visible: root.selected
        color: "transparent"
        border.width: 2
        border.color: "#69b7ff"
        radius: 4
    }

    MouseArea {
        id: dragArea
        anchors.fill: parent
        z: 2
        acceptedButtons: Qt.LeftButton
        hoverEnabled: true
        cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
        property real lastPointerX: 0
        property real lastPointerY: 0
        property bool dragged: false

        onPressed: (mouse) => {
            lastPointerX = mouse.x
            lastPointerY = mouse.y
            dragged = false
        }
        onPositionChanged: (mouse) => {
            if (!pressed) return
            const dx = mouse.x - lastPointerX
            const dy = mouse.y - lastPointerY
            if (!dragged && Math.abs(dx) + Math.abs(dy) < 3) return
            dragged = true
            // The surface moves under the pointer, so compensate the local
            // coordinates by the distance the window actually travelled.
            const applied = positionBridge.dragBy(dx, dy)
            lastPointerX = mouse.x - applied.x
            lastPointerY = mouse.y - applied.y
        }
        onReleased: {
            if (dragged)
                positionBridge.finishDrag()
            else
                root.selected = !root.selected
        }
    }

    Rectangle {
        id: mirrorButton
        z: 4
        width: 34
        height: 14
        radius: 5
        visible: root.selected
        color: mirrorArea.containsMouse ? "#cc3d79a8" : "#aa24506e"
        border.width: 1
        border.color: positionBridge.mirrored ? "#8fd0ff" : "#bfe3ff"
        x: selectionOutline.x + (selectionOutline.width - width) / 2
        y: Math.max(0, selectionOutline.y - height - 2)

        Text {
            anchors.centerIn: parent
            text: "\u2194"
            color: "#f2f7ff"
            font.pixelSize: 12
            font.bold: true
        }

        MouseArea {
            id: mirrorArea
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: positionBridge.mirrored = !positionBridge.mirrored
        }
    }

    Rectangle {
        id: closeButton
        z: 4
        width: 18
        height: 14
        radius: 5
        visible: root.selected
        color: closeArea.containsMouse ? "#ccb83d4c" : "#aa7a2430"
        border.width: 1
        border.color: "#ffb3c0"
        x: Math.min(root.width - width,
            selectionOutline.x + selectionOutline.width + 4)
        y: Math.max(0, selectionOutline.y - height - 2)

        Text {
            anchors.centerIn: parent
            text: "\u00d7"
            color: "#fff2f4"
            font.pixelSize: 12
            font.bold: true
        }

        MouseArea {
            id: closeArea
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: positionBridge.quit()
        }
    }

    // Only the bottom-right corner resizes the cat. The grip image appears
    // when the frame is shown; its white square sits on the frame corner and
    // the diagonal double arrow points outwards.
    Item {
        id: resizeGrip
        z: 3
        width: 26
        height: 26
        visible: root.selected
        x: Math.max(0, Math.min(root.width - width,
            selectionOutline.x + selectionOutline.width - 6))
        y: Math.max(0, Math.min(root.height - height,
            selectionOutline.y + selectionOutline.height - 6))

        Image {
            anchors.fill: parent
            source: Qt.resolvedUrl("assets/resize-grip.png")
            smooth: true
            mipmap: true
        }

        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.SizeFDiagCursor
            property real lastPointerX: 0
            property real lastPointerY: 0

            onPressed: (mouse) => {
                lastPointerX = resizeGrip.x + mouse.x
                lastPointerY = resizeGrip.y + mouse.y
            }
            onPositionChanged: (mouse) => {
                if (!pressed) return
                const pointerX = resizeGrip.x + mouse.x
                const pointerY = resizeGrip.y + mouse.y
                const applied = positionBridge.resizeBy(2,
                    pointerX - lastPointerX,
                    pointerY - lastPointerY)
                lastPointerX = pointerX - applied.x
                lastPointerY = pointerY - applied.y
            }
            onReleased: positionBridge.finishResize()
        }
    }
}
