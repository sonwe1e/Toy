pragma ComponentBehavior: Bound

import QtQuick

Item {
    id: control

    // The host registers ComparisonSurface dynamically, so qmllint cannot prove
    // its QQuickItem inheritance even though the runtime type is a QQuickItem.
    required property var surfaceItem
    property real position: 0.5
    readonly property real splitLogicalX: surfaceItem && typeof surfaceItem.wipeSplitLogicalX === "number" ? surfaceItem.wipeSplitLogicalX : surfaceItem.width * position

    signal positionRequested(real position)

    objectName: "wipeHandle"
    x: surfaceItem.x + Math.round(splitLogicalX) - width / 2
    y: surfaceItem.y
    width: 52
    height: surfaceItem.height

    function updatePosition(sceneX) {
        const point = surfaceItem.mapFromItem(null, sceneX, 0);
        if (surfaceItem && typeof surfaceItem.wipePositionForLogicalX === "function")
            positionRequested(surfaceItem.wipePositionForLogicalX(point.x));
        else
            positionRequested(Math.max(0, Math.min(1, point.x / Math.max(1, surfaceItem.width))));
    }

    Rectangle {
        id: rail

        objectName: "wipeRail"
        width: 3
        height: parent.height
        color: "#ffffffff"
        anchors.centerIn: parent
    }

    Rectangle {
        objectName: "wipeKnob"
        width: 20
        height: 84
        radius: 10
        scale: drag.active ? 1.08 : 1.0
        color: hover.hovered ? "#ff31445d" : "#ff233246"
        border.width: 2
        border.color: "#ffffffff"
        anchors.centerIn: parent

        Behavior on scale {
            NumberAnimation {
                duration: 90
            }
        }

        Column {
            spacing: 4
            anchors.centerIn: parent

            Repeater {
                model: 3

                Rectangle {
                    width: 4
                    height: 4
                    radius: 2
                    color: "#ffffffff"
                }
            }
        }
    }

    DragHandler {
        id: drag

        target: null
        xAxis.enabled: true
        yAxis.enabled: false
        onCentroidChanged: {
            if (active)
                control.updatePosition(centroid.scenePosition.x);
        }
    }

    TapHandler {
        acceptedButtons: Qt.LeftButton
        exclusiveSignals: TapHandler.DoubleTap
        onDoubleTapped: control.positionRequested(0.5)
    }

    HoverHandler {
        id: hover

        cursorShape: Qt.SplitHCursor
    }
}
