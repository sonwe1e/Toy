pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
// qmllint disable import
import Dvs.Ui 1.0

// qmllint enable import

Rectangle {
    id: control

    required property var host
    property alias surface: dualVideoSurface
    property alias videoOutput: surfaceLayer
    readonly property bool roiEnabled: dualVideoSurface.roiEnabled

    function clearRoi() {
        dualVideoSurface.clearRoi();
    }

    function surfaceLabelGeometry(index) {
        const label = surfaceLabelRepeater.itemAt(index);
        if (!label)
            return {};
        const panel = dualVideoSurface.sourcePanelRects[index];
        return {
            "sourceSlot": Number(panel.slot),
            "x": label.x,
            "width": label.width,
            "visible": label.visible
        };
    }

    objectName: "mediaViewportFocusTarget"
    focus: true
    color: "#06080d"
    border.color: control.host.borderColor
    border.width: control.host.chromeVisible ? 1 : 0
    radius: control.host.chromeVisible ? 7 : 0
    clip: true
    TapHandler {
        onTapped: control.forceActiveFocus()
    }

    Item {
        id: surfaceLayer

        anchors {
            fill: parent
            margins: control.host.chromeVisible ? 1 : 0
        }

        // The type is runtime-registered; startup smoke coverage verifies the registration.
        // qmllint disable import unqualified unresolved-type
        ComparisonSurface {
            id: dualVideoSurface

            objectName: "dualVideoSurface"
            Accessible.name: qsTr("VCStation synchronized comparison surface")
            viewMode: control.host.effectiveViewMode
            differenceMetric: control.host.preferences ? control.host.preferences.differenceMetric : ComparisonSurface.RgbAbsolute
            differenceGain: control.host.preferences ? control.host.preferences.differenceGain : ComparisonSurface.Gain1x
            differenceEdge: control.host.preferences ? control.host.preferences.differenceEdge : ComparisonSurface.Edge0And1
            differenceFilter: control.host.preferences ? control.host.preferences.differenceFilter : ComparisonSurface.Bilinear
            wipePosition: control.host.wipePosition
            exactPlaneAvailable: control.host.selectedDifferenceExactness === 0
            thresholdEnabled: control.host.differenceThresholdEnabled
            threshold: Number(control.host.differenceThresholdCode) / 255
            thresholdPolicy: control.host.differenceThresholdPolicy
            referenceSlot: control.host.referenceSourceIndex >= 0 ? control.host.referenceSourceIndex : 0
            anchors.fill: parent
        }
        // qmllint enable import unqualified unresolved-type
    }

    Repeater {
        objectName: "panelDividerRepeater"
        model: control.host.sourceCount > 1 && !control.host.wipeMode && (!control.host.differenceMode || control.host.analysisGridMode) ? dualVideoSurface.sourcePanelRects : []

        Rectangle {
            required property var modelData

            x: Math.round(Number(modelData.x))
            y: Math.round(Number(modelData.y))
            width: Math.round(Number(modelData.width))
            height: Math.round(Number(modelData.height))
            color: "transparent"
            border.width: 1
            border.color: "#d8e2f2"
            opacity: 0.75
            z: 8
        }
    }

    WipeHandle {
        visible: control.host.wipeMode
        z: 50
        surfaceItem: dualVideoSurface
        position: control.host.wipePosition
        onPositionRequested: position => control.host.wipePosition = position
    }

    Repeater {
        model: !control.host.chromeVisible && control.host.sourceCount > 1 ? dualVideoSurface.sourcePanelRects : []

        Rectangle {
            required property var modelData

            x: Math.max(8, Math.min(parent.width - width - 8, Number(modelData.x) + 8))
            y: 8
            width: 30
            height: 26
            radius: 4
            color: "#b3243f68"
            border.color: "#804b8df8"
            z: 10

            Text {
                anchors.centerIn: parent
                text: String.fromCharCode(65 + Number(parent.modelData.slot))
                color: "white"
                font.bold: true
                font.pixelSize: 12
            }
        }
    }

    Rectangle {
        objectName: "immersiveReviewHud"
        visible: !control.host.chromeVisible && control.host.immersiveHudVisible && control.host.immersiveHudText.length > 0
        opacity: visible ? 1.0 : 0.0
        z: 45
        width: immersiveHudLabel.implicitWidth + 24
        height: 34
        radius: 6
        color: "#dc111923"
        border.color: "#803d4d64"
        anchors {
            bottom: parent.bottom
            bottomMargin: 18
            horizontalCenter: parent.horizontalCenter
        }

        Behavior on opacity {
            NumberAnimation {
                duration: 120
            }
        }

        Text {
            id: immersiveHudLabel

            text: control.host.immersiveHudText
            color: control.host.primaryTextColor
            font.pixelSize: 13
            anchors.centerIn: parent
        }
    }

    Rectangle {
        objectName: "framePendingIndicator"
        visible: control.host.showFramePending && control.host.currentFrame >= 0
        z: 40
        radius: 12
        color: "#dc1d2635"
        border.color: control.host.borderColor
        width: pendingRow.implicitWidth + 22
        height: 30
        anchors {
            top: parent.top
            right: parent.right
            margins: 12
        }

        Row {
            id: pendingRow

            spacing: 7
            anchors.centerIn: parent

            BusyIndicator {
                width: 16
                height: 16
                running: parent.parent.visible
            }
            Text {
                text: qsTr("Fetching latest frame…")
                color: control.host.mutedTextColor
                font.pixelSize: 11
            }
        }
    }

    Rectangle {
        visible: control.host.exportMessage.length > 0
        z: 39
        radius: 5
        color: "#e61b2432"
        border.color: control.host.borderColor
        width: Math.min(parent.width - 32, exportStatusText.implicitWidth + 24)
        height: exportStatusText.implicitHeight + 16
        anchors {
            top: parent.top
            topMargin: 16
            horizontalCenter: parent.horizontalCenter
        }

        Text {
            id: exportStatusText

            text: control.host.exportMessage
            color: control.host.primaryTextColor
            font.pixelSize: 12
            wrapMode: Text.Wrap
            anchors {
                fill: parent
                margins: 8
            }
        }
    }

    MouseArea {
        id: viewportNavigation

        anchors.fill: parent
        anchors.margins: 1
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        hoverEnabled: true

        onWheel: wheel => {
            const point = control.host.panelPoint(wheel.x, wheel.y);
            dualVideoSurface.zoomAt(point.x, point.y, wheel.angleDelta.y > 0 ? 1.25 : 0.8);
            wheel.accepted = true;
        }
        onPressed: mouse => {
            if (mouse.button === Qt.RightButton) {
                control.host.openViewerContextMenu();
                return;
            }
            const point = control.host.panelPoint(mouse.x, mouse.y);
            control.host.roiPanel = point.panel;
            control.host.panLastX = point.x;
            control.host.panLastY = point.y;
            if ((mouse.modifiers & Qt.ShiftModifier) !== 0) {
                control.host.roiSelecting = true;
                control.host.roiStartX = mouse.x;
                control.host.roiStartY = mouse.y;
                control.host.roiCurrentX = mouse.x;
                control.host.roiCurrentY = mouse.y;
            }
            control.forceActiveFocus();
        }
        onPositionChanged: mouse => {
            const point = control.host.panelPoint(mouse.x, mouse.y);
            if (control.host.roiSelecting) {
                control.host.roiCurrentX = mouse.x;
                control.host.roiCurrentY = mouse.y;
            } else if (pressed && point.panel === control.host.roiPanel) {
                dualVideoSurface.panBy(point.x - control.host.panLastX, point.y - control.host.panLastY);
                control.host.panLastX = point.x;
                control.host.panLastY = point.y;
            }
        }
        onReleased: mouse => {
            if (control.host.roiSelecting) {
                const start = control.host.panelPoint(control.host.roiStartX, control.host.roiStartY);
                const end = control.host.panelPoint(mouse.x, mouse.y);
                if (start.panel === end.panel)
                    dualVideoSurface.setRoiNormalized(start.x, start.y, end.x, end.y);
            }
            control.host.roiSelecting = false;
            control.host.roiPanel = -1;
        }
        onDoubleClicked: control.host.toggleFullScreen()
    }

    Rectangle {
        visible: control.host.roiSelecting
        x: Math.min(control.host.roiStartX, control.host.roiCurrentX)
        y: Math.min(control.host.roiStartY, control.host.roiCurrentY)
        width: Math.abs(control.host.roiCurrentX - control.host.roiStartX)
        height: Math.abs(control.host.roiCurrentY - control.host.roiStartY)
        color: "#224b8df8"
        border.color: control.host.accentColor
        border.width: 1
    }

    Rectangle {
        id: analysisChrome

        objectName: "analysisControlsChrome"
        visible: control.host.chromeVisible && (control.host.differenceMode || dualVideoSurface.roiEnabled)
        z: 30
        width: analysisStatus.implicitWidth + 18
        height: 28
        radius: 5
        color: "#dc171e2a"
        border.color: control.host.borderColor
        anchors {
            right: parent.right
            rightMargin: 12
            bottom: parent.bottom
            bottomMargin: 12
        }

        Label {
            id: analysisStatus

            text: {
                const parts = [];
                if (control.host.differenceMode)
                    parts.push(control.host.comparisonExactnessLabel(control.host.selectedDifferenceExactness));
                if (dualVideoSurface.roiEnabled)
                    parts.push(qsTr("ROI active"));
                return parts.join(" · ");
            }
            color: control.host.selectedDifferenceExactness === 0 ? "#86efac" : "#facc15"
            font.pixelSize: 11
            anchors.centerIn: parent
        }
    }

    Rectangle {
        id: differenceUnavailableOverlay

        objectName: "differenceUnavailableOverlay"
        visible: control.host.differenceUnavailableDetail.length > 0
        width: Math.min(parent.width - 48, 460)
        height: unavailableColumn.implicitHeight + 32
        radius: 8
        color: "#e6121822"
        border.color: control.host.errorColor
        border.width: 1
        z: 40
        anchors.centerIn: parent

        Column {
            id: unavailableColumn

            width: parent.width - 32
            spacing: 6
            anchors.centerIn: parent

            Text {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                text: qsTr("Difference unavailable")
                color: control.host.primaryTextColor
                font.pixelSize: 17
                font.weight: Font.DemiBold
            }

            Text {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                text: control.host.differenceUnavailableDetail
                color: "#fca5a5"
                font.pixelSize: 13
            }
        }
    }

    Rectangle {
        id: alignmentStatus

        visible: control.host.chromeVisible && control.host.combinedAlignmentStatus.length > 0
        radius: 5
        color: "#d9232c3d"
        border.color: control.host.errorColor
        height: mappingStatusText.implicitHeight + 14
        width: Math.min(parent.width - 24, mappingStatusText.implicitWidth + 24)
        z: 20
        anchors {
            top: parent.top
            topMargin: 12
            horizontalCenter: parent.horizontalCenter
        }

        Text {
            id: mappingStatusText

            text: control.host.combinedAlignmentStatus
            color: "#ffd2d2"
            font.pixelSize: 12
            font.weight: Font.DemiBold
            anchors.centerIn: parent
        }
    }

    Item {
        id: surfaceLabels

        visible: control.host.chromeVisible
        z: 10
        anchors {
            fill: parent
            margins: control.host.chromeVisible ? 1 : 0
        }

        Repeater {
            id: surfaceLabelRepeater

            objectName: "surfaceLabelRepeater"
            model: dualVideoSurface.sourcePanelRects

            delegate: Rectangle {
                id: surfaceLabel

                required property int index
                required property var modelData
                objectName: "surfaceLabel"
                property int sourceSlot: Number(modelData.slot)

                readonly property real panelWidth: Number(modelData.width)
                readonly property bool wipeBadge: control.host.wipeMode
                readonly property bool showFilename: !control.host.singleMode && !wipeBadge && panelWidth >= 160
                readonly property bool compactBadge: control.host.singleMode || wipeBadge || panelWidth < 160

                visible: wipeBadge || control.host.singleMode || panelWidth >= 80
                x: wipeBadge ? (Number(modelData.slot) === control.host.differenceFirstSlot ? 12 : parent.width - width - 12) : Number(modelData.x) + 12
                y: Number(modelData.y) + 12
                width: compactBadge ? 44 : Math.min(280, Math.max(80, panelWidth - 24))
                height: compactBadge ? 36 : 42
                radius: 5
                color: "#d9111721"
                border.color: "#663a4a62"

                Rectangle {
                    width: 26
                    height: 26
                    radius: 4
                    color: control.host.accentColor
                    anchors {
                        left: parent.left
                        leftMargin: surfaceLabel.compactBadge ? 9 : 8
                        verticalCenter: parent.verticalCenter
                    }

                    Text {
                        anchors.centerIn: parent
                        text: String.fromCharCode(65 + Number(surfaceLabel.modelData.slot))
                        color: "white"
                        font.bold: true
                        font.pixelSize: 13
                    }
                }

                Text {
                    visible: surfaceLabel.showFilename
                    text: control.host.sourceFilename(Number(surfaceLabel.modelData.slot))
                    color: control.host.primaryTextColor
                    font.pixelSize: 12
                    elide: Text.ElideMiddle
                    anchors {
                        left: parent.left
                        leftMargin: 46
                        right: parent.right
                        rightMargin: 10
                        verticalCenter: parent.verticalCenter
                    }
                }
            }
        }
    }

    Rectangle {
        id: frameErrorBanner

        objectName: "frameErrorBanner"
        width: Math.min(parent.width - 48, 720)
        height: frameErrorBannerColumn.implicitHeight + 20
        radius: 6
        visible: control.host.frameErrorBannerVisible
        color: "#e6351f2a"
        border.color: "#b9503f4a"
        z: 40
        Accessible.name: qsTr("Frame unchanged. %1").arg(control.host.errorDetails())
        anchors {
            top: parent.top
            topMargin: alignmentStatus.visible ? alignmentStatus.height + 20 : 12
            horizontalCenter: parent.horizontalCenter
        }

        Column {
            id: frameErrorBannerColumn

            width: parent.width - 28
            spacing: 3
            anchors.centerIn: parent

            Text {
                width: parent.width
                text: qsTr("Frame unchanged")
                color: "#ffb4b4"
                font.pixelSize: 13
                font.weight: Font.DemiBold
            }

            Text {
                objectName: "frameErrorBannerDetail"
                width: parent.width
                text: control.host.errorDetails()
                color: control.host.primaryTextColor
                font.pixelSize: 11
                wrapMode: Text.Wrap
            }
        }
    }

    Rectangle {
        id: statusOverlay

        objectName: "statusOverlay"
        width: Math.min(parent.width - 48, 560)
        height: overlayColumn.implicitHeight + 32
        radius: 7
        visible: control.host.overlayVisible
        color: control.host.hasErrors && !control.host.busy ? "#ee351f2a" : "#ed151d29"
        border.color: control.host.hasErrors && !control.host.busy ? "#a9503f4a" : "#a43d4d64"
        anchors.centerIn: parent

        Column {
            id: overlayColumn

            width: parent.width - 36
            spacing: 8
            anchors.centerIn: parent

            BusyIndicator {
                width: 34
                height: 34
                running: control.host.busy && control.host.currentFrame < 0
                visible: running
                anchors.horizontalCenter: parent.horizontalCenter
                Accessible.name: qsTr("Loading")
            }

            Text {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                text: control.host.overlayTitle
                color: control.host.hasErrors && !control.host.busy ? "#ffb4b4" : control.host.primaryTextColor
                font.pixelSize: 17
                font.weight: Font.DemiBold
                wrapMode: Text.Wrap
            }

            Text {
                objectName: "statusDetail"
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                text: control.host.overlayDetail
                color: control.host.mutedTextColor
                font.pixelSize: 12
                lineHeight: 1.25
                wrapMode: Text.Wrap
            }
        }
    }
}
